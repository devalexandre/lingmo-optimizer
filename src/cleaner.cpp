#include "cleaner.h"

#include "core/devcaches.h"
#include "core/diskusage.h"
#include "devtools.h"
#include "core/format.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QPointer>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QtConcurrent>

#ifndef HELPER_PATH
#define HELPER_PATH "/usr/lib/lingmo-optimizer/clean-system"
#endif

static const QStringList kRootSteps = {QStringLiteral("pacman-cache"), QStringLiteral("pacman-old"), QStringLiteral("pacman-uninstalled"),
                                        QStringLiteral("journal"), QStringLiteral("flatpak")};

static QString helperPath()
{
    const QString overridden = qEnvironmentVariable("LINGMO_OPTIMIZER_HELPER");
    return overridden.isEmpty() ? QStringLiteral(HELPER_PATH) : overridden;
}

Cleaner::Cleaner(QObject *parent)
    : QObject(parent)
{
    const bool hasFlatpak = !QStandardPaths::findExecutable(QStringLiteral("flatpak")).isEmpty();
    auto step = [](const QString &id, const QString &title, const QString &description, bool root) {
        return QVariantMap {{QStringLiteral("id"), id}, {QStringLiteral("title"), title},
                            {QStringLiteral("description"), description}, {QStringLiteral("root"), root},
                            {QStringLiteral("size"), -1.0}, {QStringLiteral("upTo"), false}};
    };
    // The pacman cache and the caches of developer tools are items of
    // "Ferramentas de desenvolvimento" (DevTools), listed below these steps
    m_steps.append(step(QStringLiteral("user-cache"), tr("Caches de programas"),
                        tr("Miniaturas, shaders de vídeo e fontes. São recriados quando preciso."), false));
    m_steps.append(step(QStringLiteral("journal"), tr("Registros do sistema"),
                        tr("Mensagens do journal com mais de 7 dias."), true));
    if (hasFlatpak)
        m_steps.append(step(QStringLiteral("flatpak"), tr("Runtimes Flatpak sem uso"),
                            tr("Bibliotecas Flatpak que nenhum aplicativo instalado usa mais."), true));
}

Cleaner::~Cleaner()
{
    if (m_process)
        m_process->disconnect(this);
}

QStringList Cleaner::safeCacheDirs()
{
    const QString cache = QStandardPaths::writableLocation(QStandardPaths::GenericCacheLocation);
    const QString home = QDir::homePath();
    QStringList dirs;
    // pip, npm, yarn, Go, yay/paru... are "Ferramentas de desenvolvimento"
    for (const char *name : {"thumbnails", "mesa_shader_cache", "mesa_shader_cache_db", "nvidia/GLCache", "fontconfig",
                             "typescript", "qtshadercache"})
        dirs.append(cache + QLatin1Char('/') + QLatin1String(name));
    // Qt names it qtshadercache-<arch>-<endianness>-<abi>
    const QStringList qtCaches = QDir(cache).entryList({QStringLiteral("qtshadercache-*")}, QDir::Dirs | QDir::NoSymLinks);
    for (const QString &dir : qtCaches)
        dirs.append(cache + QLatin1Char('/') + dir);
    Q_UNUSED(home);

    QStringList existing;
    for (const QString &dir : std::as_const(dirs)) {
        const QFileInfo info(dir);
        if (info.isDir() && !info.isSymLink())
            existing.append(dir);
    }
    return existing;
}

qint64 Cleaner::journalSize()
{
    QProcess p;
    p.start(QStringLiteral("journalctl"), {QStringLiteral("--disk-usage")});
    if (p.waitForFinished(5000) && p.exitCode() == 0) {
        // "Archived and active journals take up 12.9M in the file system."
        static const QRegularExpression re(QStringLiteral("take up ([0-9.]+)([KMGT]?)"));
        const auto m = re.match(QString::fromUtf8(p.readAllStandardOutput()));
        if (m.hasMatch()) {
            double value = m.captured(1).toDouble();
            for (const QChar unit : {QLatin1Char('K'), QLatin1Char('M'), QLatin1Char('G'), QLatin1Char('T')}) {
                value *= 1024.0;
                if (m.captured(2) == unit)
                    return qint64(value);
            }
            return qint64(m.captured(1).toDouble());
        }
    } else {
        p.kill();
    }
    return DiskScanner::totalSize(QStringLiteral("/var/log/journal"));
}

