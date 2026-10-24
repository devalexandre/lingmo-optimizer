#include "diskusage.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QFile>

#include <algorithm>
#include <set>
#include <utility>

#include <dirent.h>
#include <fcntl.h>
#include <ftw.h>
#include <sys/stat.h>
#include <unistd.h>

QString DuNode::displayName() const
{
    if (kind == Others)
        return entries == 1 ? QCoreApplication::translate("DiskScanner", "1 arquivo menor")
                            : QCoreApplication::translate("DiskScanner", "%1 arquivos menores").arg(entries);
    return QFile::decodeName(name);
}

QString DuNode::path() const
{
    if (!parent)
        return QFile::decodeName(name);
    const QString base = parent->path();
    if (kind == Others)
        return base;
    return base.endsWith(QLatin1Char('/')) ? base + QFile::decodeName(name)
                                           : base + QLatin1Char('/') + QFile::decodeName(name);
}

DuNode *DuNode::child(const QString &childName) const
{
    const QByteArray encoded = QFile::encodeName(childName);
    for (const auto &c : children) {
        if (c->kind != Others && c->name == encoded)
            return c.get();
    }
    return nullptr;
}

void DuNode::sortChildren()
{
    std::stable_sort(children.begin(), children.end(), [](const auto &a, const auto &b) {
        // "Others" always last, the rest by size
        if ((a->kind == Others) != (b->kind == Others))
            return b->kind == Others;
        return a->size > b->size;
    });
}

void DuNode::removeChild(DuNode *target)
{
    auto it = std::find_if(children.begin(), children.end(), [target](const auto &c) { return c.get() == target; });
    if (it == children.end())
        return;
    const qint64 bytes = (*it)->size;
    const qint64 count = (*it)->entries + 1;
    children.erase(it);
    for (DuNode *n = this; n; n = n->parent) {
        n->size = std::max<qint64>(0, n->size - bytes);
        n->entries = std::max<qint64>(0, n->entries - count);
    }
}

struct DiskScanner::Context {
    dev_t device = 0;
    ScanProgress *progress = nullptr;
    std::set<std::pair<dev_t, ino_t>> seenInodes;   // hard links counted once
    std::function<void(std::unique_ptr<DuNode>)> takeTopLevel;
    mutable unsigned checks = 0;
    bool cancelled() const
    {
        if (!progress)
            return false;
        if (progress->cancel.load(std::memory_order_relaxed))
            return true;
        const qint64 deadline = progress->deadline.load(std::memory_order_relaxed);
        if (deadline > 0 && (++checks & 255) == 0 && QDateTime::currentMSecsSinceEpoch() >= deadline)
            progress->cancel.store(true, std::memory_order_relaxed);
        return progress->cancel.load(std::memory_order_relaxed);
    }
};

DiskScanner::DiskScanner() = default;

DiskScanner::DiskScanner(const Options &options)
    : m_options(options)
{
}

