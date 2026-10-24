#pragma once

#include <QDateTime>
#include <QFuture>
#include <QObject>
#include <QTimer>
#include <QVariantList>

#include <memory>

#include "core/devcaches.h"

// "Ferramentas de desenvolvimento": detection and sizing in a background
// thread (with progress and a time limit). Results stay cached until the
// next scan; after a cleanup only the cleaned items are measured again.
class DevTools : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QVariantList items READ items NOTIFY itemsChanged)
    Q_PROPERTY(QVariantList groups READ groups NOTIFY itemsChanged)   // [{id, title, icon, size, count}]
    Q_PROPERTY(bool scanning READ scanning NOTIFY scanningChanged)
    Q_PROPERTY(bool loaded READ loaded NOTIFY itemsChanged)
    Q_PROPERTY(QString progressLabel READ progressLabel NOTIFY progressChanged)
    Q_PROPERTY(int progressDone READ progressDone NOTIFY progressChanged)
    Q_PROPERTY(int progressTotal READ progressTotal NOTIFY progressChanged)
    Q_PROPERTY(QString scannedAt READ scannedAt NOTIFY itemsChanged)
    Q_PROPERTY(bool includeProjects READ includeProjects WRITE setIncludeProjects NOTIFY includeProjectsChanged)

public:
    explicit DevTools(QObject *parent = nullptr);
    ~DevTools() override;

    QVariantList items() const { return m_variants; }
    QVariantList groups() const;
    bool scanning() const { return m_scanning; }
    bool loaded() const { return m_loaded; }
    QString progressLabel() const { return m_progressLabel; }
    int progressDone() const { return m_progressDone; }
    int progressTotal() const { return m_progressTotal; }
    QString scannedAt() const;
    bool includeProjects() const { return m_includeProjects; }
    void setIncludeProjects(bool include);

    // For the Cleaner (by id, without the "dev:" prefix)
    bool find(const QString &id, DevCaches::Item *out) const;
    QList<DevCaches::Item> rawItems() const { return m_items; }

    Q_INVOKABLE void scan();       // the cached result if there is one
    Q_INVOKABLE void rescan();     // always measures again
    Q_INVOKABLE void cancel();
    // After cleaning: detect again, measuring only these ids (and new items)
    Q_INVOKABLE void refresh(const QStringList &ids);
    Q_INVOKABLE QVariantMap item(const QString &id) const;
    // "settings:ai" -> lingmo-settings -m ai
    Q_INVOKABLE void openLink(const QString &link);

signals:
    void itemsChanged();
    void scanningChanged();
    void progressChanged();
    void includeProjectsChanged();

private:
    void start(const QSet<QString> &remeasure, bool full);
    void setItems(const QList<DevCaches::Item> &items);

    QList<DevCaches::Item> m_items;
    QVariantList m_variants;
    bool m_scanning = false;
    bool m_loaded = false;
    bool m_includeProjects = true;
    bool m_pending = false;
    QSet<QString> m_pendingIds;
    QDateTime m_scannedAt;
    std::shared_ptr<DevCaches::Progress> m_progress;
    QFuture<QList<DevCaches::Item>> m_future;
    QTimer m_progressTimer;
    QString m_progressLabel;
    int m_progressDone = 0;
    int m_progressTotal = 0;
};
