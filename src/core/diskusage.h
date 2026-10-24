#pragma once

#include <QByteArray>
#include <QList>
#include <QMutex>
#include <QString>

#include <atomic>
#include <functional>
#include <memory>
#include <vector>

// One entry of a disk usage tree (like ncdu/baobab). Sizes are real disk
// usage (st_blocks * 512), hard links are counted once, symlinks are never
// followed and the scan stays on one filesystem.
struct DuNode {
    enum Kind : quint8 {
        Dir,
        File,
        Others,   // small files of a folder grouped together
        Mount,    // another filesystem mounted here (not scanned)
        Skipped   // /proc, /sys, /dev, /run
    };

    QByteArray name;           // the root node holds the full path
    qint64 size = 0;           // bytes on disk, children included
    qint64 entries = 0;        // files and folders below (Others: how many files)
    Kind kind = File;
    bool denied = false;       // this folder could not be read (size is partial)
    bool incomplete = false;   // something below could not be read
    DuNode *parent = nullptr;
    std::vector<std::unique_ptr<DuNode>> children; // largest first

    QString displayName() const;
    QString path() const;      // absolute path (for Others: the folder's path)
    bool isDir() const { return kind == Dir; }
    DuNode *child(const QString &name) const;
    void sortChildren();
    // Removes `child` and subtracts its size from every ancestor
    void removeChild(DuNode *child);
};

struct ScanProgress {
    std::atomic<qint64> bytes {0};
    std::atomic<qint64> entries {0};
    std::atomic<bool> cancel {false};
    // Stop at this time (ms since the epoch, CLOCK_REALTIME); 0 = no limit.
    // A scan that hits it is marked incomplete, like a cancelled one.
    std::atomic<qint64> deadline {0};
    QMutex mutex;
    QByteArray current;        // folder being read (guarded by mutex)
    QString currentPath()
    {
        QMutexLocker lock(&mutex);
        return QString::fromLocal8Bit(current);
    }
};

class DiskScanner
{
public:
    struct Options {
        bool sameFilesystem = true;
        QList<QByteArray> skipPaths = {"/proc", "/sys", "/dev", "/run"};
        // A folder with up to this many files keeps all of them as nodes;
        // bigger ones keep only files >= keepFileMinSize, the rest is "Others"
        int keepAllFilesBelow = 100;
        qint64 keepFileMinSize = 1024 * 1024;
        bool keepChildren = true;   // false: just compute the total
    };

    DiskScanner();
    explicit DiskScanner(const Options &options);

    // Scans `path` synchronously. When `takeTopLevel` is given, every finished
    // direct child (a folder with its subtree, or a kept file) is handed over
    // to it as soon as it is done (progressive results) and is not part of
    // the returned root; the root's size still counts everything.
    std::unique_ptr<DuNode> scan(const QString &path, ScanProgress *progress = nullptr,
                                 const std::function<void(std::unique_ptr<DuNode>)> &takeTopLevel = {});

    // Convenience: total disk usage of `path` (0 if missing). With a
    // progress, its cancel flag and deadline apply (then *denied is set).
    static qint64 totalSize(const QString &path, bool *denied = nullptr, ScanProgress *progress = nullptr);

private:
    struct Context;
    void scanDir(int dirFd, DuNode *node, const QByteArray &path, Context &ctx, int depth);

    Options m_options;
};

// Deletes everything inside `dir` (not `dir` itself) without following
// symlinks and without crossing into other filesystems. Returns the bytes
// freed. Used only for the known-safe cache folders.
qint64 removeDirectoryContents(const QString &dir, int *failures = nullptr);
