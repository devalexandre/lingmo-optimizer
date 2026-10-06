#include "storageanalyzer.h"

#include "core/policy.h"

#include <QDir>
#include <QStorageInfo>
#include <QtConcurrent>

#include <algorithm>

#include <unistd.h>

// ---------------------------------------------------------------- DirModel

int DirModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() || !m_node ? 0 : int(m_node->children.size());
}

DuNode *DirModel::at(int row) const
{
    if (!m_node || row < 0 || row >= int(m_node->children.size()))
        return nullptr;
    return m_node->children.at(size_t(row)).get();
}

QVariant DirModel::data(const QModelIndex &index, int role) const
{
    const DuNode *n = at(index.row());
    if (!n)
        return {};
    switch (role) {
    case NameRole: return n->displayName();
    case PathRole: return n->path();
    case SizeRole: return double(n->size);
    case FractionRole: return m_node->size > 0 ? double(n->size) / double(m_node->size) : 0.0;
    case KindRole:
        switch (n->kind) {
        case DuNode::Dir: return QStringLiteral("dir");
        case DuNode::File: return QStringLiteral("file");
        case DuNode::Others: return QStringLiteral("others");
        case DuNode::Mount: return QStringLiteral("mount");
        case DuNode::Skipped: return QStringLiteral("skipped");
        }
        return QString();
    case DeniedRole: return n->denied;
    case IncompleteRole: return n->incomplete;
    case EntriesRole: return double(n->entries);
    case CanTrashRole:
        return (n->kind == DuNode::Dir || n->kind == DuNode::File)
            && Policy::mayMoveToTrash(n->path(), QDir::homePath(), ::getuid());
    }
    return {};
}

QHash<int, QByteArray> DirModel::roleNames() const
{
    return {
        {NameRole, "name"}, {PathRole, "path"}, {SizeRole, "size"}, {FractionRole, "fraction"},
        {KindRole, "kind"}, {DeniedRole, "denied"}, {IncompleteRole, "incomplete"},
        {EntriesRole, "entries"}, {CanTrashRole, "canTrash"},
    };
}

void DirModel::setNode(DuNode *node)
{
    beginResetModel();
    m_node = node;
    endResetModel();
    emit countChanged();
}

void DirModel::reload()
{
    setNode(m_node);
}

void DirModel::childAdded(DuNode *child)
{
    if (!m_node)
        return;
    // The node keeps its children sorted; find where this one went
    int row = -1;
    for (size_t i = 0; i < m_node->children.size(); ++i) {
        if (m_node->children.at(i).get() == child) {
            row = int(i);
            break;
        }
    }
    if (row < 0)
        return;
    beginInsertRows(QModelIndex(), row, row);
    endInsertRows();
    emit countChanged();
    sizesChanged();
}

void DirModel::sizesChanged()
{
    if (rowCount() > 0)
        emit dataChanged(index(0), index(rowCount() - 1), {FractionRole, SizeRole});
}

// ---------------------------------------------------------------- analyzer

StorageAnalyzer::StorageAnalyzer(QObject *parent)
    : QObject(parent)
    , m_model(new DirModel(this))
{
    m_progressTimer.setInterval(250);
    connect(&m_progressTimer, &QTimer::timeout, this, [this] {
        if (!m_progress)
            return;
        m_scannedBytes = double(m_progress->bytes.load());
        m_scannedEntries = double(m_progress->entries.load());
        m_scanningPath = m_progress->currentPath();
        m_pendingName.clear();
        m_pendingSize = 0;
        if (m_scanRoot && m_model->node() == m_scanRoot) {
            const QString root = m_scanRoot->path();
            const QString prefix = root == QLatin1String("/") ? root : root + QLatin1Char('/');
            if (m_scanningPath.startsWith(prefix)) {
                m_pendingName = m_scanningPath.mid(prefix.size()).section(QLatin1Char('/'), 0, 0);
                // Everything read so far minus what was already handed over
                m_pendingSize = qMax(0.0, m_scannedBytes - double(m_scanRoot->size));
            }
        }
        emit progressChanged();
    });
    refreshMounts();
}

StorageAnalyzer::~StorageAnalyzer()
{
    if (m_progress)
        m_progress->cancel = true;
    m_future.waitForFinished();
}

