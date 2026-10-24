#pragma once

#include <QObject>
#include <QProcess>
#include <QStringList>
#include <QVariantList>

#include "core/devcaches.h"

class DevTools;

// "Otimizar e Limpar": a fixed list of cleanup steps.
//   user-cache    known-safe folders of ~/.cache (thumbnails, shaders, fonts),
//                 deleted in-process, never following symlinks
//   flatpak-user  flatpak uninstall --user --unused (no root needed)
//   dev:<id>      an item of "Ferramentas de desenvolvimento" (DevTools):
//                 the tool's own cleaner or the checked cache folder
//   pacman-cache, pacman-old, pacman-uninstalled, journal, flatpak
//                 root steps, all run by ONE pkexec call of the fixed helper
//                 /usr/lib/lingmo-optimizer/clean-system (one polkit prompt)
class Cleaner : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool running READ running NOTIFY runningChanged)
    Q_PROPERTY(bool estimating READ estimating NOTIFY stepsChanged)
    Q_PROPERTY(QVariantList steps READ steps NOTIFY stepsChanged)
    Q_PROPERTY(QString currentStep READ currentStep NOTIFY currentStepChanged)
    // 0..1 while running (steps done / steps)
    Q_PROPERTY(double progress READ progress NOTIFY currentStepChanged)
    // Bytes freed so far by the current (or last) run
    Q_PROPERTY(double freed READ freed NOTIFY currentStepChanged)
    // Kept for the overview button
    Q_PROPERTY(bool isCleaning READ running NOTIFY runningChanged)

public:
    explicit Cleaner(QObject *parent = nullptr);
    ~Cleaner() override;

    bool running() const { return m_running; }
    bool estimating() const { return m_estimating; }
    QVariantList steps() const { return m_steps; }
    QString currentStep() const { return m_currentStep; }
    double freed() const { return double(m_totalFreed); }
    double progress() const { return m_totalSteps > 0 ? double(m_doneSteps) / m_totalSteps : 0.0; }
    void setDevTools(DevTools *devTools) { m_devTools = devTools; }

    // The folders the "user-cache" step empties (absolute paths)
    static QStringList safeCacheDirs();
    // journalctl --disk-usage (persistent + volatile journals), 0 if unknown
    static qint64 journalSize();

    Q_INVOKABLE void estimate();                  // fills steps[].size in the background
    Q_INVOKABLE void run(const QStringList &stepIds);

signals:
    void runningChanged();
    void stepsChanged();
    void currentStepChanged();
    // results: [{id, title, freed, ok, message}]
    void finished(const QVariantList &results, double totalFreed);

private:
    void setCurrentStep(const QString &id);
    void runUserSteps(const QStringList &ids, const QList<DevCaches::Item> &devItems);
    void stepDone(const QString &nextId);
    void runRootSteps();
    void finish();
    QString titleFor(const QString &id) const;
    void addResult(const QString &id, qint64 freed, bool ok, const QString &message = QString());

    bool m_running = false;
    bool m_estimating = false;
    QVariantList m_steps;
    QString m_currentStep;
    QStringList m_rootSteps;
    QStringList m_cleanedDevIds;
    DevTools *m_devTools = nullptr;
    int m_doneSteps = 0;
    int m_totalSteps = 0;
    QVariantList m_results;
    qint64 m_totalFreed = 0;
    QProcess *m_process = nullptr;
    QByteArray m_output;
};
