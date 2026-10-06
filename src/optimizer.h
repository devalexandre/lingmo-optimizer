#pragma once
#include <QObject>
#include <QVariantMap>
#include <QProcess>

class Optimizer : public QObject {
    Q_OBJECT
    Q_PROPERTY(double cpuUsage READ cpuUsage NOTIFY statsChanged)
    Q_PROPERTY(double ramUsage READ ramUsage NOTIFY statsChanged)
    Q_PROPERTY(double diskUsage READ diskUsage NOTIFY statsChanged)
    Q_PROPERTY(bool isCleaning READ isCleaning NOTIFY isCleaningChanged)
    
public:
    explicit Optimizer(QObject *parent = nullptr);
    double cpuUsage() const;
    double ramUsage() const;
    double diskUsage() const;
    bool isCleaning() const;

    Q_INVOKABLE void refreshStats();
    Q_INVOKABLE void startCleanup();

signals:
    void statsChanged();
    void isCleaningChanged();
    void cleanupFinished(const QString &message);

private:
    double m_cpuUsage = 0.0;
    double m_ramUsage = 0.0;
    double m_diskUsage = 0.0;
    bool m_isCleaning = false;
    long long m_lastTotalUser, m_lastTotalUserLow, m_lastTotalSys, m_lastTotalIdle;
};
