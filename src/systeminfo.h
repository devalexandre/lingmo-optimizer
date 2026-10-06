#pragma once

#include <QObject>
#include <QVariantList>
#include <QVector>

#include "core/procparse.h"

// Live CPU, memory and root-disk figures for the overview donuts and the
// CPU/Memória detail pages. Refreshed every 2 s.
class SystemInfo : public QObject
{
    Q_OBJECT
    // CPU
    Q_PROPERTY(double cpuUsage READ cpuUsage NOTIFY statsChanged)
    Q_PROPERTY(QVariantList coreUsages READ coreUsages NOTIFY statsChanged)
    Q_PROPERTY(QString cpuModel READ cpuModel CONSTANT)
    Q_PROPERTY(int physicalCores READ physicalCores CONSTANT)
    Q_PROPERTY(int threads READ threads CONSTANT)
    Q_PROPERTY(double frequencyMhz READ frequencyMhz NOTIFY statsChanged)
    Q_PROPERTY(double maxFrequencyMhz READ maxFrequencyMhz CONSTANT)
    Q_PROPERTY(QString frequencyText READ frequencyText NOTIFY statsChanged)
    Q_PROPERTY(QVariantList loadAverage READ loadAverage NOTIFY statsChanged)
    Q_PROPERTY(QString uptime READ uptime NOTIFY statsChanged)
    Q_PROPERTY(double temperature READ temperature NOTIFY statsChanged)   // °C, -1 when unknown
    Q_PROPERTY(QString cpuSubtitle READ cpuSubtitle NOTIFY statsChanged)
    // Memory (bytes as double: QML numbers)
    Q_PROPERTY(double ramUsage READ ramUsage NOTIFY statsChanged)
    Q_PROPERTY(double memTotal READ memTotal NOTIFY statsChanged)
    Q_PROPERTY(double memUsed READ memUsed NOTIFY statsChanged)
    Q_PROPERTY(double memAvailable READ memAvailable NOTIFY statsChanged)
    Q_PROPERTY(double memCache READ memCache NOTIFY statsChanged)
    Q_PROPERTY(double memFree READ memFree NOTIFY statsChanged)
    Q_PROPERTY(double swapTotal READ swapTotal NOTIFY statsChanged)
    Q_PROPERTY(double swapUsed READ swapUsed NOTIFY statsChanged)
    Q_PROPERTY(double zramOriginal READ zramOriginal NOTIFY statsChanged)
    Q_PROPERTY(double zramCompressed READ zramCompressed NOTIFY statsChanged)
    Q_PROPERTY(double zramMemUsed READ zramMemUsed NOTIFY statsChanged)
    Q_PROPERTY(QString memSubtitle READ memSubtitle NOTIFY statsChanged)
    // Root filesystem
    Q_PROPERTY(double diskUsage READ diskUsage NOTIFY statsChanged)
    Q_PROPERTY(double diskUsed READ diskUsed NOTIFY statsChanged)
    Q_PROPERTY(double diskTotal READ diskTotal NOTIFY statsChanged)
    Q_PROPERTY(QString diskSubtitle READ diskSubtitle NOTIFY statsChanged)

public:
    explicit SystemInfo(QObject *parent = nullptr);

    double cpuUsage() const { return m_cpuUsage; }
    QVariantList coreUsages() const { return m_coreUsages; }
    QString cpuModel() const { return m_cpuModel; }
    int physicalCores() const { return m_physicalCores; }
    int threads() const { return m_threads; }
    double frequencyMhz() const { return m_frequency; }
    double maxFrequencyMhz() const { return m_maxFrequency; }
    QString frequencyText() const;
    QVariantList loadAverage() const { return m_loadAverage; }
    QString uptime() const { return m_uptime; }
    double temperature() const { return m_temperature; }
    QString cpuSubtitle() const;

    double ramUsage() const;
    double memTotal() const { return double(m_mem.total); }
    double memUsed() const { return double(m_mem.used()); }
    double memAvailable() const { return double(m_mem.available); }
    double memCache() const { return double(qMax<qint64>(0, m_mem.cache())); }
    double memFree() const { return double(m_mem.free); }
    double swapTotal() const { return double(m_mem.swapTotal); }
    double swapUsed() const { return double(m_mem.swapUsed()); }
    double zramOriginal() const { return double(m_zramOriginal); }
    double zramCompressed() const { return double(m_zramCompressed); }
    double zramMemUsed() const { return double(m_zramMemUsed); }
    QString memSubtitle() const;

    double diskUsage() const { return m_diskFull; }
    double diskUsed() const { return double(m_diskUsed); }
    double diskTotal() const { return double(m_diskTotal); }
    QString diskSubtitle() const;

    Q_INVOKABLE void refresh();

signals:
    void statsChanged();

private:
    void readStaticCpuInfo();
    double readFrequency() const;
    double readTemperature() const;
    void readZram();

    // CPU
    QVector<ProcParse::CpuTimes> m_lastCpu;
    double m_cpuUsage = 0.0;
    QVariantList m_coreUsages;
    QString m_cpuModel;
    int m_physicalCores = 0;
    int m_threads = 0;
    double m_frequency = 0.0;
    double m_maxFrequency = 0.0;
    QVariantList m_loadAverage;
    QString m_uptime;
    double m_temperature = -1.0;
    QString m_temperatureInput;   // sysfs file found at start-up
    // Memory
    ProcParse::MemInfo m_mem;
    qint64 m_zramOriginal = 0;
    qint64 m_zramCompressed = 0;
    qint64 m_zramMemUsed = 0;
    // Disk
    qint64 m_diskUsed = 0;
    qint64 m_diskTotal = 0;
    double m_diskFull = 0.0;
};
