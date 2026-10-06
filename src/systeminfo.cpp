#include "systeminfo.h"

#include "core/format.h"

#include <QDir>
#include <QFile>
#include <QSet>
#include <QStorageInfo>
#include <QTimer>

using namespace ProcParse;

static QByteArray readSys(const QString &path)
{
    return readProcFile(path, 64 * 1024).trimmed();
}

SystemInfo::SystemInfo(QObject *parent)
    : QObject(parent)
{
    readStaticCpuInfo();
    auto *timer = new QTimer(this);
    connect(timer, &QTimer::timeout, this, &SystemInfo::refresh);
    timer->start(2000);
    refresh();
}

void SystemInfo::readStaticCpuInfo()
{
    const QList<QByteArray> lines = readProcFile(QStringLiteral("/proc/cpuinfo")).split('\n');
    QSet<QByteArray> cores;
    QByteArray physicalId, coreId;
    for (const QByteArray &line : lines) {
        const int colon = line.indexOf(':');
        if (colon < 0)
            continue;
        const QByteArray key = line.left(colon).trimmed();
        const QByteArray value = line.mid(colon + 1).trimmed();
        if (key == "processor") {
            ++m_threads;
        } else if (key == "model name" && m_cpuModel.isEmpty()) {
            m_cpuModel = QString::fromUtf8(value).simplified();
        } else if (key == "physical id") {
            physicalId = value;
        } else if (key == "core id") {
            coreId = value;
            cores.insert(physicalId + ':' + coreId);
        }
    }
    m_physicalCores = cores.isEmpty() ? m_threads : int(cores.size());
    if (m_cpuModel.isEmpty())
        m_cpuModel = tr("Processador");

    const QByteArray maxKhz = readSys(QStringLiteral("/sys/devices/system/cpu/cpu0/cpufreq/cpuinfo_max_freq"));
    m_maxFrequency = maxKhz.toDouble() / 1000.0;

    // Temperature: a CPU hwmon driver first, then a thermal zone
    const QDir hwmon(QStringLiteral("/sys/class/hwmon"));
    const QStringList cpuDrivers = {QStringLiteral("k10temp"), QStringLiteral("coretemp"), QStringLiteral("zenpower"),
                                    QStringLiteral("cpu_thermal"), QStringLiteral("acpitz")};
    for (const QString &driver : cpuDrivers) {
        if (!m_temperatureInput.isEmpty())
            break;
        for (const QString &entry : hwmon.entryList(QDir::Dirs | QDir::NoDotAndDotDot | QDir::System)) {
            const QString base = hwmon.filePath(entry);
            if (QString::fromUtf8(readSys(base + QStringLiteral("/name"))) != driver)
                continue;
            // Prefer the package/die sensor when the driver labels them
            QString chosen;
            for (int i = 1; i <= 16; ++i) {
                const QString input = base + QStringLiteral("/temp%1_input").arg(i);
                if (!QFile::exists(input))
                    continue;
                const QByteArray label = readSys(base + QStringLiteral("/temp%1_label").arg(i));
                if (chosen.isEmpty() || label == "Tctl" || label == "Tdie" || label.startsWith("Package"))
                    chosen = input;
                if (label == "Tdie" || label.startsWith("Package"))
                    break;
            }
            if (!chosen.isEmpty()) {
                m_temperatureInput = chosen;
                break;
            }
        }
    }
    if (m_temperatureInput.isEmpty()) {
        const QDir thermal(QStringLiteral("/sys/class/thermal"));
        for (const QString &zone : thermal.entryList({QStringLiteral("thermal_zone*")}, QDir::Dirs | QDir::System)) {
            const QByteArray type = readSys(thermal.filePath(zone) + QStringLiteral("/type"));
            if (type == "x86_pkg_temp" || type == "cpu-thermal" || type == "acpitz") {
                m_temperatureInput = thermal.filePath(zone) + QStringLiteral("/temp");
                break;
            }
        }
    }
}

double SystemInfo::readFrequency() const
{
    double sum = 0.0;
    int count = 0;
    for (int i = 0; i < m_threads; ++i) {
        const QByteArray khz = readSys(QStringLiteral("/sys/devices/system/cpu/cpu%1/cpufreq/scaling_cur_freq").arg(i));
        if (khz.isEmpty())
            continue;
        sum += khz.toDouble() / 1000.0;
        ++count;
    }
    if (count > 0)
        return sum / count;
    // No cpufreq (VMs): /proc/cpuinfo has "cpu MHz"
    const QList<QByteArray> lines = readProcFile(QStringLiteral("/proc/cpuinfo")).split('\n');
    for (const QByteArray &line : lines) {
        if (line.startsWith("cpu MHz")) {
            sum += line.mid(line.indexOf(':') + 1).trimmed().toDouble();
            ++count;
        }
    }
    return count > 0 ? sum / count : 0.0;
}

