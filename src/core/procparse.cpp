#include "procparse.h"

#include <QFile>
#include <QList>

namespace ProcParse {

QByteArray readProcFile(const QString &path, qint64 maxSize)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    // readAll() keeps reading until EOF, unlike anything based on size()
    QByteArray data = file.readAll();
    if (data.size() > maxSize)
        data.truncate(maxSize);
    return data;
}

static qint64 kbField(const QByteArray &line)
{
    // "MemTotal:       32768000 kB"
    const int colon = line.indexOf(':');
    if (colon < 0)
        return 0;
    const QList<QByteArray> parts = line.mid(colon + 1).simplified().split(' ');
    if (parts.isEmpty())
        return 0;
    bool ok = false;
    const qint64 value = parts.first().toLongLong(&ok);
    if (!ok)
        return 0;
    const bool kb = parts.size() > 1 && parts.at(1) == "kB";
    return kb ? value * 1024 : value;
}

MemInfo parseMeminfo(const QByteArray &text)
{
    MemInfo info;
    const QList<QByteArray> lines = text.split('\n');
    for (const QByteArray &line : lines) {
        if (line.startsWith("MemTotal:"))
            info.total = kbField(line);
        else if (line.startsWith("MemFree:"))
            info.free = kbField(line);
        else if (line.startsWith("MemAvailable:"))
            info.available = kbField(line);
        else if (line.startsWith("Buffers:"))
            info.buffers = kbField(line);
        else if (line.startsWith("Cached:"))
            info.cached = kbField(line);
        else if (line.startsWith("SReclaimable:"))
            info.sReclaimable = kbField(line);
        else if (line.startsWith("Shmem:"))
            info.shmem = kbField(line);
        else if (line.startsWith("SwapTotal:"))
            info.swapTotal = kbField(line);
        else if (line.startsWith("SwapFree:"))
            info.swapFree = kbField(line);
    }
    return info;
}

QVector<CpuTimes> parseStat(const QByteArray &text)
{
    QVector<CpuTimes> result;
    const QList<QByteArray> lines = text.split('\n');
    for (const QByteArray &line : lines) {
        if (!line.startsWith("cpu"))
            continue;
        const QList<QByteArray> parts = line.simplified().split(' ');
        if (parts.size() < 5)
            continue;
        // user nice system idle iowait irq softirq steal guest guest_nice
        quint64 fields[8] = {0, 0, 0, 0, 0, 0, 0, 0};
        for (int i = 1; i < parts.size() && i <= 8; ++i)
            fields[i - 1] = parts.at(i).toULongLong();
        CpuTimes times;
        for (quint64 f : fields)
            times.total += f;
        times.idle = fields[3] + fields[4];
        result.append(times);
    }
    return result;
}

double cpuUsage(const CpuTimes &previous, const CpuTimes &current)
{
    if (current.total <= previous.total)
        return 0.0;
    const double total = double(current.total - previous.total);
    const double idle = current.idle >= previous.idle ? double(current.idle - previous.idle) : 0.0;
    const double usage = (total - idle) / total;
    return qBound(0.0, usage, 1.0);
}

PidStat parsePidStat(const QByteArray &text)
{
    PidStat stat;
    const int open = text.indexOf('(');
    const int close = text.lastIndexOf(')');
    if (open < 1 || close < open)
        return stat;
    stat.pid = text.left(open).trimmed().toInt();
    stat.comm = text.mid(open + 1, close - open - 1);
    const QList<QByteArray> rest = text.mid(close + 1).simplified().split(' ');
    // rest[0] is field 3 (state); field N is rest[N - 3]
    auto field = [&rest](int n) -> QByteArray { return n - 3 < rest.size() ? rest.at(n - 3) : QByteArray(); };
    if (rest.size() < 22)
        return stat;
    stat.state = field(3).isEmpty() ? '?' : field(3).at(0);
    stat.ppid = field(4).toInt();
    stat.flags = field(9).toUInt();
    stat.utime = field(14).toULongLong();
    stat.stime = field(15).toULongLong();
    stat.startTime = field(22).toULongLong();
    stat.rssPages = field(24).toLongLong();
    stat.ok = stat.pid > 0;
    return stat;
}

qint64 parseSmapsRollupPss(const QByteArray &text)
{
    const QList<QByteArray> lines = text.split('\n');
    for (const QByteArray &line : lines) {
        if (line.startsWith("Pss:"))
            return kbField(line);
    }
    return -1;
}

QStringList parseCmdline(const QByteArray &text)
{
    QStringList args;
    const QList<QByteArray> parts = text.split('\0');
    for (const QByteArray &part : parts) {
        if (!part.isEmpty())
            args.append(QString::fromLocal8Bit(part));
    }
    return args;
}

QVector<double> parseLoadavg(const QByteArray &text)
{
    QVector<double> result;
    const QList<QByteArray> parts = text.simplified().split(' ');
    for (int i = 0; i < 3 && i < parts.size(); ++i)
        result.append(parts.at(i).toDouble());
    return result;
}

} // namespace ProcParse
