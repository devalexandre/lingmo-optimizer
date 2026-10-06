#include "processmonitor.h"

#include "core/policy.h"
#include "core/procparse.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QIcon>
#include <QtConcurrent>

#include <algorithm>

#include <signal.h>
#include <sys/stat.h>
#include <unistd.h>

using namespace ProcParse;

// ---------------------------------------------------------------- model

AppGroupModel::AppGroupModel(SortKey sortKey, int limit, QObject *parent)
    : QAbstractListModel(parent)
    , m_sortKey(sortKey)
    , m_limit(limit)
{
}

int AppGroupModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : int(m_rows.size());
}

static QVariantList processList(const AppGroup &group)
{
    QVariantList list;
    for (const ProcessSample &p : group.processes) {
        QString command = p.args.join(QLatin1Char(' '));
        if (command.isEmpty())
            command = p.executable;
        if (command.size() > 160)
            command = command.left(157) + QStringLiteral("...");
        list.append(QVariantMap {
            {QStringLiteral("pid"), p.pid},
            {QStringLiteral("name"), p.executable},
            {QStringLiteral("command"), command},
            {QStringLiteral("memory"), double(p.memory)},
            {QStringLiteral("cpu"), p.cpu},
            {QStringLiteral("killable"), p.killable},
        });
    }
    return list;
}

QVariant AppGroupModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= m_rows.size())
        return {};
    const AppGroup &g = m_rows.at(index.row());
    switch (role) {
    case KeyRole: return g.key;
    case NameRole: return g.name;
    case IconRole: return g.icon;
    case MemoryRole: return double(g.memory);
    case CpuRole: return g.cpu;
    case CountRole: return int(g.processes.size());
    case KillableRole: return g.killable;
    case OwnRole: return g.own;
    case ProcessesRole: return processList(g);
    }
    return {};
}

QHash<int, QByteArray> AppGroupModel::roleNames() const
{
    return {
        {KeyRole, "key"}, {NameRole, "name"}, {IconRole, "iconName"}, {MemoryRole, "memory"},
        {CpuRole, "cpu"}, {CountRole, "processCount"}, {KillableRole, "killable"}, {OwnRole, "own"},
        {ProcessesRole, "processes"},
    };
}

void AppGroupModel::setGroups(QVector<AppGroup> groups)
{
    const SortKey key = m_sortKey;
    auto metric = [key](const auto &x) { return key == ByMemory ? double(x.memory) : x.cpu; };
    for (AppGroup &g : groups) {
        std::sort(g.processes.begin(), g.processes.end(),
                  [&metric](const ProcessSample &a, const ProcessSample &b) { return metric(a) > metric(b); });
    }
    std::sort(groups.begin(), groups.end(), [&metric](const AppGroup &a, const AppGroup &b) {
        const double ma = metric(a), mb = metric(b);
        if (ma != mb)
            return ma > mb;
        return a.memory > b.memory;
    });
    if (m_limit > 0 && groups.size() > m_limit)
        groups.resize(m_limit);

    const int oldCount = int(m_rows.size());
    QSet<QString> newKeys;
    for (const AppGroup &g : groups)
        newKeys.insert(g.key);

    for (int i = int(m_rows.size()) - 1; i >= 0; --i) {
        if (!newKeys.contains(m_rows.at(i).key)) {
            beginRemoveRows(QModelIndex(), i, i);
            m_rows.removeAt(i);
            endRemoveRows();
        }
    }
    for (int i = 0; i < groups.size(); ++i) {
        int found = -1;
        for (int j = i; j < m_rows.size(); ++j) {
            if (m_rows.at(j).key == groups.at(i).key) {
                found = j;
                break;
            }
        }
        if (found < 0) {
            beginInsertRows(QModelIndex(), i, i);
            m_rows.insert(i, groups.at(i));
            endInsertRows();
            continue;
        }
        if (found != i) {
            beginMoveRows(QModelIndex(), found, found, QModelIndex(), i);
            m_rows.move(found, i);
            endMoveRows();
        }
        m_rows[i] = groups.at(i);
        emit dataChanged(index(i), index(i));
    }
    if (oldCount != m_rows.size())
        emit countChanged();
}

const AppGroup *AppGroupModel::group(const QString &key) const
{
    for (const AppGroup &g : m_rows) {
        if (g.key == key)
            return &g;
    }
    return nullptr;
}

// ---------------------------------------------------------------- sampling