void StorageAnalyzer::refreshMounts()
{
    static const QStringList realTypes = {
        QStringLiteral("ext2"), QStringLiteral("ext3"), QStringLiteral("ext4"), QStringLiteral("btrfs"),
        QStringLiteral("xfs"), QStringLiteral("f2fs"), QStringLiteral("vfat"), QStringLiteral("exfat"),
        QStringLiteral("ntfs"), QStringLiteral("ntfs3"), QStringLiteral("fuseblk"), QStringLiteral("zfs"),
        QStringLiteral("jfs"), QStringLiteral("reiserfs"), QStringLiteral("bcachefs"), QStringLiteral("nilfs2"),
        QStringLiteral("hfsplus"), QStringLiteral("udf"), QStringLiteral("iso9660"),
    };
    QVariantList list;
    QHash<QByteArray, int> byDevice;
    QList<QStorageInfo> volumes = QStorageInfo::mountedVolumes();
    std::sort(volumes.begin(), volumes.end(), [](const QStorageInfo &a, const QStorageInfo &b) {
        return a.rootPath().size() < b.rootPath().size();
    });
    for (const QStorageInfo &v : std::as_const(volumes)) {
        const QString type = QString::fromLatin1(v.fileSystemType());
        if (!realTypes.contains(type) || !v.isValid() || !v.isReady() || v.bytesTotal() <= 0)
            continue;
        // Same device mounted twice (btrfs subvolumes, bind mounts): one entry
        if (byDevice.contains(v.device())) {
            QVariantMap m = list.at(byDevice.value(v.device())).toMap();
            QStringList also = m.value(QStringLiteral("alsoMounted")).toStringList();
            also.append(v.rootPath());
            m[QStringLiteral("alsoMounted")] = also;
            list[byDevice.value(v.device())] = m;
            continue;
        }
        const qint64 used = v.bytesTotal() - v.bytesFree();
        byDevice.insert(v.device(), int(list.size()));
        list.append(QVariantMap {
            {QStringLiteral("device"), QString::fromLocal8Bit(v.device())},
            {QStringLiteral("mountPoint"), v.rootPath()},
            {QStringLiteral("fsType"), type},
            {QStringLiteral("total"), double(v.bytesTotal())},
            {QStringLiteral("used"), double(used)},
            {QStringLiteral("free"), double(v.bytesAvailable())},
            {QStringLiteral("fraction"), double(v.bytesTotal() - v.bytesAvailable()) / double(v.bytesTotal())},
            {QStringLiteral("readOnly"), v.isReadOnly()},
            {QStringLiteral("alsoMounted"), QStringList()},
        });
    }
    m_mounts = list;
    emit mountsChanged();
}

QString StorageAnalyzer::currentPath() const
{
    return m_model->node() ? m_model->node()->path() : QString();
}

double StorageAnalyzer::currentSize() const
{
    return m_model->node() ? double(m_model->node()->size) : 0.0;
}

bool StorageAnalyzer::currentDenied() const
{
    return m_model->node() && m_model->node()->denied;
}

bool StorageAnalyzer::currentIncomplete() const
{
    return m_model->node() && m_model->node()->incomplete;
}

QVariantList StorageAnalyzer::breadcrumbs() const
{
    QVariantList crumbs;
    for (const DuNode *n = m_model->node(); n; n = n->parent) {
        QString name = n->parent ? n->displayName() : n->path();
        if (!n->parent && n->path() == QDir::homePath())
            name = tr("Pasta pessoal");
        crumbs.prepend(QVariantMap {{QStringLiteral("name"), name}, {QStringLiteral("path"), n->path()}});
    }
    return crumbs;
}

void StorageAnalyzer::setCurrent(DuNode *node)
{
    m_model->setNode(node);
    emit nodeChanged();
    emit sizesChanged();
}

void StorageAnalyzer::analyze(const QString &rawPath)
{
    QString path = QDir::cleanPath(rawPath);
    if (path.isEmpty())
        return;
    const auto cached = m_trees.constFind(path);
    if (cached != m_trees.constEnd()) {
        m_rootPath = path;
        setCurrent(cached->get());
        return;
    }
    startScan(path);
}

void StorageAnalyzer::rescan()
{
    if (!m_rootPath.isEmpty())
        startScan(m_rootPath);
}

void StorageAnalyzer::cancel()
{
    if (m_progress)
        m_progress->cancel = true;
}

