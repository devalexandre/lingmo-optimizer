#include "optimizer.h"
#include <QFile>
#include <QTextStream>
#include <QStorageInfo>
#include <QTimer>
#include <QDebug>

Optimizer::Optimizer(QObject *parent) : QObject(parent), m_lastTotalUser(0), m_lastTotalUserLow(0), m_lastTotalSys(0), m_lastTotalIdle(0) {
    QTimer *timer = new QTimer(this);
    connect(timer, &QTimer::timeout, this, &Optimizer::refreshStats);
    timer->start(2000);
    refreshStats();
}

double Optimizer::cpuUsage() const { return m_cpuUsage; }
double Optimizer::ramUsage() const { return m_ramUsage; }
double Optimizer::diskUsage() const { return m_diskUsage; }
bool Optimizer::isCleaning() const { return m_isCleaning; }

void Optimizer::refreshStats() {
    // RAM
    QFile meminfo("/proc/meminfo");
    if (meminfo.open(QIODevice::ReadOnly)) {
        QTextStream in(&meminfo);
        long long totalMem = 0, freeMem = 0, buffers = 0, cached = 0;
        while (!in.atEnd()) {
            QString line = in.readLine();
            if (line.startsWith("MemTotal:")) totalMem = line.section(' ', -2, -2).toLongLong();
            else if (line.startsWith("MemFree:")) freeMem = line.section(' ', -2, -2).toLongLong();
            else if (line.startsWith("Buffers:")) buffers = line.section(' ', -2, -2).toLongLong();
            else if (line.startsWith("Cached:")) cached = line.section(' ', -2, -2).toLongLong();
        }
        if (totalMem > 0) {
            long long usedMem = totalMem - freeMem - buffers - cached;
            m_ramUsage = (double)usedMem / totalMem;
        }
    }
    
    // CPU
    QFile stat("/proc/stat");
    if (stat.open(QIODevice::ReadOnly)) {
        QTextStream in(&stat);
        QString line = in.readLine();
        if (line.startsWith("cpu ")) {
            QStringList parts = line.split(' ', Qt::SkipEmptyParts);
            if (parts.size() > 4) {
                long long user = parts[1].toLongLong();
                long long userLow = parts[2].toLongLong();
                long long sys = parts[3].toLongLong();
                long long idle = parts[4].toLongLong();
                if (m_lastTotalUser > 0) {
                    long long diffUser = user - m_lastTotalUser;
                    long long diffUserLow = userLow - m_lastTotalUserLow;
                    long long diffSys = sys - m_lastTotalSys;
                    long long diffIdle = idle - m_lastTotalIdle;
                    long long totalDiff = diffUser + diffUserLow + diffSys + diffIdle;
                    if (totalDiff > 0) m_cpuUsage = (double)(diffUser + diffUserLow + diffSys) / totalDiff;
                }
                m_lastTotalUser = user; m_lastTotalUserLow = userLow;
                m_lastTotalSys = sys; m_lastTotalIdle = idle;
            }
        }
    }
    
    // Disk (Root)
    QStorageInfo root("/");
    if (root.isValid() && root.isReady()) {
        m_diskUsage = (double)(root.bytesTotal() - root.bytesAvailable()) / root.bytesTotal();
    }
    
    emit statsChanged();
}

void Optimizer::startCleanup() {
    if (m_isCleaning) return;
    m_isCleaning = true;
    emit isCleaningChanged();
    
    // Run cleanup commands asynchronously
    // Using pkexec to clean system caches
    QStringList args;
    args << "-c" << "rm -rf ~/.cache/* && pkexec sh -c 'pacman -Sc --noconfirm && flatpak uninstall --unused -y && journalctl --vacuum-time=3d'";
    
    QProcess *process = new QProcess(this);
    process->setProgram("bash");
    process->setArguments(args);
    connect(process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this, [this, process](int exitCode) {
        m_isCleaning = false;
        emit isCleaningChanged();
        emit cleanupFinished(exitCode == 0 ? tr("Limpeza concluída com sucesso!") : tr("Concluído (alguns arquivos não puderam ser limpos)."));
        refreshStats();
        process->deleteLater();
    });
    process->start();
}
