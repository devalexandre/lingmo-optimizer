#include "devtools.h"

#include "core/format.h"

#include <QFutureWatcher>
#include <QHash>
#include <QLocale>
#include <QRegularExpression>
#include <QProcess>
#include <QSettings>
#include <QStandardPaths>
#include <QtConcurrent>

#include <algorithm>

using namespace DevCaches;

DevTools::DevTools(QObject *parent)
    : QObject(parent)
{
    QSettings settings;
    m_includeProjects = settings.value(QStringLiteral("devtools/includeProjects"), true).toBool();
    m_progressTimer.setInterval(150);
    connect(&m_progressTimer, &QTimer::timeout, this, [this] {
        if (!m_progress)
            return;
        m_progressLabel = m_progress->currentLabel();
        m_progressDone = m_progress->done.load();
        m_progressTotal = m_progress->total.load();
        emit progressChanged();
    });
}

DevTools::~DevTools()
{
    if (m_progress)
        m_progress->cancel = true;
    m_future.waitForFinished();
}

void DevTools::setIncludeProjects(bool include)
{
    if (include == m_includeProjects)
        return;
    m_includeProjects = include;
    QSettings().setValue(QStringLiteral("devtools/includeProjects"), include);
    emit includeProjectsChanged();
}

QString DevTools::scannedAt() const
{
    return m_scannedAt.isValid() ? QLocale(QLocale::Portuguese, QLocale::Brazil).toString(m_scannedAt.time(), QStringLiteral("HH:mm"))
                                 : QString();
}

QVariantList DevTools::groups() const
{
    QVariantList list;
    for (const QString &g : groupOrder()) {
        qint64 size = 0;
        int count = 0;
        for (const Item &item : m_items) {
            if (item.group != g)
                continue;
            ++count;
            size += qMax<qint64>(0, item.size);
        }
        if (count == 0)
            continue;
        list.append(QVariantMap {{QStringLiteral("id"), g}, {QStringLiteral("title"), groupTitle(g)},
                                 {QStringLiteral("icon"), groupIcon(g)}, {QStringLiteral("size"), double(size)},
                                 {QStringLiteral("count"), count}});
    }
    return list;
}

bool DevTools::find(const QString &id, Item *out) const
{
    for (const Item &item : m_items) {
        if (item.id == id) {
            if (out)
                *out = item;
            return true;
        }
    }
    return false;
}

QVariantMap DevTools::item(const QString &id) const
{
    Item item;
    return find(id, &item) ? toVariant(item) : QVariantMap();
}

void DevTools::scan()
{
    if (m_loaded || m_scanning)
        return;
    start({}, true);
}

void DevTools::rescan()
{
    if (m_scanning) {
        m_pending = true;
        return;
    }
    start({}, true);
}

void DevTools::refresh(const QStringList &ids)
{
    if (!m_loaded && !m_scanning)
        return;
    if (m_scanning) {
        m_pending = true;
        for (const QString &id : ids)
            m_pendingIds.insert(id);
        return;
    }
    start(QSet<QString>(ids.begin(), ids.end()), false);
}

void DevTools::cancel()
{
    if (m_progress)
        m_progress->cancel = true;
}

void DevTools::start(const QSet<QString> &remeasure, bool full)
{
    m_scanning = true;
    m_pending = false;
    m_pendingIds.clear();
    emit scanningChanged();
    m_progress = std::make_shared<Progress>();
    m_progressTimer.start();

    Env env = Env::system();
    env.scanProjects = m_includeProjects;
    const QList<Item> previous = full ? QList<Item>() : m_items;
    auto progress = m_progress;

    auto *watcher = new QFutureWatcher<QList<Item>>(this);
    connect(watcher, &QFutureWatcher<QList<Item>>::finished, this, [this, watcher] {
        const QList<Item> items = watcher->result();
        watcher->deleteLater();
        m_progressTimer.stop();
        const bool cancelled = m_progress && m_progress->cancel.load();
        m_progress.reset();
        m_progressLabel.clear();
        m_progressDone = m_progressTotal = 0;
        emit progressChanged();
        if (!cancelled || m_items.isEmpty()) {
            m_scannedAt = QDateTime::currentDateTime();
            m_loaded = true;
            setItems(items);
        }
        m_scanning = false;
        emit scanningChanged();
        if (m_pending) {
            const QSet<QString> ids = m_pendingIds;
            start(ids, ids.isEmpty());
        }
    });
    m_future = QtConcurrent::run([env, previous, remeasure, progress]() {
        QList<Item> items = detect(env, progress.get());
        QSet<QString> toMeasure;
        if (!previous.isEmpty()) {
            // Keep the sizes measured before for what was not cleaned
            QHash<QString, Item> old;
            for (const Item &item : previous)
                old.insert(item.id, item);
            for (Item &item : items) {
                if (item.sizeFromTool)
                    continue;
                auto it = old.constFind(item.id);
                if (it != old.constEnd() && !remeasure.contains(item.id) && it->paths == item.paths) {
                    item.size = it->size;
                    item.partial = it->partial;
                    item.children = it->children;
                } else {
                    toMeasure.insert(item.id);
                }
            }
        }
        if (previous.isEmpty() || !toMeasure.isEmpty())
            measure(items, env, progress.get(), 15000, 120000, toMeasure);
        // Biggest first inside each group; nothing empty unless it says something
        std::stable_sort(items.begin(), items.end(), [](const Item &a, const Item &b) {
            const int ga = groupOrder().indexOf(a.group), gb = groupOrder().indexOf(b.group);
            if (ga != gb)
                return ga < gb;
            if (a.current != b.current)
                return !a.current;
            return a.size > b.size;
        });
        items.erase(std::remove_if(items.begin(), items.end(), [](const Item &item) {
                        const bool versionOrList = item.reinstall || item.action == Action::None || item.current;
                        return item.size == 0 && !item.partial && !versionOrList && item.blockedReason.isEmpty();
                    }),
                    items.end());
        return items;
    });
    watcher->setFuture(m_future);
}

void DevTools::setItems(const QList<Item> &items)
{
    m_items = items;
    m_variants.clear();
    for (const Item &item : std::as_const(m_items))
        m_variants.append(toVariant(item));
    emit itemsChanged();
}

void DevTools::openLink(const QString &link)
{
    if (!link.startsWith(QLatin1String("settings:")))
        return;
    const QString page = link.mid(9);
    static const QRegularExpression valid(QStringLiteral("^[a-z]+(/[a-z]+)?$"));
    if (!valid.match(page).hasMatch())
        return;
    const QString settings = QStandardPaths::findExecutable(QStringLiteral("lingmo-settings"));
    if (!settings.isEmpty())
        QProcess::startDetached(settings, {QStringLiteral("-m"), page});
}
