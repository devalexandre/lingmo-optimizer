#include "cleaner.h"

#include "core/diskusage.h"
#include "core/format.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QtConcurrent>

#ifndef HELPER_PATH
#define HELPER_PATH "/usr/lib/lingmo-optimizer/clean-system"
#endif

static const QStringList kRootSteps = {QStringLiteral("pacman-cache"), QStringLiteral("journal"), QStringLiteral("flatpak")};

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
    m_steps.append(step(QStringLiteral("user-cache"), tr("Caches de programas"),
                        tr("Miniaturas, pip, npm, yarn, Go, shaders de vídeo e fontes. São recriados quando preciso."), false));
    m_steps.append(step(QStringLiteral("pacman-cache"), tr("Pacotes baixados"),
                        tr("Versões antigas e pacotes desinstalados no cache do pacman (mantém a versão atual)."), true));
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
    for (const char *name : {"thumbnails", "pip", "yarn", "go-build", "mesa_shader_cache", "mesa_shader_cache_db",
                             "nvidia/GLCache", "fontconfig", "yay", "paru", "pnpm", "typescript", "node-gyp",
                             "qtshadercache"})
        dirs.append(cache + QLatin1Char('/') + QLatin1String(name));
    // Qt names it qtshadercache-<arch>-<endianness>-<abi>
    const QStringList qtCaches = QDir(cache).entryList({QStringLiteral("qtshadercache-*")}, QDir::Dirs | QDir::NoSymLinks);
    for (const QString &dir : qtCaches)
        dirs.append(cache + QLatin1Char('/') + dir);
    dirs.append(home + QStringLiteral("/.npm/_cacache"));

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

        // paccache can tell exactly what it would remove (dry run, no root)
        const QString paccache = QStandardPaths::findExecutable(QStringLiteral("paccache"));
        qint64 pacman = -1;
        if (!paccache.isEmpty()) {
            pacman = 0;
            for (const QStringList &args : {QStringList {QStringLiteral("-dk1"), QStringLiteral("--nocolor")},
                                            QStringList {QStringLiteral("-duk0"), QStringLiteral("--nocolor")}}) {
                QProcess p;
                p.start(paccache, args);
                if (!p.waitForFinished(20000))
                    continue;
                const QString out = QString::fromUtf8(p.readAllStandardOutput() + p.readAllStandardError());
                const QRegularExpression re(QStringLiteral("disk space saved: ([0-9.]+) ([KMGT]?i?B)"));
                const auto m = re.match(out);
                if (m.hasMatch()) {
                    const double value = m.captured(1).toDouble();
                    const QString unit = m.captured(2);
                    double mult = 1;
                    if (unit.startsWith(QLatin1Char('K'))) mult = 1024.0;
                    else if (unit.startsWith(QLatin1Char('M'))) mult = 1024.0 * 1024;
                    else if (unit.startsWith(QLatin1Char('G'))) mult = 1024.0 * 1024 * 1024;
                    else if (unit.startsWith(QLatin1Char('T'))) mult = 1024.0 * 1024 * 1024 * 1024;
                    pacman += qint64(value * mult);
                }
            }
        }
        if (pacman < 0) {
            sizes.insert(QStringLiteral("pacman-cache-upTo"), true);
            pacman = DiskScanner::totalSize(QStringLiteral("/var/cache/pacman/pkg"));
        }
        sizes.insert(QStringLiteral("pacman-cache"), double(pacman));
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
}

void Cleaner::run(const QStringList &stepIds)
{
    if (m_running || stepIds.isEmpty())
        return;
    m_running = true;
    m_results.clear();
    m_totalFreed = 0;
    m_rootSteps.clear();
    for (const QString &id : stepIds) {
        if (kRootSteps.contains(id))
            m_rootSteps.append(id);
    }
    emit runningChanged();
    runUserSteps(stepIds);
}

void Cleaner::runUserSteps(const QStringList &ids)
{
    const bool cache = ids.contains(QStringLiteral("user-cache"));
    const bool flatpakUser = ids.contains(QStringLiteral("flatpak"))
        && QFileInfo::exists(QDir::homePath() + QStringLiteral("/.local/share/flatpak"));
    if (cache)
        setCurrentStep(QStringLiteral("user-cache"));

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
    watcher->setFuture(QtConcurrent::run([cache, flatpakUser] {
        QVariantList results;
        if (cache) {
            qint64 freed = 0;
            int failures = 0;
            for (const QString &dir : safeCacheDirs()) {
                int f = 0;
                freed += removeDirectoryContents(dir, &f);
                failures += f;
            }
            results.append(QVariantMap {{QStringLiteral("id"), QStringLiteral("user-cache")},
                                        {QStringLiteral("freed"), double(freed)}, {QStringLiteral("ok"), true},
                                        {QStringLiteral("message"), failures > 0
                                             ? QCoreApplication::translate("Cleaner", "%1 itens em uso não puderam ser apagados").arg(failures)
                                             : QString()}});
        }
        if (flatpakUser) {
            const QString dir = QDir::homePath() + QStringLiteral("/.local/share/flatpak");
            const qint64 before = DiskScanner::totalSize(dir);
            QProcess p;
            p.start(QStringLiteral("flatpak"), {QStringLiteral("uninstall"), QStringLiteral("--user"), QStringLiteral("--unused"),
                                                QStringLiteral("-y"), QStringLiteral("--noninteractive")});
            const bool ok = p.waitForFinished(10 * 60 * 1000) && p.exitCode() == 0;
            const qint64 after = DiskScanner::totalSize(dir);
            results.append(QVariantMap {{QStringLiteral("id"), QStringLiteral("flatpak-user")},
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
                    addResult(id, parts.at(1).toLongLong(), ok,
                              ok ? QString() : tr("O comando terminou com erro"));
                    m_rootSteps.removeAll(id);
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
    m_running = false;
    emit runningChanged();
    emit finished(m_results, double(m_totalFreed));
}
