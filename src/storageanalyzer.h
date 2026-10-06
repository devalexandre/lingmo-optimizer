#pragma once

#include <QAbstractListModel>
#include <QFuture>
#include <QHash>
#include <QObject>
#include <QTimer>
#include <QVariantList>

#include <memory>

#include "core/diskusage.h"

// Children of the folder being looked at, largest first
class DirModel : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)

public:
    enum Roles {
        NameRole = Qt::UserRole + 1,
        PathRole,
        SizeRole,
        FractionRole,
        KindRole,       // "dir", "file", "others", "mount", "skipped"
        DeniedRole,
        IncompleteRole,
        EntriesRole,
        CanTrashRole
    };

    using QAbstractListModel::QAbstractListModel;
    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    void setNode(DuNode *node);
    DuNode *node() const { return m_node; }
    DuNode *at(int row) const;
    void childAdded(DuNode *child);   // a progressive result arrived for m_node
    void sizesChanged();
    void reload();

signals:
    void countChanged();

private:
    DuNode *m_node = nullptr;
};

// Mounted filesystems + the "Onde está o espaço" analyzer
class StorageAnalyzer : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QVariantList mounts READ mounts NOTIFY mountsChanged)
    Q_PROPERTY(DirModel *model READ model CONSTANT)
    Q_PROPERTY(QString rootPath READ rootPath NOTIFY nodeChanged)
    Q_PROPERTY(QString currentPath READ currentPath NOTIFY nodeChanged)
    Q_PROPERTY(double currentSize READ currentSize NOTIFY sizesChanged)
    Q_PROPERTY(bool currentDenied READ currentDenied NOTIFY nodeChanged)
    Q_PROPERTY(bool currentIncomplete READ currentIncomplete NOTIFY sizesChanged)
    Q_PROPERTY(QVariantList breadcrumbs READ breadcrumbs NOTIFY nodeChanged)
    Q_PROPERTY(bool scanning READ scanning NOTIFY scanningChanged)
    Q_PROPERTY(bool hasResult READ hasResult NOTIFY nodeChanged)
    Q_PROPERTY(double scannedBytes READ scannedBytes NOTIFY progressChanged)
    Q_PROPERTY(double scannedEntries READ scannedEntries NOTIFY progressChanged)
    Q_PROPERTY(QString scanningPath READ scanningPath NOTIFY progressChanged)
    // Top-level folder being read right now and its size so far (only while
    // the root being shown is the one being scanned)
    Q_PROPERTY(QString pendingName READ pendingName NOTIFY progressChanged)
    Q_PROPERTY(double pendingSize READ pendingSize NOTIFY progressChanged)

public:
    explicit StorageAnalyzer(QObject *parent = nullptr);
    ~StorageAnalyzer() override;

    QVariantList mounts() const { return m_mounts; }
    DirModel *model() const { return m_model; }
    QString rootPath() const { return m_rootPath; }
    QString currentPath() const;
    double currentSize() const;
    bool currentDenied() const;
    bool currentIncomplete() const;
    QVariantList breadcrumbs() const;
    bool scanning() const { return m_scanning; }
    bool hasResult() const { return m_model->node() != nullptr; }
    double scannedBytes() const { return m_scannedBytes; }
    double scannedEntries() const { return m_scannedEntries; }
    QString scanningPath() const { return m_scanningPath; }
    QString pendingName() const { return m_pendingName; }
    double pendingSize() const { return m_pendingSize; }

    Q_INVOKABLE void refreshMounts();
    // Shows `path` (cached result if there is one, a new scan otherwise)
    Q_INVOKABLE void analyze(const QString &path);
    Q_INVOKABLE void rescan();
    Q_INVOKABLE void cancel();
    Q_INVOKABLE void open(int row);               // drill into a folder
    Q_INVOKABLE void goTo(int breadcrumbIndex);    // 0 = root
    Q_INVOKABLE void up();
    // After the file was moved to the trash elsewhere: drop it from the tree
    Q_INVOKABLE void forget(const QString &path);

signals:
    void mountsChanged();
    void nodeChanged();
    void sizesChanged();
    void scanningChanged();
    void progressChanged();

private:
    void startScan(const QString &path);
    void receiveTopLevel(quint64 generation, DuNode *child);
    void scanDone(quint64 generation, DuNode *root);
    void setCurrent(DuNode *node);

    QVariantList m_mounts;
    DirModel *m_model;
    QHash<QString, std::shared_ptr<DuNode>> m_trees;   // finished/ongoing scans by root path
    QString m_rootPath;
    DuNode *m_scanRoot = nullptr;                      // tree being filled
    bool m_scanning = false;
    quint64 m_generation = 0;
    std::shared_ptr<ScanProgress> m_progress;
    QFuture<void> m_future;
    QTimer m_progressTimer;
    double m_scannedBytes = 0;
    double m_scannedEntries = 0;
    QString m_scanningPath;
    QString m_pendingName;
    double m_pendingSize = 0;
};