double SystemInfo::readTemperature() const
{
    if (m_temperatureInput.isEmpty())
        return -1.0;
    bool ok = false;
    const double milli = readSys(m_temperatureInput).toDouble(&ok);
    return ok ? milli / 1000.0 : -1.0;
}

void SystemInfo::readZram()
{
    m_zramOriginal = m_zramCompressed = m_zramMemUsed = 0;
    const QDir block(QStringLiteral("/sys/block"));
    for (const QString &dev : block.entryList({QStringLiteral("zram*")}, QDir::Dirs | QDir::System)) {
        // mm_stat: orig_data_size compr_data_size mem_used_total ...
        const QList<QByteArray> fields = readSys(block.filePath(dev) + QStringLiteral("/mm_stat")).simplified().split(' ');
        if (fields.size() < 3)
            continue;
        m_zramOriginal += fields.at(0).toLongLong();
        m_zramCompressed += fields.at(1).toLongLong();
        m_zramMemUsed += fields.at(2).toLongLong();
    }
}

void SystemInfo::refresh()
{
    // Memory (procfs files have size 0: readProcFile uses readAll())
    const MemInfo mem = parseMeminfo(readProcFile(QStringLiteral("/proc/meminfo")));
    if (mem.valid())
        m_mem = mem;
    readZram();

    // CPU: aggregate line + one per core
    const QVector<CpuTimes> cpu = parseStat(readProcFile(QStringLiteral("/proc/stat")));
    if (!cpu.isEmpty()) {
        if (m_lastCpu.size() == cpu.size()) {
            m_cpuUsage = ProcParse::cpuUsage(m_lastCpu.at(0), cpu.at(0));
            QVariantList cores;
            for (int i = 1; i < cpu.size(); ++i)
                cores.append(ProcParse::cpuUsage(m_lastCpu.at(i), cpu.at(i)));
            m_coreUsages = cores;
        } else {
            // First sample: usage since boot, so the donut is not empty for 2 s
            CpuTimes zero;
            m_cpuUsage = ProcParse::cpuUsage(zero, cpu.at(0));
            QVariantList cores;
            for (int i = 1; i < cpu.size(); ++i)
                cores.append(ProcParse::cpuUsage(zero, cpu.at(i)));
            m_coreUsages = cores;
        }
        m_lastCpu = cpu;
    }
    m_frequency = readFrequency();
    m_temperature = readTemperature();

    QVariantList load;
    for (double value : parseLoadavg(readProcFile(QStringLiteral("/proc/loadavg"))))
        load.append(value);
    m_loadAverage = load;
    const QByteArray up = readProcFile(QStringLiteral("/proc/uptime")).simplified();
    m_uptime = Format::duration(qint64(up.split(' ').value(0).toDouble()));

    // Root filesystem, as df shows it (used = total - free)
    QStorageInfo root(QStringLiteral("/"));
    root.refresh();
    if (root.isValid() && root.isReady() && root.bytesTotal() > 0) {
        m_diskTotal = root.bytesTotal();
        m_diskUsed = root.bytesTotal() - root.bytesFree();
        // The ring follows df's "Uso%": space you can no longer use
        m_diskFull = double(root.bytesTotal() - root.bytesAvailable()) / double(root.bytesTotal());
    }

    emit statsChanged();
}

double SystemInfo::ramUsage() const
{
    return m_mem.total > 0 ? double(m_mem.used()) / double(m_mem.total) : 0.0;
}

QString SystemInfo::cpuSubtitle() const
{
    const QString cores = m_physicalCores == 1 ? tr("1 núcleo") : tr("%1 núcleos").arg(m_physicalCores);
    const QString freq = Format::frequency(m_frequency);
    return freq.isEmpty() ? cores : cores + QStringLiteral(" · ") + freq;
}

QString SystemInfo::frequencyText() const
{
    return Format::frequency(m_frequency);
}

QString SystemInfo::memSubtitle() const
{
    return tr("%1 de %2").arg(Format::bytes(m_mem.used()), Format::bytes(m_mem.total));
}

QString SystemInfo::diskSubtitle() const
{
    return tr("%1 de %2").arg(Format::bytes(m_diskUsed), Format::bytes(m_diskTotal));
}
