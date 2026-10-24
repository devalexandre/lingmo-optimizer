#pragma once

#include <QList>
#include <QMutex>
#include <QProcessEnvironment>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

#include <atomic>

// "Ferramentas de desenvolvimento": caches and leftovers of developer tools
// (package managers, Docker/Podman, Go, Node, Python, Rust, Java/Android,
// JetBrains, AI models, old CMake builds).
//
// Pure of UI code so the tests can run it against a scratch HOME with fake
// tools on PATH. Three steps:
//   detect()   what exists (cheap; asks docker/paccache/ollama with timeouts)
//   measure()  disk usage of each item, with a time limit
//   clean()    the tool's own cleaner when installed, otherwise deleting the
//              cache folder, after checkPath() approved every path
namespace DevCaches {

enum class Action {
    None,        // list only
    EmptyDirs,   // delete what is inside each path (the folders stay)
    RemoveDirs,  // delete each path itself (a version, node_modules, a venv)
    Command,     // run program + args (the tool's own cleaner)
    Root         // a step of the privileged helper (pacman), run by the Cleaner
};

struct Item {
    QString id;               // stable across scans ("go-build", "nvm:v18.20.4", "nm:/home/...")
    QString group;            // packages, docker, podman, go, node, python, rust, java, jetbrains, ai, builds
    QString title;
    QString description;      // what it is, in plain words
    QString comesBack;        // whether it comes back by itself
    QString warning;          // shown in orange (volumes may hold databases...)
    QString path;             // shown to the person (and "Abrir")
    QStringList paths;        // what EmptyDirs/RemoveDirs delete, and what is measured
    QStringList roots;        // known cache locations: every path must be inside one of them
    Action action = Action::None;
    Action fallback = Action::None; // when the tool is missing at cleaning time
    QString programName;      // "go", "npm"... (looked up again when cleaning)
    QString program;          // resolved at detection ("" = not installed)
    QStringList args;
    QString commandText;      // shown: "go clean -cache"
    QString rootStep;         // Action::Root: helper step id
    QString link;             // "settings:ai", "settings:ai/transcription"
    qint64 size = -1;         // bytes on disk, -1 = unknown
    bool sizeFromTool = false; // reported by docker/podman/paccache, not measured here
    bool partial = false;     // measuring stopped early: at least `size`
    bool upTo = false;        // the tool frees at most `size` (pnpm store prune)
    bool reinstall = false;   // removes an environment/version/content: "pode precisar reinstalar"
    bool current = false;     // the version in use: listed, never offered
    bool defaultSelected = false;
    QString blockedReason;    // a path failed checkPath (shown, not cleanable)
    QString busyReason;       // in use right now
    QStringList busyComms;    // process names that use it
    QStringList busyCmdline;  // command line substrings that use it
    QStringList busyExePrefixes; // executables under these folders use it
    int order = 100;          // cleaning order (docker images before containers)
    QVariantList children;    // [{name, size, detail}] shown under the item
    qint64 ageDays = -1;      // project items: days since last touched

    bool cleanable() const
    {
        return action != Action::None && !current && blockedReason.isEmpty();
    }
};

struct Env {
    QString home;
    QStringList path;               // PATH entries (plus ~/.local/bin, ~/.cargo/bin...)
    QProcessEnvironment vars;       // GOMODCACHE, NVM_DIR, PYENV_ROOT...
    qint64 now = 0;                 // seconds since the epoch
    uint uid = 0;
    bool scanProjects = true;       // node_modules, venvs and CMake builds in project folders
    QStringList projectRoots;       // empty: the usual names in home (projects, src, dev...)
    QStringList userDirs;           // Documents, Pictures...: never touched, nothing inside them
    QString pacmanCache = QStringLiteral("/var/cache/pacman/pkg");
    QString pacmanLock = QStringLiteral("/var/lib/pacman/db.lck");
    QString procDir = QStringLiteral("/proc");
    int commandTimeoutMs = 20000;   // detection commands (docker df, paccache -d...)
    int cleanTimeoutMs = 30 * 60 * 1000;