void StorageAnalyzer::startScan(const QString &path)
{
    // A previous scan keeps running until it notices the flag; its results
    // carry an old generation number and are thrown away
    if (m_progress)
        m_progress->cancel = true;
    m_model->setNode(nullptr); // before any tree it may point into goes away
    if (m_scanning && m_rootPath != path)
        m_trees.remove(m_rootPath); // half-done tree of the abandoned scan
    ++m_generation;

    auto root = std::make_shared<DuNode>();
    root->name = QFile::encodeName(path);
    root->kind = DuNode::Dir;
    m_trees.insert(path, root);
    m_rootPath = path;
    m_scanRoot = root.get();
    setCurrent(m_scanRoot);

    m_progress = std::make_shared<ScanProgress>();
    m_scannedBytes = m_scannedEntries = 0;
    m_scanningPath = path;
    m_scanning = true;
    m_progressTimer.start();
    emit scanningChanged();
    emit progressChanged();

    const quint64 generation = m_generation;
    const auto progress = m_progress;
    m_future = QtConcurrent::run([this, path, generation, progress] {
        DiskScanner scanner;
        auto result = scanner.scan(path, progress.get(), [this, generation](std::unique_ptr<DuNode> child) {
            DuNode *raw = child.release();
            QMetaObject::invokeMethod(this, [this, generation, raw] { receiveTopLevel(generation, raw); },
                                      Qt::QueuedConnection);
        });
        DuNode *raw = result.release();
        QMetaObject::invokeMethod(this, [this, generation, raw] { scanDone(generation, raw); }, Qt::QueuedConnection);
    });
}

void StorageAnalyzer::receiveTopLevel(quint64 generation, DuNode *raw)
{
    std::unique_ptr<DuNode> child(raw);
    if (generation != m_generation || !m_scanRoot)
        return;
    child->parent = m_scanRoot;
    m_scanRoot->size += child->size;
    m_scanRoot->entries += child->entries + 1;
    if (child->denied || child->incomplete)
        m_scanRoot->incomplete = true;
    DuNode *added = child.get();
    auto &children = m_scanRoot->children;
    auto pos = std::find_if(children.begin(), children.end(),
                            [added](const auto &c) { return c->kind == DuNode::Others || c->size < added->size; });
    children.insert(pos, std::move(child));
    if (m_model->node() == m_scanRoot)
        m_model->childAdded(added);
    emit sizesChanged();
}

void StorageAnalyzer::scanDone(quint64 generation, DuNode *raw)
{
    std::unique_ptr<DuNode> result(raw);
    if (generation != m_generation || !m_scanRoot)
        return;
    // The worker's root holds the totals and the "smaller files" entry
    m_scanRoot->size = result->size;
    m_scanRoot->entries = result->entries;
    m_scanRoot->denied = result->denied;
    m_scanRoot->incomplete = result->incomplete || m_progress->cancel.load();
    for (auto &c : result->children) {
        c->parent = m_scanRoot;
        m_scanRoot->children.push_back(std::move(c));
    }
    m_scanRoot->sortChildren();
    if (m_model->node() == m_scanRoot)
        m_model->reload();

    m_scanRoot = nullptr;
    m_scanning = false;
    m_pendingName.clear();
    m_pendingSize = 0;
    m_progressTimer.stop();
    m_scannedBytes = double(m_progress->bytes.load());
    m_scannedEntries = double(m_progress->entries.load());
    emit progressChanged();
    emit scanningChanged();
    emit sizesChanged();
    emit nodeChanged();
}

void StorageAnalyzer::open(int row)
{
    DuNode *n = m_model->at(row);
    if (!n)
        return;
    if (n->kind == DuNode::Dir && !n->children.empty())
        setCurrent(n);
    else if (n->kind == DuNode::Mount)
        analyze(n->path());
}

void StorageAnalyzer::goTo(int index)
{
    QVector<DuNode *> chain;
    for (DuNode *n = m_model->node(); n; n = n->parent)
        chain.prepend(n);
    if (index >= 0 && index < chain.size())
        setCurrent(chain.at(index));
}

void StorageAnalyzer::up()
{
    if (m_model->node() && m_model->node()->parent)
        setCurrent(m_model->node()->parent);
}

void StorageAnalyzer::forget(const QString &rawPath)
{
    const QString path = QDir::cleanPath(rawPath);
    bool changed = false;
    for (auto it = m_trees.begin(); it != m_trees.end(); ++it) {
        const QString root = it.key();
        if (!path.startsWith(root == QLatin1String("/") ? root : root + QLatin1Char('/')))
            continue;
        const QStringList parts = path.mid(root.size()).split(QLatin1Char('/'), Qt::SkipEmptyParts);
        DuNode *node = it->get();
        for (const QString &part : parts) {
            node = node ? node->child(part) : nullptr;
        }
        if (!node || !node->parent)
            continue;
        // Leaving the folder being shown if it is the one removed
        for (DuNode *n = m_model->node(); n; n = n->parent) {
            if (n == node) {
                m_model->setNode(node->parent);
                break;
            }
        }
        node->parent->removeChild(node);
        changed = true;
    }
    if (changed) {
        m_model->reload();
        emit nodeChanged();
        emit sizesChanged();
    }
}