void Cleaner::estimate()
{
    if (m_estimating)
        return;
    m_estimating = true;
    emit stepsChanged();

    auto *watcher = new QFutureWatcher<QVariantMap>(this);
    connect(watcher, &QFutureWatcher<QVariantMap>::finished, this, [this, watcher] {
        const QVariantMap sizes = watcher->result();
        watcher->deleteLater();
        for (QVariant &v : m_steps) {
            QVariantMap step = v.toMap();
            const QString id = step.value(QStringLiteral("id")).toString();
            if (sizes.contains(id)) {
                step[QStringLiteral("size")] = sizes.value(id).toDouble();
                step[QStringLiteral("upTo")] = sizes.value(id + QStringLiteral("-upTo")).toBool();
            }
            v = step;
        }
        m_estimating = false;
        emit stepsChanged();
    });
    watcher->setFuture(QtConcurrent::run([] {
        QVariantMap sizes;
        qint64 cache = 0;
        for (const QString &dir : safeCacheDirs())
            cache += DiskScanner::totalSize(dir);
        sizes.insert(QStringLiteral("user-cache"), double(cache));

        // Upper bounds: how much of it is older than 7 days is not known
        sizes.insert(QStringLiteral("journal"), double(journalSize()));
        sizes.insert(QStringLiteral("journal-upTo"), true);
        return sizes;
    }));
}

QString Cleaner::titleFor(const QString &id) const
{
    for (const QVariant &v : m_steps) {
        const QVariantMap step = v.toMap();
        if (step.value(QStringLiteral("id")).toString() == id)
            return step.value(QStringLiteral("title")).toString();
    }
    if (id == QLatin1String("flatpak-user"))
        return tr("Runtimes Flatpak sem uso (usuário)");
    if (id == QLatin1String("pacman-cache"))
        return tr("Pacotes baixados");
    if (m_devTools) {
        DevCaches::Item item;
        const QString devId = id.startsWith(QLatin1String("dev:")) ? id.mid(4) : id;
        if (m_devTools->find(devId, &item))
            return item.title;
    }
    return id;
}

void Cleaner::setCurrentStep(const QString &id)
{
    m_currentStep = id.isEmpty() ? QString() : titleFor(id);
    emit currentStepChanged();
}

void Cleaner::addResult(const QString &id, qint64 freed, bool ok, const QString &message)
{
    m_results.append(QVariantMap {{QStringLiteral("id"), id}, {QStringLiteral("title"), titleFor(id)},
                                  {QStringLiteral("freed"), double(freed)}, {QStringLiteral("ok"), ok},
                                  {QStringLiteral("message"), message}});
    if (ok)
        m_totalFreed += freed;
    emit currentStepChanged();
}

void Cleaner::run(const QStringList &stepIds)
{
    if (m_running || stepIds.isEmpty())
        return;
    m_running = true;
    m_results.clear();
    m_totalFreed = 0;
    m_rootSteps.clear();
    m_cleanedDevIds.clear();
    QStringList userSteps;
    QList<DevCaches::Item> devItems;
    for (const QString &id : stepIds) {
        if (kRootSteps.contains(id)) {
            if (!m_rootSteps.contains(id))
                m_rootSteps.append(id);
        } else if (id.startsWith(QLatin1String("dev:"))) {
            DevCaches::Item item;
            if (!m_devTools || !m_devTools->find(id.mid(4), &item))
                continue;
            m_cleanedDevIds.append(item.id);
            if (item.action == DevCaches::Action::Root) {
                // pacman: a fixed step of the privileged helper
                if (kRootSteps.contains(item.rootStep) && !m_rootSteps.contains(item.rootStep))
                    m_rootSteps.append(item.rootStep);
            } else if (item.cleanable()) {
                devItems.append(item);
            }
        } else {
            userSteps.append(id);
        }
    }
    // Docker/Podman images before containers: images of stopped containers stay
    std::stable_sort(devItems.begin(), devItems.end(),
                     [](const DevCaches::Item &a, const DevCaches::Item &b) { return a.order < b.order; });
    m_doneSteps = 0;
    m_totalSteps = int(userSteps.size() + devItems.size() + m_rootSteps.size());
    emit runningChanged();
    runUserSteps(userSteps, devItems);
}