std::unique_ptr<DuNode> DiskScanner::scan(const QString &path, ScanProgress *progress,
                                          const std::function<void(std::unique_ptr<DuNode>)> &takeTopLevel)
{
    auto root = std::make_unique<DuNode>();
    QByteArray encoded = QFile::encodeName(path);
    while (encoded.size() > 1 && encoded.endsWith('/'))
        encoded.chop(1);
    root->name = encoded;
    root->kind = DuNode::Dir;

    struct stat st;
    if (::lstat(encoded.constData(), &st) != 0) {
        root->denied = true;
        return root;
    }
    root->size = qint64(st.st_blocks) * 512;
    if (!S_ISDIR(st.st_mode)) {
        root->kind = DuNode::File;
        return root;
    }

    Context ctx;
    ctx.device = st.st_dev;
    ctx.progress = progress;
    ctx.takeTopLevel = takeTopLevel;

    const int fd = ::open(encoded.constData(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
    if (fd < 0) {
        root->denied = true;
        return root;
    }
    scanDir(fd, root.get(), encoded, ctx, 0);
    return root;
}

void DiskScanner::scanDir(int dirFd, DuNode *node, const QByteArray &path, Context &ctx, int depth)
{
    DIR *dir = ::fdopendir(dirFd);
    if (!dir) {
        ::close(dirFd);
        node->denied = true;
        return;
    }
    const int fd = ::dirfd(dir);

    if (ctx.progress) {
        QMutexLocker lock(&ctx.progress->mutex);
        ctx.progress->current = path;
    }

    std::vector<std::unique_ptr<DuNode>> files;
    const bool handOver = depth == 0 && ctx.takeTopLevel;

    errno = 0;
    while (dirent *entry = ::readdir(dir)) {
        if (ctx.cancelled()) {
            node->incomplete = true;
            break;
        }
        const char *name = entry->d_name;
        if (name[0] == '.' && (name[1] == '\0' || (name[1] == '.' && name[2] == '\0')))
            continue;

        struct stat st;
        if (::fstatat(fd, name, &st, AT_SYMLINK_NOFOLLOW) != 0) {
            node->incomplete = true;
            continue;
        }
        qint64 bytes = qint64(st.st_blocks) * 512;

        if (S_ISDIR(st.st_mode)) {
            auto child = std::make_unique<DuNode>();
            child->name = QByteArray(name);
            child->kind = DuNode::Dir;
            child->parent = node;
            const QByteArray childPath = path == "/" ? path + name : path + '/' + name;

            if (m_options.skipPaths.contains(childPath)) {
                child->kind = DuNode::Skipped;
            } else if (m_options.sameFilesystem && st.st_dev != ctx.device) {
                child->kind = DuNode::Mount;
            } else {
                child->size = bytes;
                if (ctx.progress)
                    ctx.progress->bytes.fetch_add(bytes, std::memory_order_relaxed);
                const int childFd = ::openat(fd, name, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
                if (childFd < 0)
                    child->denied = true;
                else
                    scanDir(childFd, child.get(), childPath, ctx, depth + 1);
            }
            node->size += child->size;
            node->entries += child->entries + 1;
            if (child->denied || child->incomplete)
                node->incomplete = true;

            if (handOver)
                ctx.takeTopLevel(std::move(child));
            else if (m_options.keepChildren)
                node->children.push_back(std::move(child));
        } else {
            if (st.st_nlink > 1 && !ctx.seenInodes.insert({st.st_dev, st.st_ino}).second)
                bytes = 0; // another link to a file already counted
            node->size += bytes;
            node->entries += 1;
            if (ctx.progress) {
                ctx.progress->bytes.fetch_add(bytes, std::memory_order_relaxed);
                ctx.progress->entries.fetch_add(1, std::memory_order_relaxed);
            }
            if (m_options.keepChildren) {
                auto file = std::make_unique<DuNode>();
                file->name = QByteArray(name);
                file->kind = DuNode::File;
                file->size = bytes;
                file->parent = node;
                files.push_back(std::move(file));
            }
        }
        errno = 0;
    }
    if (errno != 0)
        node->incomplete = true;
    ::closedir(dir);

    if (!m_options.keepChildren)
        return;

    // Keep every file of small folders; in big ones only the large files,
    // the rest becomes one "N smaller files" entry
    const bool keepAll = int(files.size()) <= m_options.keepAllFilesBelow;
    std::unique_ptr<DuNode> others;
    for (auto &file : files) {
        if (keepAll || file->size >= m_options.keepFileMinSize) {
            if (handOver)
                ctx.takeTopLevel(std::move(file));
            else
                node->children.push_back(std::move(file));
        } else {
            if (!others) {
                others = std::make_unique<DuNode>();
                others->kind = DuNode::Others;
                others->parent = node;
            }
            others->size += file->size;
            others->entries += 1;
        }
    }
    if (others)
        node->children.push_back(std::move(others));
    node->sortChildren();
}

qint64 DiskScanner::totalSize(const QString &path, bool *denied, ScanProgress *progress)
{
    Options options;
    options.keepChildren = false;
    DiskScanner scanner(options);
    const auto root = scanner.scan(path, progress);
    if (denied)
        *denied = root->denied || root->incomplete;
    return root->size;
}

// nftw has no user pointer: per-thread state for removeDirectoryContents
namespace {
thread_local qint64 t_freed = 0;
thread_local int t_failures = 0;
thread_local int t_rootLevel = 0;

int removeEntry(const char *path, const struct stat *st, int type, struct FTW *ftw)
{
    if (ftw->level <= t_rootLevel)
        return 0; // never the folder itself
    int result;
    if (type == FTW_DP)
        result = ::rmdir(path);
    else
        result = ::unlink(path); // a symlink is removed as a link, never followed
    if (result == 0 && type != FTW_NS) {
        if (type == FTW_DP || st->st_nlink <= 1)
            t_freed += qint64(st->st_blocks) * 512;
    } else if (result != 0) {
        ++t_failures;
    }
    return 0;
}
} // namespace

qint64 removeDirectoryContents(const QString &dir, int *failures)
{
    t_freed = 0;
    t_failures = 0;
    t_rootLevel = 0;
    const QByteArray encoded = QFile::encodeName(dir);
    struct stat st;
    if (::lstat(encoded.constData(), &st) != 0 || !S_ISDIR(st.st_mode)) {
        if (failures)
            *failures = 0;
        return 0;
    }
    // FTW_PHYS: do not follow symlinks; FTW_MOUNT: stay on this filesystem;
    // FTW_DEPTH: children before their folder
    ::nftw(encoded.constData(), removeEntry, 32, FTW_DEPTH | FTW_PHYS | FTW_MOUNT);
    if (failures)
        *failures = t_failures;
    return t_freed;
}
