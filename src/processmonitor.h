#pragma once

#include <QAbstractListModel>
#include <QFutureWatcher>
#include <QHash>
#include <QObject>
#include <QTimer>
#include <QVariantList>
#include <QVector>

#include "desktopapps.h"

// One running process, as sampled from /proc
struct ProcessSample {
    int pid = 0;
    quint64 startTime = 0;
    uint uid = 0;
    bool kernelThread = false;
    QString executable;      // base name used for grouping ("firefox")
    QStringList args;
    qint64 memory = 0;       // PSS (smaps_rollup) or RSS, bytes
    quint64 cpuTicks = 0;    // utime + stime
    double cpu = 0.0;        // share of the whole machine, 0..1
    bool killable = false;
};

// Processes grouped by application
struct AppGroup {
    QString key;
    QString name;
    QString icon;
    qint64 memory = 0;
    double cpu = 0.0;
    bool killable = false;   // every process may be ended
    bool own = false;        // all processes belong to the user
    QVector<ProcessSample> processes;
};

class AppGroupModel : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)

public:
    enum SortKey { ByMemory, ByCpu };
    enum Roles {
        KeyRole = Qt::UserRole + 1,
        NameRole,
        IconRole,
        MemoryRole,
        CpuRole,
        CountRole,
        KillableRole,
        OwnRole,
        ProcessesRole
    };

    AppGroupModel(SortKey sortKey, int limit, QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    // Updates rows in place (moves/inserts/removes) so the view keeps its
    // scroll position and expanded items between refreshes
    void setGroups(QVector<AppGroup> groups);
    const AppGroup *group(const QString &key) const;

signals:
    void countChanged();

private:
    SortKey m_sortKey;
    int m_limit;
    QVector<AppGroup> m_rows;
};

class ProcessMonitor : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool active READ active WRITE setActive NOTIFY activeChanged)
    Q_PROPERTY(AppGroupModel *memoryModel READ memoryModel CONSTANT)
    Q_PROPERTY(AppGroupModel *cpuModel READ cpuModel CONSTANT)
    Q_PROPERTY(int processCount READ processCount NOTIFY updated)
    Q_PROPERTY(bool ready READ ready NOTIFY updated)

public:
    explicit ProcessMonitor(QObject *parent = nullptr);
    ~ProcessMonitor() override;

    bool active() const { return m_active; }
    void setActive(bool active);
    AppGroupModel *memoryModel() const { return m_memoryModel; }
    AppGroupModel *cpuModel() const { return m_cpuModel; }
    int processCount() const { return m_processCount; }
    bool ready() const { return m_ready; }

    // SIGTERM to every process of the app; after 3 s, appStillRunning() is
    // emitted if any is left so the UI can offer forceKillApp()
    Q_INVOKABLE int terminateApp(const QString &key);
    Q_INVOKABLE int forceKillApp(const QString &key);
    Q_INVOKABLE bool terminateProcess(int pid);
    Q_INVOKABLE bool forceKillProcess(int pid);
    Q_INVOKABLE void refresh();

signals:
    void activeChanged();
    void updated();
    void appStillRunning(const QString &key, const QString &name, int remaining);
    void processStillRunning(int pid, const QString &name);

private:
    struct Known {
        quint64 startTime = 0;
        QString executable;
        QStringList args;
        uint uid = 0;
        bool kernelThread = false;
        quint64 cpuTicks = 0;
    };
    void sampleFinished();
    QVector<AppGroup> group(const QVector<ProcessSample> &samples) const;
    bool sendSignal(int pid, int signal);
    bool isAlive(int pid, quint64 startTime) const;

    bool m_active = false;
    bool m_ready = false;
    int m_processCount = 0;
    QTimer m_timer;
    AppGroupModel *m_memoryModel;
    AppGroupModel *m_cpuModel;
    DesktopApps m_apps;
    QHash<int, Known> m_known;          // last sample, by pid
    qint64 m_lastSampleMs = 0;
    QFutureWatcher<QVector<ProcessSample>> m_watcher;
    QHash<QString, QVector<QPair<int, quint64>>> m_lastGroups; // key -> (pid, start)
};