void Cleaner::stepDone(const QString &nextId)
{
    ++m_doneSteps;
    setCurrentStep(nextId);
}

void Cleaner::runUserSteps(const QStringList &ids, const QList<DevCaches::Item> &devItems)
{
    const bool cache = ids.contains(QStringLiteral("user-cache"));
    const bool flatpakUser = ids.contains(QStringLiteral("flatpak"))
        && QFileInfo::exists(QDir::homePath() + QStringLiteral("/.local/share/flatpak"));
    if (cache)
        setCurrentStep(QStringLiteral("user-cache"));
    else if (!devItems.isEmpty())
        setCurrentStep(QStringLiteral("dev:") + devItems.first().id);

    auto *watcher = new QFutureWatcher<QVariantList>(this);
    connect(watcher, &QFutureWatcher<QVariantList>::finished, this, [this, watcher] {
        const QVariantList results = watcher->result();
        watcher->deleteLater();
        for (const QVariant &v : results) {
            const QVariantMap r = v.toMap();
            addResult(r.value(QStringLiteral("id")).toString(), qint64(r.value(QStringLiteral("freed")).toDouble()),
                      r.value(QStringLiteral("ok")).toBool(), r.value(QStringLiteral("message")).toString());
        }
        runRootSteps();
    });
    QPointer<Cleaner> self(this);
    watcher->setFuture(QtConcurrent::run([self, cache, flatpakUser, devItems] {
        QVariantList results;
        auto next = [&self](const QString &id) {
            QMetaObject::invokeMethod(self.data(), [self, id] {
                if (self)
                    self->stepDone(id);
            }, Qt::QueuedConnection);
        };
        // Each result reaches the UI (and the Island) as soon as it is known
        auto post = [&self](const QVariantMap &r) {
            QMetaObject::invokeMethod(self.data(), [self, r] {
                if (self)
                    self->addResult(r.value(QStringLiteral("id")).toString(), qint64(r.value(QStringLiteral("freed")).toDouble()),
                                    r.value(QStringLiteral("ok")).toBool(), r.value(QStringLiteral("message")).toString());
            }, Qt::QueuedConnection);
        };
        if (cache) {
            qint64 freed = 0;
            int failures = 0;
            for (const QString &dir : safeCacheDirs()) {
                int f = 0;
                freed += removeDirectoryContents(dir, &f);
                failures += f;
            }
            post(QVariantMap {{QStringLiteral("id"), QStringLiteral("user-cache")},
                                        {QStringLiteral("freed"), double(freed)}, {QStringLiteral("ok"), true},
                                        {QStringLiteral("message"), failures > 0
                                             ? QCoreApplication::translate("Cleaner", "%1 itens em uso não puderam ser apagados").arg(failures)
                                             : QString()}});
            next(devItems.isEmpty() ? QString() : QStringLiteral("dev:") + devItems.first().id);
        }
        // Developer tools, one by one: each re-checks its paths and whether
        // something uses it right now
        const DevCaches::Env env = DevCaches::Env::system();
        for (int i = 0; i < devItems.size(); ++i) {
            const DevCaches::Item &item = devItems.at(i);
            const DevCaches::Result r = DevCaches::clean(item, env);
            post(QVariantMap {{QStringLiteral("id"), QStringLiteral("dev:") + item.id},
                                        {QStringLiteral("freed"), double(r.freed)}, {QStringLiteral("ok"), r.ok},
                                        {QStringLiteral("message"), r.message}});
            next(i + 1 < devItems.size() ? QStringLiteral("dev:") + devItems.at(i + 1).id : QString());
        }
        if (flatpakUser) {
            const QString dir = QDir::homePath() + QStringLiteral("/.local/share/flatpak");
            const qint64 before = DiskScanner::totalSize(dir);
            QProcess p;
            p.start(QStringLiteral("flatpak"), {QStringLiteral("uninstall"), QStringLiteral("--user"), QStringLiteral("--unused"),
                                                QStringLiteral("-y"), QStringLiteral("--noninteractive")});
            const bool ok = p.waitForFinished(10 * 60 * 1000) && p.exitCode() == 0;
            const qint64 after = DiskScanner::totalSize(dir);
            post(QVariantMap {{QStringLiteral("id"), QStringLiteral("flatpak-user")},
                                        {QStringLiteral("freed"), double(qMax<qint64>(0, before - after))},
                                        {QStringLiteral("ok"), ok}, {QStringLiteral("message"), QString()}});
        }
        return results;
    }));
}