namespace {

struct KnownInfo {
    quint64 startTime;
    QString executable;
    QStringList args;
};

QString groupingName(int pid, const QStringList &args, const QByteArray &comm)
{
    // The executable's real name when we may read it (our own processes)
    QString exe = QFileInfo(QStringLiteral("/proc/%1/exe").arg(pid)).symLinkTarget();
    exe.remove(QStringLiteral(" (deleted)"));
    QString base = QFileInfo(exe).fileName();
    if (base.isEmpty() && !args.isEmpty())
        base = QFileInfo(args.first().section(QLatin1Char(' '), 0, 0)).fileName();
    if (base.isEmpty())
        base = QString::fromLocal8Bit(comm);

    // Scripts: "python3 /usr/bin/foo" is "foo"
    static const QStringList interpreters = {QStringLiteral("python"), QStringLiteral("perl"), QStringLiteral("ruby"),
                                             QStringLiteral("node"), QStringLiteral("bash"), QStringLiteral("sh"),
                                             QStringLiteral("gjs"), QStringLiteral("lua")};
    for (const QString &interp : interpreters) {
        if (base == interp || (base.startsWith(interp) && base.mid(interp.size()).contains(QRegularExpression(QStringLiteral("^[0-9.]+$"))))) {
            for (int i = 1; i < args.size(); ++i) {
                const QString &arg = args.at(i);
                if (arg.startsWith(QLatin1Char('-')))
                    continue;
                if (arg.contains(QLatin1Char('/')) || arg.endsWith(QStringLiteral(".py")) || arg.endsWith(QStringLiteral(".js")))
                    return QFileInfo(arg).fileName();
                break;
            }
            break;
        }
    }
    return base;
}

QVector<ProcessSample> collectProcesses(QHash<int, KnownInfo> known)
{
    QVector<ProcessSample> samples;
    const uint myUid = ::getuid();
    const int myPid = int(QCoreApplication::applicationPid());
    const long pageSize = ::sysconf(_SC_PAGESIZE);

    const QDir proc(QStringLiteral("/proc"));
    const QStringList entries = proc.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QString &entry : entries) {
        bool numeric = false;
        const int pid = entry.toInt(&numeric);
        if (!numeric)
            continue;
        const QString base = QStringLiteral("/proc/") + entry;
        const PidStat stat = parsePidStat(readProcFile(base + QStringLiteral("/stat")));
        if (!stat.ok || stat.state == 'Z')
            continue;

        struct stat st;
        if (::stat(QFile::encodeName(base).constData(), &st) != 0)
            continue;

        ProcessSample s;
        s.pid = pid;
        s.startTime = stat.startTime;
        s.uid = st.st_uid;
        s.kernelThread = stat.isKernelThread() || stat.ppid == 2 || pid == 2;
        s.cpuTicks = stat.utime + stat.stime;

        if (s.kernelThread) {
            s.executable = QString::fromLocal8Bit(stat.comm);
        } else {
            const auto it = known.constFind(pid);
            if (it != known.constEnd() && it->startTime == stat.startTime) {
                s.executable = it->executable;
                s.args = it->args;
            } else {
                s.args = parseCmdline(readProcFile(base + QStringLiteral("/cmdline"), 64 * 1024));
                s.executable = groupingName(pid, s.args, stat.comm);
            }
            // PSS splits shared pages between processes: the honest figure.
            // Only our own processes are readable; RSS for the rest.
            const qint64 pss = s.uid == myUid ? parseSmapsRollupPss(readProcFile(base + QStringLiteral("/smaps_rollup")))
                                              : -1;
            s.memory = pss >= 0 ? pss : stat.rssPages * pageSize;
        }
        s.killable = Policy::mayTerminate(s.executable, s.args, s.uid, myUid, pid, myPid, s.kernelThread);
        samples.append(s);
    }
    return samples;
}

} // namespace

// ---------------------------------------------------------------- monitor

ProcessMonitor::ProcessMonitor(QObject *parent)
    : QObject(parent)
    , m_memoryModel(new AppGroupModel(AppGroupModel::ByMemory, 0, this))
    , m_cpuModel(new AppGroupModel(AppGroupModel::ByCpu, 30, this))
{
    m_timer.setInterval(2500);
    connect(&m_timer, &QTimer::timeout, this, &ProcessMonitor::refresh);
    connect(&m_watcher, &QFutureWatcher<QVector<ProcessSample>>::finished, this, &ProcessMonitor::sampleFinished);
}

ProcessMonitor::~ProcessMonitor()
{
    m_watcher.waitForFinished();
}

void ProcessMonitor::setActive(bool active)
{
    if (m_active == active)
        return;
    m_active = active;
    if (active) {
        if (m_apps.isEmpty())
            m_apps.load();
        refresh();
        m_timer.start();
    } else {
        m_timer.stop();
    }
    emit activeChanged();
}

void ProcessMonitor::refresh()
{
    if (m_watcher.isRunning())
        return;
    QHash<int, KnownInfo> known;
    for (auto it = m_known.constBegin(); it != m_known.constEnd(); ++it)
        known.insert(it.key(), KnownInfo {it->startTime, it->executable, it->args});
    m_watcher.setFuture(QtConcurrent::run(collectProcesses, known));
}

