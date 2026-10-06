#pragma once

#include <QByteArray>
#include <QString>
#include <QVector>

// Parsers for the /proc text files the optimizer reads. They take the file
// contents (so tests can feed sample text) and never touch the filesystem,
// except readProcFile() which does the reading.
namespace ProcParse {

// procfs files report st_size 0, so QTextStream::atEnd() is true right away:
// always read them with readAll(). Returns an empty array on error.
QByteArray readProcFile(const QString &path, qint64 maxSize = 4 * 1024 * 1024);

struct MemInfo {
    qint64 total = 0;          // all values in bytes
    qint64 free = 0;
    qint64 available = 0;
    qint64 buffers = 0;
    qint64 cached = 0;         // page cache ("Cached")
    qint64 sReclaimable = 0;
    qint64 shmem = 0;
    qint64 swapTotal = 0;
    qint64 swapFree = 0;
    bool valid() const { return total > 0; }
    // What people mean by "used": everything the kernel cannot hand out right now
    qint64 used() const { return available > 0 ? total - available : total - free - buffers - cached; }
    qint64 cache() const { return buffers + cached + sReclaimable - shmem; }
    qint64 swapUsed() const { return swapTotal - swapFree; }
};
MemInfo parseMeminfo(const QByteArray &text);

// One "cpu"/"cpuN" line of /proc/stat. total sums user, nice, system, idle,
// iowait, irq, softirq and steal (guest/guest_nice are already part of
// user/nice); idle counts idle + iowait.
struct CpuTimes {
    quint64 total = 0;
    quint64 idle = 0;
    quint64 busy() const { return total - idle; }
};
// Index 0 is the aggregate "cpu" line, then cpu0, cpu1...
QVector<CpuTimes> parseStat(const QByteArray &text);
// Busy fraction (0..1) between two samples of the same line
double cpuUsage(const CpuTimes &previous, const CpuTimes &current);

// /proc/PID/stat. comm is between the first '(' and the LAST ')' (it can hold
// spaces and parentheses).
struct PidStat {
    bool ok = false;
    int pid = 0;
    QByteArray comm;
    char state = '?';
    int ppid = 0;
    quint32 flags = 0;
    quint64 utime = 0;   // clock ticks
    quint64 stime = 0;
    quint64 startTime = 0;
    qint64 rssPages = 0;
    bool isKernelThread() const { return flags & 0x00200000; } // PF_KTHREAD
};
PidStat parsePidStat(const QByteArray &text);

// "Pss:" of /proc/PID/smaps_rollup in bytes, -1 when absent
qint64 parseSmapsRollupPss(const QByteArray &text);

// /proc/PID/cmdline: NUL-separated arguments
QStringList parseCmdline(const QByteArray &text);

// /proc/loadavg -> the three averages
QVector<double> parseLoadavg(const QByteArray &text);

} // namespace ProcParse