void Cleaner::runRootSteps()
{
    if (m_rootSteps.isEmpty()) {
        finish();
        return;
    }
    setCurrentStep(m_rootSteps.first());
    m_output.clear();
    m_process = new QProcess(this);
    m_process->setProcessChannelMode(QProcess::MergedChannels);
    connect(m_process, &QProcess::readyRead, this, [this] {
        m_output += m_process->readAll();
        int newline;
        while ((newline = m_output.indexOf('\n')) >= 0) {
            const QByteArray line = m_output.left(newline).trimmed();
            m_output.remove(0, newline + 1);
            if (line.startsWith("@@STEP ")) {
                setCurrentStep(QString::fromUtf8(line.mid(7)));
            } else if (line.startsWith("@@FREED ")) {
                const QList<QByteArray> parts = line.mid(8).split(' ');
                if (parts.size() >= 2) {
                    const QString id = QString::fromUtf8(parts.at(0));
                    const bool ok = parts.value(2, "0") == "0";
                    const int status = parts.value(2, "0").toInt();
                    addResult(id, parts.at(1).toLongLong(), ok,
                              ok ? QString()
                                 : status == 75 ? tr("O pacman está instalando ou atualizando algo agora: tente depois")
                                                : tr("O comando terminou com erro"));
                    m_rootSteps.removeAll(id);
                    ++m_doneSteps;
                    emit currentStepChanged();
                }
            }
        }
    });
    connect(m_process, &QProcess::finished, this, [this](int exitCode, QProcess::ExitStatus) {
        m_output += m_process->readAll();
        // pkexec: 126 = dialog dismissed, 127 = not authorized
        QString message;
        if (exitCode == 126 || exitCode == 127)
            message = tr("Autenticação cancelada");
        else if (exitCode != 0)
            message = tr("Não foi possível executar a limpeza do sistema");
        for (const QString &id : std::as_const(m_rootSteps))
            addResult(id, 0, false, message.isEmpty() ? tr("Não executado") : message);
        m_rootSteps.clear();
        m_process->deleteLater();
        m_process = nullptr;
        finish();
    });

    // One pkexec call with fixed arguments = one password prompt
    QStringList args = m_rootSteps;
    if (qEnvironmentVariableIsSet("LINGMO_OPTIMIZER_NO_PKEXEC")) {
        m_process->start(helperPath(), args);
    } else {
        args.prepend(helperPath());
        m_process->start(QStringLiteral("pkexec"), args);
    }
}

void Cleaner::finish()
{
    setCurrentStep(QString());
    m_doneSteps = m_totalSteps;
    m_running = false;
    // Measure again only what was cleaned (the rest stays cached)
    if (m_devTools && !m_cleanedDevIds.isEmpty())
        m_devTools->refresh(m_cleanedDevIds);
    emit runningChanged();
    emit finished(m_results, double(m_totalFreed));
}