void ProcessMonitor::sampleFinished()
{
    QVector<ProcessSample> samples = m_watcher.result();
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    const double elapsed = m_lastSampleMs > 0 ? (now - m_lastSampleMs) / 1000.0 : 0.0;
    const double ticksPerSecond = double(::sysconf(_SC_CLK_TCK));
    const double cpus = double(qMax(1L, ::sysconf(_SC_NPROCESSORS_ONLN)));

    QHash<int, Known> known;
    for (ProcessSample &s : samples) {
        const auto it = m_known.constFind(s.pid);
        if (elapsed > 0.0 && it != m_known.constEnd() && it->startTime == s.startTime && s.cpuTicks >= it->cpuTicks) {
            const double seconds = double(s.cpuTicks - it->cpuTicks) / ticksPerSecond;
            s.cpu = qBound(0.0, seconds / (elapsed * cpus), 1.0);
        }
        known.insert(s.pid, Known {s.startTime, s.executable, s.args, s.uid, s.kernelThread, s.cpuTicks});
    }
    m_known = known;
    m_lastSampleMs = now;
    m_processCount = int(samples.size());

    const QVector<AppGroup> groups = group(samples);
    m_lastGroups.clear();
    for (const AppGroup &g : groups) {
        QVector<QPair<int, quint64>> pids;
        for (const ProcessSample &p : g.processes)
            pids.append({p.pid, p.startTime});
        m_lastGroups.insert(g.key, pids);
    }

    // Memory: without kernel threads (they own no user memory)
    QVector<AppGroup> memoryGroups;
    for (const AppGroup &g : groups) {
        if (g.memory > 0)
            memoryGroups.append(g);
    }
    m_memoryModel->setGroups(memoryGroups);
    // CPU: the first sample has no deltas yet
    if (elapsed > 0.0)
        m_cpuModel->setGroups(groups);
    m_ready = elapsed > 0.0;
    emit updated();
}

QVector<AppGroup> ProcessMonitor::group(const QVector<ProcessSample> &samples) const
{
    const uint myUid = ::getuid();
    QHash<QString, AppGroup> groups;
    for (const ProcessSample &s : samples) {
        const QString key = s.kernelThread ? QStringLiteral("@kernel") : s.executable;
        AppGroup &g = groups[key];
        if (g.key.isEmpty()) {
            g.key = key;
            g.killable = true;
            g.own = true;
            if (s.kernelThread) {
                g.name = tr("Kernel do Linux");
                g.icon = QStringLiteral("preferences-system");
            } else {
                const DesktopApps::App app = m_apps.find(s.executable);
                g.name = app.name.isEmpty() ? s.executable : app.name;
                if (!app.icon.isEmpty())
                    g.icon = app.icon;
                else if (QIcon::hasThemeIcon(s.executable.toLower()))
                    g.icon = s.executable.toLower();
                else
                    g.icon = QStringLiteral("application-x-executable");
            }
        }
        g.memory += s.memory;
        g.cpu += s.cpu;
        g.killable = g.killable && s.killable;
        g.own = g.own && s.uid == myUid;
        g.processes.append(s);
    }
    return groups.values().toVector();
}

bool ProcessMonitor::isAlive(int pid, quint64 startTime) const
{
    const PidStat stat = parsePidStat(readProcFile(QStringLiteral("/proc/%1/stat").arg(pid)));
    return stat.ok && stat.startTime == startTime && stat.state != 'Z';
}

bool ProcessMonitor::sendSignal(int pid, int signal)
{
    // Re-check right before signalling: the pid must still be the process we
    // showed (same start time) and still allowed by the policy
    const auto it = m_known.constFind(pid);
    if (it == m_known.constEnd() || !isAlive(pid, it->startTime))
        return false;
    if (!Policy::mayTerminate(it->executable, it->args, it->uid, ::getuid(), pid,
                              int(QCoreApplication::applicationPid()), it->kernelThread))
        return false;
    return ::kill(pid, signal) == 0;
}

int ProcessMonitor::terminateApp(const QString &key)
{
    const auto pids = m_lastGroups.value(key);
    int sent = 0;
    for (const auto &p : pids) {
        if (sendSignal(p.first, SIGTERM))
            ++sent;
    }
    QString name = key;
    if (const AppGroup *g = m_memoryModel->group(key))
        name = g->name;
    else if (const AppGroup *g = m_cpuModel->group(key))
        name = g->name;
    if (sent > 0) {
        QTimer::singleShot(3000, this, [this, key, name, pids] {
            int remaining = 0;
            for (const auto &p : pids) {
                if (isAlive(p.first, p.second))
                    ++remaining;
            }
            if (remaining > 0)
                emit appStillRunning(key, name, remaining);
            refresh();
        });
    }
    refresh();
    return sent;
}

int ProcessMonitor::forceKillApp(const QString &key)
{
    int sent = 0;
    for (const auto &p : m_lastGroups.value(key)) {
        if (sendSignal(p.first, SIGKILL))
            ++sent;
    }
    QTimer::singleShot(500, this, &ProcessMonitor::refresh);
    return sent;
}

bool ProcessMonitor::terminateProcess(int pid)
{
    if (!sendSignal(pid, SIGTERM))
        return false;
    const quint64 start = m_known.value(pid).startTime;
    const QString name = m_known.value(pid).executable;
    QTimer::singleShot(3000, this, [this, pid, start, name] {
        if (isAlive(pid, start))
            emit processStillRunning(pid, name);
        refresh();
    });
    return true;
}

bool ProcessMonitor::forceKillProcess(int pid)
{
    const bool ok = sendSignal(pid, SIGKILL);
    QTimer::singleShot(500, this, &ProcessMonitor::refresh);
    return ok;
}