    static Env system();
    QString var(const QString &name) const { return vars.value(name); }
    QString findExecutable(const QString &name) const;
    QProcessEnvironment processEnvironment() const;
    QStringList effectiveProjectRoots() const;
};

// Shared with the UI thread: what is being looked at, how far along
struct Progress {
    std::atomic<bool> cancel {false};
    std::atomic<int> done {0};
    std::atomic<int> total {0};
    QMutex mutex;
    QString label;
    void setLabel(const QString &text)
    {
        QMutexLocker lock(&mutex);
        label = text;
    }
    QString currentLabel()
    {
        QMutexLocker lock(&mutex);
        return label;
    }
};

QList<Item> detect(const Env &env, Progress *progress = nullptr);

// Fills Item::size (and partial). Only the items in `only` when it is not
// empty. Each folder gets at most perPathMs, everything at most totalMs; what
// is left after that is marked partial with the size found so far.
void measure(QList<Item> &items, const Env &env, Progress *progress = nullptr,
             qint64 perPathMs = 15000, qint64 totalMs = 120000, const QSet<QString> &only = {});

struct Result {
    qint64 freed = 0;
    bool ok = false;
    QString message;
};
// Cleans one item (never Root: the Cleaner runs the helper). Re-checks every
// path and whether something uses it right now.
Result clean(const Item &item, const Env &env);

// What may be cleaned without a person looking at the list (the intent
// optimizer.clean.caches): preselected caches that come back by themselves.
// Never versions, environments, projects, volumes, containers, list-only or
// root items, nor anything in use or refused by checkPath.
bool safeForAutomation(const Item &item);

// ---------------------------------------------------------------- safety
enum class PathVerdict {
    Allowed,
    NotAbsolute,
    Missing,
    IsHome,
    OutsideHome,
    OutsideRoot,     // not inside the item's known cache location
    UserFolder,      // Documents, Pictures... or something holding/inside one
    Symlink,         // the path itself is a symbolic link
    SymlinkEscape,   // a folder on the way is a symbolic link
    NotDirectory,
    NotOwned
};
// May `path` be deleted (removeSelf) or emptied, as part of the cache
// location `root`? Both must be strictly inside home; the path must be root
// itself (emptying only) or inside it, a real folder owned by the user, with
// no symbolic link anywhere on the way from home.
PathVerdict checkPath(const QString &path, const QString &root, const Env &env, bool removeSelf);
QString verdictText(PathVerdict verdict);

// Deletes everything inside `path` (and `path` itself with removeSelf)
// without following symbolic links (they are removed as links) and without
// entering other filesystems. Read-only folders (the Go module cache) are
// made writable first. Returns the bytes freed.
qint64 removeTree(const QString &path, bool removeSelf, int *failures = nullptr);

// ---------------------------------------------------------------- in use
struct Running {
    QString comm;
    QString cmdline;   // arguments joined with spaces
    QString exe;       // resolved /proc/<pid>/exe ("" when not readable)
};
QList<Running> runningProcesses(const QString &procDir = QStringLiteral("/proc"));
// "" when nothing in `running` uses the item, else who does
QString busyReason(const Item &item, const QList<Running> &running);

// ---------------------------------------------------------------- versions
// -1, 0, 1 comparing "v18.20.4", "go1.21.5", "3.12.1" (numbers compared as numbers)
int compareVersions(const QString &a, const QString &b);
// nvm alias ("default" file contents: "20", "lts/*", "node", "v18.1.0") to an installed version, "" if none
QString resolveNvmAlias(const QString &nvmDir, const QString &alias, const QStringList &installed);
// Versions nvm uses: the one of `node` on PATH (resolved), $NVM_BIN, and the default alias.
// When the default cannot be resolved, the newest installed version is kept.
QStringList nvmProtected(const QString &nvmDir, const QStringList &installed, const QString &resolvedNode,
                         const QString &nvmBin);
// gvm: the go on PATH (resolved), $GOROOT, and $GVM_ROOT/environments/default (gvm_go_name)
QStringList gvmProtected(const QString &gvmRoot, const QStringList &installed, const QString &resolvedGo,
                         const QString &goroot);
// pyenv: $PYENV_ROOT/version (global), $PYENV_VERSION, and the python on PATH (resolved)
QStringList pyenvProtected(const QString &pyenvRoot, const QStringList &installed, const QString &pyenvVersionEnv,
                           const QString &resolvedPython);
// rustup: default_toolchain and [overrides] of settings.toml, and $RUSTUP_TOOLCHAIN
QStringList rustupProtected(const QString &settingsToml, const QStringList &installed, const QString &envToolchain);

struct JetBrainsDir {
    QString product;   // "IntelliJIdea", "PyCharm", "GoLand"...
    int year = 0;
    int minor = 0;
};
bool parseJetBrainsDir(const QString &name, JetBrainsDir *out);
// Of these folder names, the ones that are not the newest of their product
QStringList oldJetBrainsDirs(const QStringList &names);

// pacman: "disk space saved: 1.50 GiB" of a paccache dry run, -1 if absent
qint64 parsePaccacheSaved(const QString &output);
// docker/podman "Total reclaimed space: 1.2GB", -1 if absent
qint64 parseReclaimed(const QString &output);

QVariantMap toVariant(const Item &item);
QString groupTitle(const QString &group);
QString groupIcon(const QString &group);
QStringList groupOrder();

} // namespace DevCaches
