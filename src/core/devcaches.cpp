#include "devcaches.h"

#include "diskusage.h"
#include "hogs.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QProcess>
#include <QRegularExpression>
#include <QStandardPaths>

#include <algorithm>
#include <functional>
#include <vector>

#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

namespace DevCaches {

static QString tr_(const char *text)
{
    return QCoreApplication::translate("DevCaches", text);
}

static constexpr qint64 Day = 24 * 60 * 60;

// ======================================================================
// small helpers

static QString stripSlash(QString path)
{
    while (path.size() > 1 && path.endsWith(QLatin1Char('/')))
        path.chop(1);
    return path;
}

static bool isInside(const QString &path, const QString &dir)
{
    return path.startsWith(dir + QLatin1Char('/'));
}

static bool isRealDir(const QString &path)
{
    struct stat st;
    return ::lstat(QFile::encodeName(path).constData(), &st) == 0 && S_ISDIR(st.st_mode);
}

static bool isRealFile(const QString &path)
{
    struct stat st;
    return ::lstat(QFile::encodeName(path).constData(), &st) == 0 && S_ISREG(st.st_mode);
}

static qint64 mtimeOf(const QString &path)
{
    struct stat st;
    return ::lstat(QFile::encodeName(path).constData(), &st) == 0 ? qint64(st.st_mtime) : 0;
}

static QString readText(const QString &path, qint64 max = 64 * 1024)
{
    if (!isRealFile(path))
        return QString();
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return QString();
    return QString::fromUtf8(f.read(max));
}

static QString canonical(const QString &path)
{
    if (path.isEmpty())
        return QString();
    const QString c = QFileInfo(path).canonicalFilePath();
    return c.isEmpty() ? QDir::cleanPath(path) : c;
}

// Real (non-symlink) subfolders of `dir`, sorted
static QStringList subdirs(const QString &dir)
{
    QStringList names = QDir(dir).entryList(QDir::Dirs | QDir::NoDotAndDotDot | QDir::NoSymLinks | QDir::Hidden, QDir::Name);
    names.erase(std::remove_if(names.begin(), names.end(), [&dir](const QString &n) {
                    return !isRealDir(dir + QLatin1Char('/') + n);
                }),
                names.end());
    return names;
}

struct CommandOutput {
    bool started = false;
    bool finished = false;
    int exitCode = -1;
    QString out;
};

static CommandOutput runCommand(const Env &env, const QString &program, const QStringList &args, int timeoutMs,
                                bool merge = false)
{
    CommandOutput result;
    if (program.isEmpty())
        return result;
    QProcess p;
    p.setProcessEnvironment(env.processEnvironment());
    p.setWorkingDirectory(env.home);
    if (merge)
        p.setProcessChannelMode(QProcess::MergedChannels);
    p.start(program, args);
    result.started = p.waitForStarted(5000);
    if (!result.started)
        return result;
    result.finished = p.waitForFinished(timeoutMs);
    if (!result.finished) {
        p.kill();
        p.waitForFinished(2000);
        return result;
    }
    result.exitCode = p.exitStatus() == QProcess::NormalExit ? p.exitCode() : -1;
    result.out = QString::fromUtf8(p.readAllStandardOutput());
    return result;
}

static QString lastLines(const QString &text, int count = 3)
{
    QStringList lines = text.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    for (QString &l : lines)
        l = l.trimmed();
    lines.removeAll(QString());
    return lines.mid(qMax(0, int(lines.size()) - count)).join(QLatin1Char('\n'));
}

// ======================================================================
// Env

Env Env::system()
{
    Env env;
    env.home = stripSlash(QDir::homePath());
    env.vars = QProcessEnvironment::systemEnvironment();
    env.now = QDateTime::currentSecsSinceEpoch();
    env.uid = ::getuid();
    env.path = env.vars.value(QStringLiteral("PATH")).split(QLatin1Char(':'), Qt::SkipEmptyParts);
    // Desktop sessions often lack what ~/.bashrc adds to PATH
    const QString cargo = env.var(QStringLiteral("CARGO_HOME")).isEmpty() ? env.home + QStringLiteral("/.cargo")
                                                                          : env.var(QStringLiteral("CARGO_HOME"));
    const QString pyenv = env.var(QStringLiteral("PYENV_ROOT")).isEmpty() ? env.home + QStringLiteral("/.pyenv")
                                                                          : env.var(QStringLiteral("PYENV_ROOT"));
    const QString gvm = env.var(QStringLiteral("GVM_ROOT")).isEmpty() ? env.home + QStringLiteral("/.gvm")
                                                                      : env.var(QStringLiteral("GVM_ROOT"));
    for (const QString &extra : {env.home + QStringLiteral("/.local/bin"), cargo + QStringLiteral("/bin"),
                                 env.home + QStringLiteral("/go/bin"), pyenv + QStringLiteral("/bin"), gvm + QStringLiteral("/bin")}) {
        if (!env.path.contains(extra))
            env.path.append(extra);
    }
    for (auto location : {QStandardPaths::DesktopLocation, QStandardPaths::DocumentsLocation, QStandardPaths::MusicLocation,
                          QStandardPaths::MoviesLocation, QStandardPaths::PicturesLocation, QStandardPaths::DownloadLocation,
                          QStandardPaths::PublicShareLocation, QStandardPaths::TemplatesLocation}) {
        const QString dir = stripSlash(QStandardPaths::writableLocation(location));
        if (!dir.isEmpty() && dir != env.home && !env.userDirs.contains(dir))
            env.userDirs.append(dir);
    }
    return env;
}

QString Env::findExecutable(const QString &name) const
{
    if (path.isEmpty())
        return QString();
    return QStandardPaths::findExecutable(name, path);
}

QProcessEnvironment Env::processEnvironment() const
{
    QProcessEnvironment e = vars;
    e.insert(QStringLiteral("HOME"), home);
    e.insert(QStringLiteral("PATH"), path.join(QLatin1Char(':')));
    e.insert(QStringLiteral("LC_ALL"), QStringLiteral("C"));
    // Never let a tool stop and ask
    e.insert(QStringLiteral("DEBIAN_FRONTEND"), QStringLiteral("noninteractive"));
    e.insert(QStringLiteral("CI"), QStringLiteral("1"));
    return e;
}

QStringList Env::effectiveProjectRoots() const
{
    if (!projectRoots.isEmpty())
        return projectRoots;
    QStringList roots;
    for (const char *name : {"projects", "Projects", "projetos", "Projetos", "dev", "src", "code", "git", "workspace", "repos"}) {
        const QString dir = home + QLatin1Char('/') + QLatin1String(name);
        if (isRealDir(dir))
            roots.append(dir);
    }
    return roots;
}

// ======================================================================
// safety

PathVerdict checkPath(const QString &path, const QString &root, const Env &env, bool removeSelf)
{
    if (!path.startsWith(QLatin1Char('/')) || !env.home.startsWith(QLatin1Char('/')) || !root.startsWith(QLatin1Char('/')))
        return PathVerdict::NotAbsolute;
    const QString home = stripSlash(QDir::cleanPath(env.home));
    const QString clean = stripSlash(QDir::cleanPath(path));
    const QString cleanRoot = stripSlash(QDir::cleanPath(root));
    if (clean == home)
        return PathVerdict::IsHome;
    if (!isInside(clean, home))
        return PathVerdict::OutsideHome;
    // The known cache location itself must be strictly inside home
    if (!isInside(cleanRoot, home))
        return PathVerdict::OutsideRoot;
    if (removeSelf ? !isInside(clean, cleanRoot) : (clean != cleanRoot && !isInside(clean, cleanRoot)))
        return PathVerdict::OutsideRoot;
    for (const QString &dir : env.userDirs) {
        const QString ud = stripSlash(QDir::cleanPath(dir));
        if (ud.isEmpty() || ud == home)
            continue;
        if (clean == ud || isInside(clean, ud) || isInside(ud, clean))
            return PathVerdict::UserFolder;
    }

    struct stat st;
    if (::lstat(QFile::encodeName(clean).constData(), &st) != 0)
        return PathVerdict::Missing;
    if (S_ISLNK(st.st_mode))
        return PathVerdict::Symlink;
    if (!S_ISDIR(st.st_mode))
        return PathVerdict::NotDirectory;

    // No symbolic link anywhere between home and the path: resolving it must
    // give exactly the same path below the (resolved) home
    const QString realHome = stripSlash(QFileInfo(home).canonicalFilePath());
    const QString realPath = stripSlash(QFileInfo(clean).canonicalFilePath());
    if (realHome.isEmpty() || realPath.isEmpty())
        return PathVerdict::Missing;
    if (realPath != realHome + clean.mid(home.size()))
        return PathVerdict::SymlinkEscape;

    if (st.st_uid != env.uid)
        return PathVerdict::NotOwned;
    return PathVerdict::Allowed;
}

QString verdictText(PathVerdict verdict)
{
    switch (verdict) {
    case PathVerdict::Allowed: return QString();
    case PathVerdict::NotAbsolute: return tr_("Caminho inválido");
    case PathVerdict::Missing: return tr_("A pasta não existe mais");
    case PathVerdict::IsHome: return tr_("É a sua pasta pessoal");
    case PathVerdict::OutsideHome: return tr_("Fora da sua pasta pessoal");
    case PathVerdict::OutsideRoot: return tr_("Fora das pastas de cache conhecidas");
    case PathVerdict::UserFolder: return tr_("Dentro de uma pasta de documentos, fotos, músicas ou downloads");
    case PathVerdict::Symlink: return tr_("É um link simbólico: o Otimizador não segue links");
    case PathVerdict::SymlinkEscape: return tr_("Está atrás de um link simbólico: o Otimizador não segue links");
    case PathVerdict::NotDirectory: return tr_("Não é uma pasta");
    case PathVerdict::NotOwned: return tr_("Pertence a outro usuário");
    }
    return QString();
}

namespace {
struct RemoveContext {
    dev_t device = 0;
    qint64 freed = 0;
    int failures = 0;
};

void emptyDirFd(int fd, RemoveContext &ctx, int depth);

void removeEntryAt(int parentFd, const char *name, RemoveContext &ctx, int depth)
{
    struct stat st;
    if (::fstatat(parentFd, name, &st, AT_SYMLINK_NOFOLLOW) != 0) {
        ++ctx.failures;
        return;
    }
    const qint64 bytes = qint64(st.st_blocks) * 512;
    if (S_ISDIR(st.st_mode)) {
        // Another filesystem mounted here (or absurd nesting): leave it
        if (st.st_dev != ctx.device || depth > 512) {
            ++ctx.failures;
            return;
        }
        // The Go module cache is read-only on purpose
        if ((st.st_mode & S_IRWXU) != S_IRWXU)
            ::fchmodat(parentFd, name, (st.st_mode & 07777) | S_IRWXU, 0);
        const int fd = ::openat(parentFd, name, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
        if (fd < 0) {
            ++ctx.failures;
            return;
        }
        emptyDirFd(fd, ctx, depth + 1);
        if (::unlinkat(parentFd, name, AT_REMOVEDIR) == 0)
            ctx.freed += bytes;
        else
            ++ctx.failures;
    } else {
        // Files, and symbolic links as links (never followed)
        if (::unlinkat(parentFd, name, 0) == 0) {
            if (st.st_nlink <= 1)
                ctx.freed += bytes;
        } else {
            ++ctx.failures;
        }
    }
}

void emptyDirFd(int fd, RemoveContext &ctx, int depth)
{
    DIR *dir = ::fdopendir(fd);
    if (!dir) {
        ::close(fd);
        ++ctx.failures;
        return;
    }
    std::vector<QByteArray> names;
    while (dirent *entry = ::readdir(dir)) {
        const char *n = entry->d_name;
        if (n[0] == '.' && (n[1] == '\0' || (n[1] == '.' && n[2] == '\0')))
            continue;
        names.emplace_back(n);
    }
    for (const QByteArray &n : names)
        removeEntryAt(::dirfd(dir), n.constData(), ctx, depth);
    ::closedir(dir);
}
} // namespace

qint64 removeTree(const QString &path, bool removeSelf, int *failures)
{
    if (failures)
        *failures = 0;
    const QByteArray encoded = QFile::encodeName(path);
    struct stat st;
    if (::lstat(encoded.constData(), &st) != 0 || !S_ISDIR(st.st_mode))
        return 0;
    if ((st.st_mode & S_IRWXU) != S_IRWXU)
        ::chmod(encoded.constData(), (st.st_mode & 07777) | S_IRWXU);
    const int fd = ::open(encoded.constData(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
    if (fd < 0) {
        if (failures)
            *failures = 1;
        return 0;
    }
    RemoveContext ctx;
    ctx.device = st.st_dev;
    emptyDirFd(fd, ctx, 0);
    if (removeSelf) {
        if (::rmdir(encoded.constData()) == 0)
            ctx.freed += qint64(st.st_blocks) * 512;
        else
            ++ctx.failures;
    }
    if (failures)
        *failures = ctx.failures;
    return ctx.freed;
}

// ======================================================================
// in use

QList<Running> runningProcesses(const QString &procDir)
{
    QList<Running> list;
    const QStringList pids = QDir(procDir).entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    const QString self = QString::number(QCoreApplication::applicationPid());
    for (const QString &pid : pids) {
        bool numeric = false;
        pid.toInt(&numeric);
        if (!numeric || pid == self)
            continue;
        const QString base = procDir + QLatin1Char('/') + pid;
        QFile comm(base + QStringLiteral("/comm"));
        if (!comm.open(QIODevice::ReadOnly))
            continue;
        Running r;
        r.comm = QString::fromUtf8(comm.readAll()).trimmed();
        QFile cmd(base + QStringLiteral("/cmdline"));
        if (cmd.open(QIODevice::ReadOnly)) {
            QByteArray data = cmd.read(8192);
            data.replace('\0', ' ');
            r.cmdline = QString::fromUtf8(data).trimmed();
        }
        r.exe = QFile::symLinkTarget(base + QStringLiteral("/exe"));
        list.append(r);
    }
    return list;
}

QString busyReason(const Item &item, const QList<Running> &running)
{
    for (const Running &r : running) {
        if (item.busyComms.contains(r.comm))
            return r.comm;
        for (const QString &part : item.busyCmdline) {
            if (!part.isEmpty() && r.cmdline.contains(part))
                return r.comm;
        }
        for (const QString &prefix : item.busyExePrefixes) {
            if (!prefix.isEmpty() && !r.exe.isEmpty() && (r.exe == prefix || r.exe.startsWith(prefix + QLatin1Char('/'))))
                return r.comm;
        }
    }
    return QString();
}

// ======================================================================
// versions

static QList<qint64> numbersOf(const QString &text)
{
    QList<qint64> numbers;
    static const QRegularExpression re(QStringLiteral("[0-9]+"));
    auto it = re.globalMatch(text);
    while (it.hasNext())
        numbers.append(it.next().captured(0).toLongLong());
    return numbers;
}

int compareVersions(const QString &a, const QString &b)
{
    const QList<qint64> x = numbersOf(a), y = numbersOf(b);
    for (int i = 0; i < qMax(x.size(), y.size()); ++i) {
        const qint64 p = i < x.size() ? x.at(i) : -1;
        const qint64 q = i < y.size() ? y.at(i) : -1;
        if (p != q)
            return p < q ? -1 : 1;
    }
    return a == b ? 0 : (a < b ? -1 : 1);
}

static QString newest(const QStringList &versions)
{
    QString best;
    for (const QString &v : versions) {
        if (best.isEmpty() || compareVersions(v, best) > 0)
            best = v;
    }
    return best;
}

// Installed versions matching a prefix ("20" -> v20.x.y, "3.12" -> 3.12.x)
static QString newestWithPrefix(const QStringList &installed, const QString &prefix)
{
    QStringList matches;
    for (const QString &v : installed) {
        if (v == prefix || v.startsWith(prefix + QLatin1Char('.')))
            matches.append(v);
    }
    return newest(matches);
}

QString resolveNvmAlias(const QString &nvmDir, const QString &alias, const QStringList &installed)
{
    QString a = alias.trimmed();
    for (int depth = 0; depth < 6 && !a.isEmpty(); ++depth) {
        if (a == QLatin1String("node") || a == QLatin1String("stable"))
            return newest(installed);
        if (a.startsWith(QLatin1String("lts/"))) {
            const QString name = a.mid(4);
            if (name.contains(QLatin1Char('/')) || name == QLatin1String(".."))
                return QString();
            a = readText(nvmDir + QStringLiteral("/alias/lts/") + name).trimmed();
            continue;
        }
        const QString v = a.startsWith(QLatin1Char('v')) ? a : QLatin1Char('v') + a;
        if (installed.contains(v))
            return v;
        const QString prefixed = newestWithPrefix(installed, v);
        if (!prefixed.isEmpty())
            return prefixed;
        // Another alias (nvm alias myproject 18)
        if (a.contains(QLatin1Char('/')) || a.startsWith(QLatin1Char('.')))
            return QString();
        const QString next = readText(nvmDir + QStringLiteral("/alias/") + a).trimmed();
        if (next == a)
            return QString();
        a = next;
    }
    return QString();
}

static QString versionUnder(const QString &resolved, const QString &base, const QStringList &installed)
{
    if (resolved.isEmpty())
        return QString();
    for (const QString &v : installed) {
        const QString dir = base + QLatin1Char('/') + v;
        if (resolved == dir || resolved.startsWith(dir + QLatin1Char('/')))
            return v;
    }
    return QString();
}

QStringList nvmProtected(const QString &nvmDir, const QStringList &installed, const QString &resolvedNode,
                         const QString &nvmBin)
{
    QStringList keep;
    const QString base = canonical(nvmDir) + QStringLiteral("/versions/node");
    const QString rawBase = nvmDir + QStringLiteral("/versions/node");
    for (const QString &candidate : {resolvedNode, canonical(nvmBin), nvmBin}) {
        QString v = versionUnder(candidate, base, installed);
        if (v.isEmpty())
            v = versionUnder(candidate, rawBase, installed);
        if (!v.isEmpty() && !keep.contains(v))
            keep.append(v);
    }
    const QString defaultFile = nvmDir + QStringLiteral("/alias/default");
    if (isRealFile(defaultFile)) {
        QString v = resolveNvmAlias(nvmDir, readText(defaultFile), installed);
        if (v.isEmpty())
            v = newest(installed);   // cannot tell: keep the newest
        if (!v.isEmpty() && !keep.contains(v))
            keep.append(v);
    }
    return keep;
}

QStringList gvmProtected(const QString &gvmRoot, const QStringList &installed, const QString &resolvedGo,
                         const QString &goroot)
{
    QStringList keep;
    const QString base = canonical(gvmRoot) + QStringLiteral("/gos");
    const QString rawBase = gvmRoot + QStringLiteral("/gos");
    for (const QString &candidate : {resolvedGo, canonical(goroot), goroot}) {
        QString v = versionUnder(candidate, base, installed);
        if (v.isEmpty())
            v = versionUnder(candidate, rawBase, installed);
        if (!v.isEmpty() && !keep.contains(v))
            keep.append(v);
    }
    // gvm use go1.21 --default writes environments/default
    static const QRegularExpression re(QStringLiteral("gvm_go_name=[\"']?([^\"'\\s;]+)"));
    const auto m = re.match(readText(gvmRoot + QStringLiteral("/environments/default")));
    if (m.hasMatch() && installed.contains(m.captured(1)) && !keep.contains(m.captured(1)))
        keep.append(m.captured(1));
    return keep;
}

QStringList pyenvProtected(const QString &pyenvRoot, const QStringList &installed, const QString &pyenvVersionEnv,
                           const QString &resolvedPython)
{
    QStringList keep;
    auto add = [&](const QString &name) {
        const QString n = name.trimmed();
        if (n.isEmpty() || n == QLatin1String("system"))
            return;
        QString v = installed.contains(n) ? n : newestWithPrefix(installed, n);
        if (!v.isEmpty() && !keep.contains(v))
            keep.append(v);
    };
    // Global: one or more names, one per line or space separated; # comments
    for (QString line : readText(pyenvRoot + QStringLiteral("/version")).split(QLatin1Char('\n'))) {
        line = line.section(QLatin1Char('#'), 0, 0);
        for (const QString &name : line.split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts))
            add(name);
    }
    for (const QString &name : pyenvVersionEnv.split(QLatin1Char(':'), Qt::SkipEmptyParts))
        add(name);
    QString v = versionUnder(resolvedPython, canonical(pyenvRoot) + QStringLiteral("/versions"), installed);
    if (v.isEmpty())
        v = versionUnder(resolvedPython, pyenvRoot + QStringLiteral("/versions"), installed);
    if (!v.isEmpty() && !keep.contains(v))
        keep.append(v);
    return keep;
}

QStringList rustupProtected(const QString &settingsToml, const QStringList &installed, const QString &envToolchain)
{
    QStringList wanted;
    bool inOverrides = false;
    bool sawDefault = false;
    static const QRegularExpression keyValue(QStringLiteral("^\\s*(\"[^\"]*\"|[A-Za-z0-9_.-]+)\\s*=\\s*\"([^\"]*)\""));
    for (const QString &raw : settingsToml.split(QLatin1Char('\n'))) {
        const QString line = raw.trimmed();
        if (line.startsWith(QLatin1Char('['))) {
            inOverrides = line == QLatin1String("[overrides]");
            continue;
        }
        const auto m = keyValue.match(line);
        if (!m.hasMatch())
            continue;
        if (inOverrides) {
            wanted.append(m.captured(2));
        } else if (m.captured(1) == QLatin1String("default_toolchain")) {
            wanted.append(m.captured(2));
            sawDefault = true;
        }
    }
    if (!envToolchain.isEmpty())
        wanted.append(envToolchain);
    // Without a known default we cannot tell which one is used: keep them all
    if (!sawDefault)
        return installed;
    QStringList keep;
    for (const QString &t : installed) {
        for (const QString &w : std::as_const(wanted)) {
            if (!w.isEmpty() && (t == w || t.startsWith(w + QLatin1Char('-')))) {
                keep.append(t);
                break;
            }
        }
    }
    return keep;
}

bool parseJetBrainsDir(const QString &name, JetBrainsDir *out)
{
    static const QRegularExpression re(QStringLiteral("^([A-Za-z][A-Za-z-]*?)(20[0-9]{2})\\.([0-9]+)$"));
    const auto m = re.match(name);
    if (!m.hasMatch())
        return false;
    if (out) {
        out->product = m.captured(1);
        out->year = m.captured(2).toInt();
        out->minor = m.captured(3).toInt();
    }
    return true;
}

QStringList oldJetBrainsDirs(const QStringList &names)
{
    QHash<QString, JetBrainsDir> newestOf;
    for (const QString &n : names) {
        JetBrainsDir d;
        if (!parseJetBrainsDir(n, &d))
            continue;
        auto it = newestOf.find(d.product);
        if (it == newestOf.end() || d.year > it->year || (d.year == it->year && d.minor > it->minor))
            newestOf.insert(d.product, d);
    }
    QStringList old;
    for (const QString &n : names) {
        JetBrainsDir d;
        if (!parseJetBrainsDir(n, &d))
            continue;
        const JetBrainsDir best = newestOf.value(d.product);
        if ((d.year != best.year || d.minor != best.minor) && !old.contains(n))
            old.append(n);
    }
    std::sort(old.begin(), old.end());
    return old;
}

qint64 parsePaccacheSaved(const QString &output)
{
    static const QRegularExpression re(QStringLiteral("disk space saved: ([0-9.]+) ([KMGT]?i?B)"));
    const auto m = re.match(output);
    if (!m.hasMatch())
        return -1;
    double value = m.captured(1).toDouble();
    const QString unit = m.captured(2);
    const char *units = "KMGT";
    for (int i = 0; i < 4; ++i) {
        value *= 1024.0;
        if (unit.startsWith(QLatin1Char(units[i])))
            return qint64(value);
    }
    return qint64(m.captured(1).toDouble());
}

qint64 parseReclaimed(const QString &output)
{
    static const QRegularExpression re(QStringLiteral("Total reclaimed space:\\s*([0-9.,]+\\s*[kKMGTP]?i?B)"));
    const auto m = re.match(output);
    return m.hasMatch() ? Hogs::parseHumanSize(m.captured(1)) : -1;
}

// ======================================================================
// detection

namespace {

struct Ctx {
    const Env &env;
    Progress *progress;
    QList<Item> items;
    QString cacheHome;
    QString dataHome;

    void label(const QString &text)
    {
        if (progress)
            progress->setLabel(text);
    }
    bool cancelled() const { return progress && progress->cancel.load(); }
};

Item make(const QString &group, const QString &id, const QString &title, const QString &description,
          const QString &comesBack, const QString &path)
{
    Item item;
    item.group = group;
    item.id = id;
    item.title = title;
    item.description = description;
    item.comesBack = comesBack;
    item.path = path;
    return item;
}

// A cache folder emptied in place: the tool's cleaner when installed
void addCache(Ctx &c, Item item, const QStringList &paths, const QString &root, const QString &program = QString(),
              const QStringList &args = {})
{
    QStringList existing;
    for (const QString &p : paths) {
        if (isRealDir(p) || QFileInfo(p).isSymLink())
            existing.append(p);
    }
    if (existing.isEmpty())
        return;
    item.paths = existing;
    if (item.path.isEmpty())
        item.path = existing.first();
    item.roots = {root};
    item.fallback = Action::EmptyDirs;
    item.programName = program;
    item.args = args;
    item.program = program.isEmpty() ? QString() : c.env.findExecutable(program);
    if (!item.program.isEmpty()) {
        item.action = Action::Command;
        item.commandText = (QStringList {program} + args).join(QLatin1Char(' '));
    } else {
        item.action = Action::EmptyDirs;
    }
    c.items.append(item);
}

void detectPackages(Ctx &c)
{
    c.label(tr_("Pacotes (pacman e AUR)"));
    const Env &env = c.env;
    const QString paccache = env.findExecutable(QStringLiteral("paccache"));
    if (!paccache.isEmpty() && QFileInfo(env.pacmanCache).isDir()) {
        struct Step {
            const char *id;
            const char *dryRun;
            const char *real;
            const char *title;
            const char *description;
            const char *comesBack;
        };
        static const Step steps[] = {
            {"pacman-old", "-dk1", "-rk1", QT_TRANSLATE_NOOP("DevCaches", "Versões antigas de pacotes (pacman)"),
             QT_TRANSLATE_NOOP("DevCaches", "Cada atualização guarda o pacote baixado. Fica só a versão instalada de cada programa."),
             QT_TRANSLATE_NOOP("DevCaches", "Não fazem falta: só serviriam para voltar a uma versão anterior.")},
            {"pacman-uninstalled", "-duk0", "-ruk0", QT_TRANSLATE_NOOP("DevCaches", "Pacotes desinstalados (pacman)"),
             QT_TRANSLATE_NOOP("DevCaches", "Pacotes de programas que você já removeu e que continuam no cache."),
             QT_TRANSLATE_NOOP("DevCaches", "São baixados de novo se você reinstalar o programa.")},
        };
        for (const Step &s : steps) {
            const CommandOutput out = runCommand(env, paccache, {QLatin1String(s.dryRun), QStringLiteral("--nocolor")},
                                                 env.commandTimeoutMs, true);
            const qint64 saved = out.finished ? parsePaccacheSaved(out.out) : -1;
            if (saved <= 0)
                continue;
            Item item = make(QStringLiteral("packages"), QLatin1String(s.id), tr_(s.title), tr_(s.description),
                             tr_(s.comesBack), env.pacmanCache);
            item.action = Action::Root;
            item.rootStep = QLatin1String(s.id);
            item.commandText = QStringLiteral("paccache ") + QLatin1String(s.real);
            item.size = saved;
            item.sizeFromTool = true;
            item.defaultSelected = true;
            item.order = 5;
            if (QFileInfo::exists(env.pacmanLock))
                item.busyReason = QStringLiteral("pacman");
            c.items.append(item);
        }
    }

    for (const char *tool : {"yay", "paru"}) {
        const QString dir = c.cacheHome + QLatin1Char('/') + QLatin1String(tool);
        Item item = make(QStringLiteral("packages"), QLatin1String(tool),
                         tr_("Cache de compilação do AUR (%1)").arg(QLatin1String(tool)),
                         tr_("Fontes baixadas e pacotes compilados de programas do AUR instalados pelo %1.").arg(QLatin1String(tool)),
                         tr_("Volta sozinho: é baixado e compilado de novo na próxima atualização do pacote."), dir);
        item.defaultSelected = true;
        item.busyComms = {QLatin1String(tool), QStringLiteral("makepkg")};
        addCache(c, item, {dir}, dir);
    }
}

struct DfRow {
    QString type;
    qint64 size = -1;
    qint64 reclaimable = -1;
};

QList<DfRow> containerDf(const Env &env, const QString &exe, bool *ok)
{
    QList<DfRow> rows;
    const CommandOutput out = runCommand(env, exe, {QStringLiteral("system"), QStringLiteral("df"), QStringLiteral("--format"),
                                                    QStringLiteral("{{.Type}}\t{{.Size}}\t{{.Reclaimable}}")},
                                         env.commandTimeoutMs);
    *ok = out.finished && out.exitCode == 0;
    if (!*ok)
        return rows;
    for (const QString &line : out.out.split(QLatin1Char('\n'), Qt::SkipEmptyParts)) {
        const QStringList cols = line.split(QLatin1Char('\t'));
        if (cols.size() < 3)
            continue;
        DfRow row;
        row.type = cols.at(0).trimmed();
        row.size = Hogs::parseHumanSize(cols.at(1));
        // "1.2GB (50%)"
        row.reclaimable = Hogs::parseHumanSize(cols.at(2).trimmed().section(QLatin1Char(' '), 0, 0));
        rows.append(row);
    }
    return rows;
}

void detectContainers(Ctx &c, const QString &tool)
{
    const Env &env = c.env;
    const QString exe = env.findExecutable(tool);
    if (exe.isEmpty())
        return;
    c.label(tool == QLatin1String("docker") ? tr_("Docker") : tr_("Podman"));
    bool ok = false;
    const QList<DfRow> rows = containerDf(env, exe, &ok);
    const QString name = tool == QLatin1String("docker") ? QStringLiteral("Docker") : QStringLiteral("Podman");
    const QString storage = tool == QLatin1String("podman") ? c.dataHome + QStringLiteral("/containers/storage") : QString();
    if (!ok) {
        // Installed but the daemon is not reachable (stopped, or not in the docker group)
        if (tool == QLatin1String("docker") && QFileInfo::exists(QStringLiteral("/var/lib/docker"))) {
            Item item = make(tool, tool + QStringLiteral("-unavailable"), tr_("Docker (sem acesso)"),
                             tr_("O Docker está instalado, mas o serviço não respondeu: ele está parado ou seu usuário não está "
                                 "no grupo docker. Nada é medido nem limpo."),
                             QString(), QStringLiteral("/var/lib/docker"));
            item.sizeFromTool = true;   // root-only folder: not measured
            c.items.append(item);
        }
        return;
    }
    for (const DfRow &row : rows) {
        Item item;
        if (row.type == QLatin1String("Images") && row.reclaimable > 0) {
            item = make(tool, tool + QStringLiteral("-images"), tr_("Imagens sem uso (%1)").arg(name),
                        tr_("Imagens que nenhum contêiner usa, nem os parados. Imagens de contêineres que você ainda tem ficam."),
                        tr_("Baixadas de novo (pull) quando você precisar delas."), storage);
            item.args = {QStringLiteral("image"), QStringLiteral("prune"), QStringLiteral("-a"), QStringLiteral("-f")};
            item.defaultSelected = true;
            item.order = 10;
        } else if (row.type == QLatin1String("Containers") && row.reclaimable > 0) {
            item = make(tool, tool + QStringLiteral("-containers"), tr_("Contêineres parados (%1)").arg(name),
                        tr_("Contêineres que não estão rodando. Os que estão rodando nunca são tocados."),
                        tr_("Não voltam: só recriando a partir da imagem."), storage);
            item.args = {QStringLiteral("container"), QStringLiteral("prune"), QStringLiteral("-f")};
            item.warning = tr_("Arquivos criados dentro de um contêiner parado somem com ele.");
            item.reinstall = true;
            item.order = 20;
        } else if (row.type == QLatin1String("Build Cache") && row.reclaimable > 0) {
            item = make(tool, tool + QStringLiteral("-build"), tr_("Cache de build (%1)").arg(name),
                        tr_("Camadas guardadas pelo build de imagens para acelerar o próximo."),
                        tr_("Volta sozinho no próximo build (que fica mais lento uma vez)."), storage);
            item.args = {QStringLiteral("builder"), QStringLiteral("prune"), QStringLiteral("-a"), QStringLiteral("-f")};
            item.defaultSelected = true;
            item.order = 30;
        } else if (row.type == QLatin1String("Local Volumes") && row.size > 0) {
            item = make(tool, tool + QStringLiteral("-volumes"), tr_("Volumes (%1)").arg(name),
                        tr_("Dados guardados pelos contêineres fora das imagens. Só listados: remova pelo próprio %1 o que "
                            "tiver certeza.").arg(name),
                        tr_("Não voltam: o que estava neles se perde."), storage);
            item.warning = tr_("Volumes podem conter dados de bancos de dados.");
            item.reinstall = true;
            item.size = row.size;
            item.sizeFromTool = true;
            const CommandOutput vols = runCommand(env, exe, {QStringLiteral("volume"), QStringLiteral("ls"), QStringLiteral("--format"),
                                                             QStringLiteral("{{.Name}}")}, env.commandTimeoutMs);
            const CommandOutput dangling = runCommand(env, exe, {QStringLiteral("volume"), QStringLiteral("ls"), QStringLiteral("-q"),
                                                                 QStringLiteral("-f"), QStringLiteral("dangling=true")},
                                                      env.commandTimeoutMs);
            const QStringList unused = dangling.out.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
            int count = 0;
            for (const QString &vol : vols.out.split(QLatin1Char('\n'), Qt::SkipEmptyParts)) {
                if (++count > 30)
                    break;
                item.children.append(QVariantMap {{QStringLiteral("name"), vol.trimmed()}, {QStringLiteral("size"), -1.0},
                                                  {QStringLiteral("detail"), unused.contains(vol.trimmed())
                                                       ? tr_("nenhum contêiner usa") : tr_("usado por um contêiner")}});
            }
            c.items.append(item);
            continue;
        } else {
            continue;
        }
        item.action = Action::Command;
        item.programName = tool;
        item.program = exe;
        item.commandText = (QStringList {tool} + item.args).join(QLatin1Char(' '));
        item.size = row.reclaimable;
        item.sizeFromTool = true;
        c.items.append(item);
    }
}

void detectGo(Ctx &c)
{
    const Env &env = c.env;
    c.label(tr_("Go"));
    const QString go = env.findExecutable(QStringLiteral("go"));
    QString gocache = env.var(QStringLiteral("GOCACHE"));
    QString gomodcache = env.var(QStringLiteral("GOMODCACHE"));
    QString toolCache, toolModCache;
    if (!go.isEmpty()) {
        const CommandOutput out = runCommand(env, go, {QStringLiteral("env"), QStringLiteral("GOCACHE"), QStringLiteral("GOMODCACHE")}, 8000);
        if (out.finished && out.exitCode == 0) {
            const QStringList lines = out.out.split(QLatin1Char('\n'));
            toolCache = stripSlash(lines.value(0).trimmed());
            toolModCache = stripSlash(lines.value(1).trimmed());
        }
    }
    if (!toolCache.isEmpty())
        gocache = toolCache;
    if (gocache.isEmpty() || gocache == QLatin1String("off"))
        gocache = c.cacheHome + QStringLiteral("/go-build");

    Item build = make(QStringLiteral("go"), QStringLiteral("go-build"), tr_("Cache de compilação do Go"),
                      tr_("Resultados de compilações e testes anteriores, para compilar mais rápido."),
                      tr_("Volta sozinho: o próximo go build recria o que precisar."), gocache);
    build.defaultSelected = true;
    build.busyComms = {QStringLiteral("go")};
    addCache(c, build, {gocache}, gocache, (!toolCache.isEmpty() && toolCache == gocache) ? QStringLiteral("go") : QString(),
             {QStringLiteral("clean"), QStringLiteral("-cache")});

    QStringList mods;
    auto addMod = [&mods](const QString &p) {
        const QString clean = stripSlash(QDir::cleanPath(p));
        if (!p.isEmpty() && !mods.contains(clean))
            mods.append(clean);
    };
    addMod(toolModCache);
    addMod(gomodcache);
    const QString gopath = env.var(QStringLiteral("GOPATH")).section(QLatin1Char(':'), 0, 0);
    if (!gopath.isEmpty())
        addMod(gopath + QStringLiteral("/pkg/mod"));
    addMod(env.home + QStringLiteral("/go/pkg/mod"));
    bool first = true;
    for (const QString &mod : std::as_const(mods)) {
        if (!isRealDir(mod) && !QFileInfo(mod).isSymLink())
            continue;
        Item item = make(QStringLiteral("go"), first ? QStringLiteral("go-mod") : QStringLiteral("go-mod:") + mod,
                         tr_("Módulos baixados pelo Go"),
                         tr_("Código-fonte das dependências (módulos) baixadas pelo Go."),
                         tr_("Volta sozinho: o Go baixa de novo os módulos que um projeto usar."), mod);
        item.defaultSelected = true;
        item.busyComms = {QStringLiteral("go")};
        const bool tool = !toolModCache.isEmpty() && toolModCache == mod;
        addCache(c, item, {mod}, mod, tool ? QStringLiteral("go") : QString(), {QStringLiteral("clean"), QStringLiteral("-modcache")});
        first = false;
    }

    // gvm: Go versions side by side
    const QString gvmRoot = env.var(QStringLiteral("GVM_ROOT")).isEmpty() ? env.home + QStringLiteral("/.gvm")
                                                                          : stripSlash(env.var(QStringLiteral("GVM_ROOT")));
    const QString gos = gvmRoot + QStringLiteral("/gos");
    if (!isRealDir(gos))
        return;
    const QStringList installed = subdirs(gos);
    const QStringList keep = gvmProtected(gvmRoot, installed, canonical(go), env.var(QStringLiteral("GOROOT")));
    QString gvm = env.findExecutable(QStringLiteral("gvm"));
    if (gvm.isEmpty() && QFileInfo(gvmRoot + QStringLiteral("/bin/gvm")).isExecutable())
        gvm = gvmRoot + QStringLiteral("/bin/gvm");
    for (const QString &v : installed) {
        const QString dir = gos + QLatin1Char('/') + v;
        Item item = make(QStringLiteral("go"), QStringLiteral("gvm:") + v, tr_("Go %1 (gvm)").arg(v.startsWith(QLatin1String("go")) ? v.mid(2) : v),
                         tr_("Uma versão do Go instalada pelo gvm, com os pacotes dela."),
                         tr_("Não volta sozinha: pode precisar reinstalar (gvm install %1).").arg(v), dir);
        item.paths = {dir};
        const QString pkgset = gvmRoot + QStringLiteral("/pkgsets/") + v;
        if (isRealDir(pkgset))
            item.paths.append(pkgset);
        item.roots = {gvmRoot};
        item.reinstall = true;
        item.current = keep.contains(v);
        if (item.current)
            item.comesBack = tr_("Em uso: a versão atual ou a padrão do gvm. Não é oferecida.");
        item.busyExePrefixes = {canonical(dir)};
        item.fallback = Action::RemoveDirs;
        item.programName = QStringLiteral("gvm");
        if (!gvm.isEmpty()) {
            item.action = Action::Command;
            item.program = gvm;
            item.args = {QStringLiteral("uninstall"), v};
            item.commandText = QStringLiteral("gvm uninstall ") + v;
        } else {
            item.action = Action::RemoveDirs;
        }
        c.items.append(item);
    }
}

void detectNode(Ctx &c)
{
    const Env &env = c.env;
    c.label(tr_("Node.js"));
    const QString npmCache = env.var(QStringLiteral("npm_config_cache")).isEmpty()
        ? env.home + QStringLiteral("/.npm") : stripSlash(env.var(QStringLiteral("npm_config_cache")));
    {
        Item item = make(QStringLiteral("node"), QStringLiteral("npm-cache"), tr_("Cache do npm"),
                         tr_("Pacotes baixados pelo npm, guardados para instalar mais rápido."),
                         tr_("Volta sozinho: o npm baixa de novo no próximo npm install."), npmCache + QStringLiteral("/_cacache"));
        item.defaultSelected = true;
        item.busyComms = {QStringLiteral("npm")};
        addCache(c, item, {npmCache + QStringLiteral("/_cacache")}, npmCache, QStringLiteral("npm"),
                 {QStringLiteral("cache"), QStringLiteral("clean"), QStringLiteral("--force")});
    }
    {
        const QString dir = c.cacheHome + QStringLiteral("/yarn");
        Item item = make(QStringLiteral("node"), QStringLiteral("yarn"), tr_("Cache do Yarn"),
                         tr_("Pacotes baixados pelo Yarn."), tr_("Volta sozinho no próximo yarn install."), dir);
        item.defaultSelected = true;
        item.busyComms = {QStringLiteral("yarn")};
        addCache(c, item, {dir}, dir, QStringLiteral("yarn"), {QStringLiteral("cache"), QStringLiteral("clean")});
        const QString berry = env.home + QStringLiteral("/.yarn/berry/cache");
        Item b = make(QStringLiteral("node"), QStringLiteral("yarn-berry"), tr_("Cache do Yarn (Berry)"),
                      tr_("Pacotes baixados pelo Yarn 2 ou mais novo."), tr_("Volta sozinho no próximo yarn install."), berry);
        b.defaultSelected = true;
        b.busyComms = {QStringLiteral("yarn")};
        addCache(c, b, {berry}, berry);
    }
    {
        const QString dir = c.cacheHome + QStringLiteral("/pnpm");
        Item item = make(QStringLiteral("node"), QStringLiteral("pnpm-cache"), tr_("Cache do pnpm"),
                         tr_("Informações de pacotes baixadas pelo pnpm."), tr_("Volta sozinho no próximo pnpm install."), dir);
        item.defaultSelected = true;
        item.busyComms = {QStringLiteral("pnpm")};
        addCache(c, item, {dir}, dir);
        const QString store = env.var(QStringLiteral("PNPM_HOME")).isEmpty()
            ? c.dataHome + QStringLiteral("/pnpm/store") : stripSlash(env.var(QStringLiteral("PNPM_HOME"))) + QStringLiteral("/store");
        Item s = make(QStringLiteral("node"), QStringLiteral("pnpm-store"), tr_("Loja de pacotes do pnpm"),
                      tr_("Todos os pacotes que o pnpm já instalou, compartilhados entre os projetos. Com o pnpm instalado, "
                          "só os que nenhum projeto usa são apagados (pnpm store prune)."),
                      tr_("Volta sozinho no próximo pnpm install."), store);
        s.defaultSelected = true;
        s.busyComms = {QStringLiteral("pnpm")};
        s.upTo = !env.findExecutable(QStringLiteral("pnpm")).isEmpty();
        addCache(c, s, {store}, store, QStringLiteral("pnpm"), {QStringLiteral("store"), QStringLiteral("prune")});
    }
    {
        const QString dir = c.cacheHome + QStringLiteral("/node-gyp");
        Item item = make(QStringLiteral("node"), QStringLiteral("node-gyp"), tr_("Cabeçalhos do node-gyp"),
                         tr_("Arquivos do Node baixados para compilar módulos nativos."),
                         tr_("Volta sozinho quando um módulo nativo for compilado de novo."), dir);
        item.defaultSelected = true;
        addCache(c, item, {dir}, dir);
    }

    // nvm
    const QString nvmDir = env.var(QStringLiteral("NVM_DIR")).isEmpty() ? env.home + QStringLiteral("/.nvm")
                                                                        : stripSlash(env.var(QStringLiteral("NVM_DIR")));
    const QString versions = nvmDir + QStringLiteral("/versions/node");
    if (!isRealDir(versions))
        return;
    const QStringList installed = subdirs(versions);
    const QStringList keep = nvmProtected(nvmDir, installed, canonical(env.findExecutable(QStringLiteral("node"))),
                                          env.var(QStringLiteral("NVM_BIN")));
    const QString nvm = env.findExecutable(QStringLiteral("nvm"));
    const QString bash = env.findExecutable(QStringLiteral("bash"));
    const bool nvmScript = isRealFile(nvmDir + QStringLiteral("/nvm.sh")) && !bash.isEmpty();
    for (const QString &v : installed) {
        const QString dir = versions + QLatin1Char('/') + v;
        Item item = make(QStringLiteral("node"), QStringLiteral("nvm:") + v, tr_("Node %1 (nvm)").arg(v),
                         tr_("Uma versão do Node.js instalada pelo nvm, com os pacotes globais dela."),
                         tr_("Não volta sozinha: pode precisar reinstalar (nvm install %1).").arg(v), dir);
        item.paths = {dir};
        item.roots = {nvmDir};
        item.reinstall = true;
        item.current = keep.contains(v);
        if (item.current)
            item.comesBack = tr_("Em uso: a versão atual ou a padrão do nvm. Não é oferecida.");
        item.busyExePrefixes = {canonical(dir)};
        item.fallback = Action::RemoveDirs;
        item.programName = QStringLiteral("nvm");
        item.commandText = QStringLiteral("nvm uninstall ") + v;
        if (!nvm.isEmpty()) {
            item.action = Action::Command;
            item.program = nvm;
            item.args = {QStringLiteral("uninstall"), v};
        } else if (nvmScript) {
            // nvm is a shell function: load it in a non-interactive bash
            item.action = Action::Command;
            item.programName = QStringLiteral("bash");
            item.program = bash;
            item.args = {QStringLiteral("-c"), QStringLiteral("export NVM_DIR=\"$1\"; . \"$1/nvm.sh\" && nvm uninstall \"$2\""),
                         QStringLiteral("nvm"), nvmDir, v};
        } else {
            item.action = Action::RemoveDirs;
            item.commandText.clear();
        }
        c.items.append(item);
    }
}

void detectPython(Ctx &c)
{
    const Env &env = c.env;
    c.label(tr_("Python"));
    {
        const QString dir = c.cacheHome + QStringLiteral("/pip");
        Item item = make(QStringLiteral("python"), QStringLiteral("pip"), tr_("Cache do pip"),
                         tr_("Pacotes Python baixados e compilados pelo pip."),
                         tr_("Volta sozinho: o pip baixa de novo no próximo pip install."), dir);
        item.defaultSelected = true;
        item.busyComms = {QStringLiteral("pip"), QStringLiteral("pip3")};
        const bool hasPip = !env.findExecutable(QStringLiteral("pip")).isEmpty();
        addCache(c, item, {dir}, dir, hasPip ? QStringLiteral("pip") : QStringLiteral("pip3"),
                 {QStringLiteral("cache"), QStringLiteral("purge")});
    }
    {
        const QString root = c.cacheHome + QStringLiteral("/pypoetry");
        Item item = make(QStringLiteral("python"), QStringLiteral("poetry"), tr_("Cache do Poetry"),
                         tr_("Pacotes baixados pelo Poetry. Os ambientes virtuais dele (virtualenvs) ficam."),
                         tr_("Volta sozinho no próximo poetry install."), root);
        item.defaultSelected = true;
        item.busyComms = {QStringLiteral("poetry")};
        addCache(c, item, {root + QStringLiteral("/cache"), root + QStringLiteral("/artifacts")}, root);
    }
    {
        const QString dir = env.var(QStringLiteral("UV_CACHE_DIR")).isEmpty() ? c.cacheHome + QStringLiteral("/uv")
                                                                              : stripSlash(env.var(QStringLiteral("UV_CACHE_DIR")));
        Item item = make(QStringLiteral("python"), QStringLiteral("uv"), tr_("Cache do uv"),
                         tr_("Pacotes Python baixados pelo uv."), tr_("Volta sozinho: o uv baixa de novo quando precisar."), dir);
        item.defaultSelected = true;
        item.busyComms = {QStringLiteral("uv")};
        addCache(c, item, {dir}, dir, QStringLiteral("uv"), {QStringLiteral("cache"), QStringLiteral("clean")});
    }
    {
        const QString dir = c.cacheHome + QStringLiteral("/pipenv");
        Item item = make(QStringLiteral("python"), QStringLiteral("pipenv"), tr_("Cache do Pipenv"),
                         tr_("Pacotes baixados pelo Pipenv."), tr_("Volta sozinho no próximo pipenv install."), dir);
        item.defaultSelected = true;
        item.busyComms = {QStringLiteral("pipenv")};
        addCache(c, item, {dir}, dir);
    }

    // pyenv
    const QString root = env.var(QStringLiteral("PYENV_ROOT")).isEmpty() ? env.home + QStringLiteral("/.pyenv")
                                                                         : stripSlash(env.var(QStringLiteral("PYENV_ROOT")));
    const QString versions = root + QStringLiteral("/versions");
    if (!isRealDir(versions))
        return;
    const QStringList installed = subdirs(versions);   // virtualenv aliases are symlinks: skipped
    QString python = canonical(env.findExecutable(QStringLiteral("python")));
    if (python.isEmpty())
        python = canonical(env.findExecutable(QStringLiteral("python3")));
    const QStringList keep = pyenvProtected(root, installed, env.var(QStringLiteral("PYENV_VERSION")), python);
    QString pyenv = env.findExecutable(QStringLiteral("pyenv"));
    if (pyenv.isEmpty() && QFileInfo(root + QStringLiteral("/bin/pyenv")).isExecutable())
        pyenv = root + QStringLiteral("/bin/pyenv");
    for (const QString &v : installed) {
        const QString dir = versions + QLatin1Char('/') + v;
        Item item = make(QStringLiteral("python"), QStringLiteral("pyenv:") + v, tr_("Python %1 (pyenv)").arg(v),
                         tr_("Uma versão do Python instalada pelo pyenv, com os pacotes e ambientes dela."),
                         tr_("Não volta sozinha: pode precisar reinstalar (pyenv install %1).").arg(v), dir);
        item.paths = {dir};
        item.roots = {root};
        item.reinstall = true;
        item.current = keep.contains(v);
        if (item.current)
            item.comesBack = tr_("Em uso: a versão global ou atual do pyenv. Não é oferecida.");
        if (!subdirs(dir + QStringLiteral("/envs")).isEmpty())
            item.warning = tr_("Tem ambientes virtuais (pyenv virtualenv) que também serão apagados.");
        item.busyExePrefixes = {canonical(dir)};
        item.fallback = Action::RemoveDirs;
        item.programName = QStringLiteral("pyenv");
        if (!pyenv.isEmpty()) {
            item.action = Action::Command;
            item.program = pyenv;
            item.args = {QStringLiteral("uninstall"), QStringLiteral("-f"), v};
            item.commandText = QStringLiteral("pyenv uninstall -f ") + v;
        } else {
            item.action = Action::RemoveDirs;
        }
        c.items.append(item);
    }
}

void detectRust(Ctx &c)
{
    const Env &env = c.env;
    c.label(tr_("Rust"));
    const QString cargoHome = env.var(QStringLiteral("CARGO_HOME")).isEmpty() ? env.home + QStringLiteral("/.cargo")
                                                                              : stripSlash(env.var(QStringLiteral("CARGO_HOME")));
    {
        Item item = make(QStringLiteral("rust"), QStringLiteral("cargo"), tr_("Crates baixadas pelo cargo"),
                         tr_("Código das dependências (crates) baixadas do registro e do git, compactadas e extraídas."),
                         tr_("Volta sozinho: o cargo baixa de novo as crates no próximo build."), cargoHome + QStringLiteral("/registry"));
        item.defaultSelected = true;
        item.busyComms = {QStringLiteral("cargo"), QStringLiteral("rustc")};
        const bool cargoCache = !env.findExecutable(QStringLiteral("cargo-cache")).isEmpty();
        addCache(c, item,
                 {cargoHome + QStringLiteral("/registry/cache"), cargoHome + QStringLiteral("/registry/src"),
                  cargoHome + QStringLiteral("/git/checkouts"), cargoHome + QStringLiteral("/git/db")},
                 cargoHome, cargoCache ? QStringLiteral("cargo") : QString(),
                 {QStringLiteral("cache"), QStringLiteral("--remove-dir"),
                  QStringLiteral("git-db,git-repos,registry-sources,registry-crate-cache")});
    }

    const QString rustupHome = env.var(QStringLiteral("RUSTUP_HOME")).isEmpty() ? env.home + QStringLiteral("/.rustup")
                                                                                : stripSlash(env.var(QStringLiteral("RUSTUP_HOME")));
    const QString toolchains = rustupHome + QStringLiteral("/toolchains");
    if (!isRealDir(toolchains))
        return;
    const QStringList installed = subdirs(toolchains);
    const QStringList keep = rustupProtected(readText(rustupHome + QStringLiteral("/settings.toml")), installed,
                                             env.var(QStringLiteral("RUSTUP_TOOLCHAIN")));
    const QString rustup = env.findExecutable(QStringLiteral("rustup"));
    for (const QString &t : installed) {
        const QString dir = toolchains + QLatin1Char('/') + t;
        Item item = make(QStringLiteral("rust"), QStringLiteral("rustup:") + t, tr_("Rust %1 (rustup)").arg(t),
                         tr_("Um conjunto de ferramentas do Rust (toolchain) instalado pelo rustup."),
                         tr_("Não volta sozinho: pode precisar reinstalar (rustup toolchain install)."), dir);
        item.paths = {dir};
        item.roots = {rustupHome};
        item.reinstall = true;
        item.current = keep.contains(t);
        if (item.current)
            item.comesBack = tr_("Em uso: o padrão do rustup ou de um projeto. Não é oferecido.");
        item.busyExePrefixes = {canonical(dir)};
        item.fallback = Action::RemoveDirs;
        item.programName = QStringLiteral("rustup");
        if (!rustup.isEmpty()) {
            item.action = Action::Command;
            item.program = rustup;
            item.args = {QStringLiteral("toolchain"), QStringLiteral("uninstall"), t};
            item.commandText = QStringLiteral("rustup toolchain uninstall ") + t;
        } else {
            item.action = Action::RemoveDirs;
        }
        c.items.append(item);
    }
}

void detectJava(Ctx &c)
{
    const Env &env = c.env;
    c.label(tr_("Java e Android"));
    const QString gradleHome = env.var(QStringLiteral("GRADLE_USER_HOME")).isEmpty()
        ? env.home + QStringLiteral("/.gradle") : stripSlash(env.var(QStringLiteral("GRADLE_USER_HOME")));
    {
        Item item = make(QStringLiteral("java"), QStringLiteral("gradle"), tr_("Cache do Gradle"),
                         tr_("Dependências baixadas e resultados de builds do Gradle."),
                         tr_("Volta sozinho: o Gradle baixa de novo o que precisar no próximo build."), gradleHome + QStringLiteral("/caches"));
        item.defaultSelected = true;
        item.busyCmdline = {QStringLiteral("GradleDaemon"), QStringLiteral("GradleWrapperMain"), QStringLiteral("org.gradle.launcher")};
        addCache(c, item, {gradleHome + QStringLiteral("/caches")}, gradleHome);
    }
    {
        const QString repo = env.home + QStringLiteral("/.m2/repository");
        Item item = make(QStringLiteral("java"), QStringLiteral("maven"), tr_("Repositório local do Maven"),
                         tr_("Bibliotecas baixadas pelo Maven (~/.m2/repository)."),
                         tr_("Volta no próximo build, baixando tudo de novo."), repo);
        item.warning = tr_("As dependências serão baixadas de novo. Bibliotecas que você instalou com \"mvn install\" "
                           "precisam ser instaladas outra vez.");
        item.reinstall = true;
        item.busyCmdline = {QStringLiteral("plexus-classworlds")};
        addCache(c, item, {repo}, env.home + QStringLiteral("/.m2"));
    }

    // Android SDK: list only (removed through the SDK/Device Manager)
    QString sdk = env.var(QStringLiteral("ANDROID_HOME"));
    if (sdk.isEmpty())
        sdk = env.var(QStringLiteral("ANDROID_SDK_ROOT"));
    if (sdk.isEmpty())
        sdk = env.home + QStringLiteral("/Android/Sdk");
    sdk = stripSlash(sdk);
    const QString images = sdk + QStringLiteral("/system-images");
    for (const QString &api : subdirs(images)) {
        for (const QString &tag : subdirs(images + QLatin1Char('/') + api)) {
            for (const QString &abi : subdirs(images + QLatin1Char('/') + api + QLatin1Char('/') + tag)) {
                const QString dir = images + QLatin1Char('/') + api + QLatin1Char('/') + tag + QLatin1Char('/') + abi;
                Item item = make(QStringLiteral("java"), QStringLiteral("android-image:") + api + QLatin1Char('/') + tag + QLatin1Char('/') + abi,
                                 tr_("Imagem do emulador Android: %1 · %2 · %3").arg(api, tag, abi),
                                 tr_("Sistema usado pelo emulador para criar dispositivos virtuais. Remova pelo SDK Manager do "
                                     "Android Studio se não usar mais."),
                                 tr_("Volta baixando de novo pelo SDK Manager."), dir);
                item.paths = {dir};
                item.roots = {sdk};
                item.reinstall = true;
                c.items.append(item);
            }
        }
    }
    QString avdHome = env.var(QStringLiteral("ANDROID_AVD_HOME"));
    if (avdHome.isEmpty())
        avdHome = (env.var(QStringLiteral("ANDROID_USER_HOME")).isEmpty() ? env.home + QStringLiteral("/.android")
                                                                          : env.var(QStringLiteral("ANDROID_USER_HOME")))
            + QStringLiteral("/avd");
    for (const QString &avd : subdirs(avdHome)) {
        if (!avd.endsWith(QLatin1String(".avd")))
            continue;
        const QString dir = avdHome + QLatin1Char('/') + avd;
        Item item = make(QStringLiteral("java"), QStringLiteral("avd:") + avd, tr_("Dispositivo virtual Android: %1").arg(avd.chopped(4)),
                         tr_("Um dispositivo do emulador, com os apps e dados instalados nele. Remova pelo Device Manager do "
                             "Android Studio."),
                         tr_("Não volta: os dados do dispositivo se perdem."), dir);
        item.paths = {dir};
        item.roots = {avdHome};
        item.reinstall = true;
        c.items.append(item);
    }
}

void detectJetBrains(Ctx &c)
{
    c.label(tr_("JetBrains"));
    const QStringList bases = {c.dataHome + QStringLiteral("/JetBrains"), c.cacheHome + QStringLiteral("/JetBrains")};
    QStringList names;
    for (const QString &base : bases) {
        for (const QString &n : subdirs(base)) {
            if (parseJetBrainsDir(n, nullptr) && !names.contains(n))
                names.append(n);
        }
    }
    const QStringList old = oldJetBrainsDirs(names);
    QHash<QString, JetBrainsDir> newest;
    for (const QString &n : std::as_const(names)) {
        JetBrainsDir d;
        parseJetBrainsDir(n, &d);
        auto it = newest.find(d.product);
        if (it == newest.end() || d.year > it->year || (d.year == it->year && d.minor > it->minor))
            newest.insert(d.product, d);
    }
    for (const QString &n : old) {
        JetBrainsDir d;
        parseJetBrainsDir(n, &d);
        const JetBrainsDir best = newest.value(d.product);
        Item item = make(QStringLiteral("jetbrains"), QStringLiteral("jetbrains:") + n,
                         QStringLiteral("%1 %2.%3").arg(d.product).arg(d.year).arg(d.minor),
                         tr_("Caches, índices e plugins de uma versão antiga do %1. A versão %2.%3 é a mais nova e fica.")
                             .arg(d.product).arg(best.year).arg(best.minor),
                         tr_("Não volta: é de uma versão que você já atualizou. As configurações (~/.config/JetBrains) ficam."),
                         QString());
        for (const QString &base : bases) {
            const QString dir = base + QLatin1Char('/') + n;
            if (isRealDir(dir) || QFileInfo(dir).isSymLink()) {
                item.paths.append(dir);
                item.roots.append(base);
            }
        }
        item.path = item.paths.value(0);
        item.action = Action::RemoveDirs;
        item.fallback = Action::RemoveDirs;
        item.defaultSelected = true;
        item.busyCmdline = {QLatin1Char('=') + n, QLatin1Char('/') + n + QLatin1Char('/')};
        c.items.append(item);
    }
}

void detectAi(Ctx &c)
{
    const Env &env = c.env;
    c.label(tr_("Modelos de IA"));
    const QString hf = env.var(QStringLiteral("HF_HOME")).isEmpty() ? c.cacheHome + QStringLiteral("/huggingface")
                                                                    : stripSlash(env.var(QStringLiteral("HF_HOME")));
    {
        Item item = make(QStringLiteral("ai"), QStringLiteral("huggingface"), tr_("Cache do Hugging Face"),
                         tr_("Modelos e conjuntos de dados baixados por programas de IA (transformers, diffusers...). "
                             "O login (token) do Hugging Face fica."),
                         tr_("Volta quando um programa precisar do modelo: ele é baixado de novo."), hf);
        item.reinstall = true;
        item.busyCmdline = {QStringLiteral("huggingface")};
        const QString hub = hf + QStringLiteral("/hub");
        for (const QString &m : subdirs(hub)) {
            if (!m.startsWith(QLatin1String("models--")) && !m.startsWith(QLatin1String("datasets--")))
                continue;
            QString name = m.section(QStringLiteral("--"), 1).replace(QStringLiteral("--"), QStringLiteral("/"));
            item.children.append(QVariantMap {{QStringLiteral("name"), name}, {QStringLiteral("path"), hub + QLatin1Char('/') + m},
                                              {QStringLiteral("size"), -1.0},
                                              {QStringLiteral("detail"), m.startsWith(QLatin1String("models--")) ? tr_("modelo") : tr_("dados")}});
        }
        addCache(c, item, {hub, hf + QStringLiteral("/xet"), hf + QStringLiteral("/datasets")}, hf);
    }

    // Ollama: list only, managed in Settings > IA
    {
        QVariantList children;
        qint64 total = 0;
        const QString ollama = env.findExecutable(QStringLiteral("ollama"));
        if (!ollama.isEmpty()) {
            const CommandOutput out = runCommand(env, ollama, {QStringLiteral("list")}, 8000);
            static const QRegularExpression re(QStringLiteral("^(\\S+)\\s+\\S+\\s+([0-9.]+ ?[KMGT]?B)\\s"));
            if (out.finished && out.exitCode == 0) {
                for (const QString &line : out.out.split(QLatin1Char('\n'), Qt::SkipEmptyParts).mid(1)) {
                    const auto m = re.match(line);
                    if (!m.hasMatch())
                        continue;
                    const qint64 size = Hogs::parseHumanSize(m.captured(2));
                    total += qMax<qint64>(0, size);
                    children.append(QVariantMap {{QStringLiteral("name"), m.captured(1)}, {QStringLiteral("size"), double(size)},
                                                 {QStringLiteral("detail"), QString()}});
                }
            }
        }
        const QString models = env.home + QStringLiteral("/.ollama/models");
        if (!children.isEmpty() || isRealDir(models)) {
            Item item = make(QStringLiteral("ai"), QStringLiteral("ollama"), tr_("Modelos do Ollama"),
                             tr_("Modelos de IA baixados pelo Ollama. Escolha quais manter em Configurações › IA."),
                             tr_("Volta baixando o modelo de novo."), isRealDir(models) ? models : QString());
            item.children = children;
            item.link = QStringLiteral("settings:ai");
            if (!children.isEmpty()) {
                item.size = total;
                item.sizeFromTool = true;
            } else {
                item.paths = {models};
                item.roots = {env.home + QStringLiteral("/.ollama")};
            }
            c.items.append(item);
        }
    }

    // Lingmo transcription (whisper.cpp) models: list only, Settings > IA
    const QString whisper = c.dataHome + QStringLiteral("/lingmo/whisper-models");
    if (isRealDir(whisper)) {
        Item item = make(QStringLiteral("ai"), QStringLiteral("whisper"), tr_("Modelos de transcrição do Lingmo"),
                         tr_("Modelos do Whisper usados para legendas e ditado. Remova os que não usa em Configurações › IA › "
                             "Transcrição."),
                         tr_("Volta baixando o modelo de novo nas Configurações."), whisper);
        item.paths = {whisper};
        item.roots = {c.dataHome + QStringLiteral("/lingmo")};
        item.link = QStringLiteral("settings:ai/transcription");
        for (const QFileInfo &fi : QDir(whisper).entryInfoList({QStringLiteral("*.bin")}, QDir::Files | QDir::NoSymLinks, QDir::Name))
            item.children.append(QVariantMap {{QStringLiteral("name"), fi.completeBaseName().remove(QStringLiteral("ggml-"))},
                                              {QStringLiteral("size"), double(fi.size())}, {QStringLiteral("detail"), QString()}});
        if (!item.children.isEmpty())
            c.items.append(item);
    }
}

// node_modules, venvs and CMake build folders in the project folders
void detectProjects(Ctx &c)
{
    const Env &env = c.env;
    if (!env.scanProjects)
        return;
    const qint64 deadline = QDateTime::currentMSecsSinceEpoch() + 30000;
    QList<Item> nodeModules, venvs, builds;
    std::function<void(const QString &, const QString &, int)> walk = [&](const QString &dir, const QString &root, int depth) {
        if (depth > 6 || c.cancelled() || QDateTime::currentMSecsSinceEpoch() > deadline)
            return;
        // A CMake build folder: nothing else to look for inside
        if (depth > 0 && isRealFile(dir + QStringLiteral("/CMakeCache.txt"))) {
            const qint64 age = (env.now - mtimeOf(dir + QStringLiteral("/CMakeCache.txt"))) / Day;
            if (age > 30) {
                Item item = make(QStringLiteral("builds"), QStringLiteral("build:") + dir,
                                 QDir(env.home).relativeFilePath(dir),
                                 tr_("Pasta de compilação do CMake sem uso há %1 dias. Só listada: apague pelo gerenciador de "
                                     "arquivos se o projeto estiver parado.").arg(age),
                                 tr_("Volta ao configurar e compilar o projeto de novo."), dir);
                item.paths = {dir};
                item.roots = {root};
                item.ageDays = age;
                builds.append(item);
            }
            return;
        }
        for (const QString &name : subdirs(dir)) {
            const QString child = dir + QLatin1Char('/') + name;
            if (name == QLatin1String("node_modules")) {
                if (!isRealFile(dir + QStringLiteral("/package.json")))
                    continue;
                const qint64 touched = std::max({mtimeOf(child), mtimeOf(child + QStringLiteral("/.package-lock.json")),
                                                 mtimeOf(dir + QStringLiteral("/package.json")),
                                                 mtimeOf(dir + QStringLiteral("/package-lock.json"))});
                const qint64 age = (env.now - touched) / Day;
                if (age > 90) {
                    Item item = make(QStringLiteral("node"), QStringLiteral("nm:") + child,
                                     QStringLiteral("node_modules · ") + QDir(env.home).relativeFilePath(dir),
                                     tr_("Dependências instaladas de um projeto parado há %1 dias.").arg(age),
                                     tr_("Volta com npm install (ou yarn/pnpm install) no projeto."), child);
                    item.paths = {child};
                    item.roots = {root};
                    item.ageDays = age;
                    item.reinstall = true;
                    item.action = Action::RemoveDirs;
                    item.fallback = Action::RemoveDirs;
                    nodeModules.append(item);
                }
                continue;
            }
            if ((name == QLatin1String(".venv") || name == QLatin1String("venv")) && isRealFile(child + QStringLiteral("/pyvenv.cfg"))) {
                const qint64 touched = std::max({mtimeOf(child), mtimeOf(child + QStringLiteral("/pyvenv.cfg")),
                                                 mtimeOf(child + QStringLiteral("/bin"))});
                const qint64 age = (env.now - touched) / Day;
                if (age > 90) {
                    Item item = make(QStringLiteral("python"), QStringLiteral("venv:") + child,
                                     name + QStringLiteral(" · ") + QDir(env.home).relativeFilePath(dir),
                                     tr_("Ambiente virtual do Python de um projeto parado há %1 dias.").arg(age),
                                     tr_("Volta recriando o ambiente (python -m venv e pip install, uv sync ou poetry install)."), child);
                    item.paths = {child};
                    item.roots = {root};
                    item.ageDays = age;
                    item.reinstall = true;
                    item.action = Action::RemoveDirs;
                    item.fallback = Action::RemoveDirs;
                    item.busyExePrefixes = {canonical(child)};
                    venvs.append(item);
                }
                continue;
            }
            if (name.startsWith(QLatin1Char('.')) || name == QLatin1String("__pycache__") || name == QLatin1String("target"))
                continue;
            walk(child, root, depth + 1);
        }
    };
    for (const QString &root : env.effectiveProjectRoots()) {
        c.label(tr_("Pastas de projetos (%1)").arg(QDir(env.home).relativeFilePath(root)));
        walk(stripSlash(root), stripSlash(root), 0);
    }
    auto oldestFirst = [](const Item &a, const Item &b) { return a.ageDays > b.ageDays; };
    for (QList<Item> *list : {&nodeModules, &venvs, &builds}) {
        std::sort(list->begin(), list->end(), oldestFirst);
        c.items.append(list->mid(0, 60));
    }
}

} // namespace

QList<Item> detect(const Env &env, Progress *progress)
{
    Ctx c {env, progress, {}, {}, {}};
    c.cacheHome = env.var(QStringLiteral("XDG_CACHE_HOME")).isEmpty() ? env.home + QStringLiteral("/.cache")
                                                                      : stripSlash(env.var(QStringLiteral("XDG_CACHE_HOME")));
    c.dataHome = env.var(QStringLiteral("XDG_DATA_HOME")).isEmpty() ? env.home + QStringLiteral("/.local/share")
                                                                    : stripSlash(env.var(QStringLiteral("XDG_DATA_HOME")));
    const std::function<void(Ctx &)> steps[] = {
        detectPackages, [](Ctx &x) { detectContainers(x, QStringLiteral("docker")); },
        [](Ctx &x) { detectContainers(x, QStringLiteral("podman")); }, detectGo, detectNode, detectPython, detectRust,
        detectJava, detectJetBrains, detectAi, detectProjects};
    for (const auto &step : steps) {
        if (c.cancelled())
            break;
        step(c);
    }

    // Paths we would delete ourselves must pass the safety rules now, so
    // the person never gets offered something we would refuse later
    const QList<Running> running = runningProcesses(env.procDir);
    for (Item &item : c.items) {
        if (item.action == Action::EmptyDirs || item.action == Action::RemoveDirs) {
            for (const QString &p : std::as_const(item.paths)) {
                QString root;
                for (const QString &r : std::as_const(item.roots)) {
                    if (p == r || isInside(p, r))
                        root = r;
                }
                const PathVerdict v = checkPath(p, root.isEmpty() ? QStringLiteral("/") : root, env,
                                                item.action == Action::RemoveDirs);
                if (v != PathVerdict::Allowed) {
                    item.blockedReason = verdictText(v);
                    break;
                }
            }
        }
        if (item.busyReason.isEmpty())
            item.busyReason = busyReason(item, running);
        if (item.current || !item.cleanable())
            item.defaultSelected = false;
    }
    return c.items;
}

// ======================================================================
// sizes

void measure(QList<Item> &items, const Env &env, Progress *progress, qint64 perPathMs, qint64 totalMs,
             const QSet<QString> &only)
{
    Q_UNUSED(env);
    const qint64 end = QDateTime::currentMSecsSinceEpoch() + totalMs;
    int total = 0;
    for (const Item &item : std::as_const(items)) {
        if (!item.sizeFromTool && (only.isEmpty() || only.contains(item.id)))
            ++total;
    }
    if (progress) {
        progress->total = total;
        progress->done = 0;
    }
    for (Item &item : items) {
        if (item.sizeFromTool || (!only.isEmpty() && !only.contains(item.id)))
            continue;
        if (progress) {
            if (progress->cancel.load())
                break;
            progress->setLabel(item.title);
        }
        auto sizeOf = [&](const QString &path, bool *partial) -> qint64 {
            const qint64 now = QDateTime::currentMSecsSinceEpoch();
            if (now >= end) {
                *partial = true;
                return 0;
            }
            ScanProgress scan;
            scan.deadline = std::min(now + perPathMs, end);
            bool incomplete = false;
            const qint64 size = DiskScanner::totalSize(path, &incomplete, &scan);
            if (incomplete || scan.cancel.load())
                *partial = true;
            return size;
        };
        bool partial = false;
        qint64 size = 0;
        // Items with measured children (Hugging Face models): one walk per child
        QStringList measuredChildren;
        for (QVariant &v : item.children) {
            QVariantMap child = v.toMap();
            const QString path = child.value(QStringLiteral("path")).toString();
            if (path.isEmpty())
                continue;
            child[QStringLiteral("size")] = double(sizeOf(path, &partial));
            measuredChildren.append(path);
            v = child;
        }
        std::sort(item.children.begin(), item.children.end(), [](const QVariant &a, const QVariant &b) {
            return a.toMap().value(QStringLiteral("size")).toDouble() > b.toMap().value(QStringLiteral("size")).toDouble();
        });
        for (const QString &p : std::as_const(item.paths)) {
            if (QFileInfo(p).isSymLink())
                continue;
            size += sizeOf(p, &partial);
        }
        item.size = size;
        item.partial = partial;
        if (progress)
            ++progress->done;
    }
}

// ======================================================================
// cleaning

static qint64 pathsSize(const QStringList &paths)
{
    qint64 size = 0;
    for (const QString &p : paths) {
        if (!QFileInfo(p).isSymLink())
            size += DiskScanner::totalSize(p);
    }
    return size;
}

Result clean(const Item &item, const Env &env)
{
    Result r;
    if (item.action == Action::None || item.action == Action::Root) {
        r.message = tr_("Não pode ser limpo por aqui");
        return r;
    }
    if (item.current) {
        r.message = tr_("Está em uso");
        return r;
    }
    const QString busy = busyReason(item, runningProcesses(env.procDir));
    if (!busy.isEmpty()) {
        r.message = tr_("Em uso agora por \"%1\": feche o programa e tente de novo").arg(busy);
        return r;
    }
    // Project folders: make sure they are still what was found
    for (const QString &p : item.paths) {
        if (item.id.startsWith(QLatin1String("nm:")) && QFileInfo(p).fileName() != QLatin1String("node_modules")) {
            r.message = tr_("Não é uma pasta node_modules");
            return r;
        }
        if (item.id.startsWith(QLatin1String("venv:")) && !isRealFile(p + QStringLiteral("/pyvenv.cfg"))) {
            r.message = tr_("Não é um ambiente virtual do Python");
            return r;
        }
    }

    Action action = item.action;
    QString program = item.program;
    if (action == Action::Command && !QFileInfo(program).isExecutable()) {
        program = env.findExecutable(item.programName);
        if (program.isEmpty())
            action = item.fallback;
    }

    if (action == Action::Command) {
        const bool container = item.group == QLatin1String("docker") || item.group == QLatin1String("podman");
        const qint64 before = container ? 0 : pathsSize(item.paths);
        QProcess p;
        p.setProcessEnvironment(env.processEnvironment());
        p.setWorkingDirectory(env.home);
        p.setProcessChannelMode(QProcess::MergedChannels);
        p.setStandardInputFile(QProcess::nullDevice());
        p.start(program, item.args);
        if (!p.waitForStarted(10000)) {
            r.message = tr_("Não foi possível executar %1").arg(item.programName);
            return r;
        }
        if (!p.waitForFinished(env.cleanTimeoutMs)) {
            p.kill();
            p.waitForFinished(3000);
            r.message = tr_("%1 demorou demais e foi interrompido").arg(item.programName);
            return r;
        }
        const QString output = QString::fromUtf8(p.readAll());
        r.ok = p.exitStatus() == QProcess::NormalExit && p.exitCode() == 0;
        if (container) {
            qint64 freed = parseReclaimed(output);
            if (freed < 0) {
                // podman prints no total: compare what is still reclaimable
                bool ok = false;
                const QString type = item.id.endsWith(QLatin1String("-images")) ? QStringLiteral("Images")
                    : item.id.endsWith(QLatin1String("-containers")) ? QStringLiteral("Containers") : QStringLiteral("Build Cache");
                qint64 after = -1;
                for (const DfRow &row : containerDf(env, program, &ok)) {
                    if (row.type == type)
                        after = row.reclaimable;
                }
                freed = after >= 0 ? qMax<qint64>(0, item.size - after) : 0;
            }
            r.freed = freed;
        } else {
            r.freed = qMax<qint64>(0, before - pathsSize(item.paths));
        }
        if (!r.ok)
            r.message = lastLines(output).isEmpty() ? tr_("O comando terminou com erro") : lastLines(output);
        return r;
    }

    if (action != Action::EmptyDirs && action != Action::RemoveDirs) {
        r.message = tr_("Não pode ser limpo por aqui");
        return r;
    }
    const bool removeSelf = action == Action::RemoveDirs;
    int failures = 0;
    QString refused;
    for (const QString &p : item.paths) {
        struct stat st;
        if (::lstat(QFile::encodeName(p).constData(), &st) != 0)
            continue;   // already gone
        QString root;
        for (const QString &candidate : item.roots) {
            const QString cr = stripSlash(QDir::cleanPath(candidate));
            const QString cp = stripSlash(QDir::cleanPath(p));
            if (cp == cr || isInside(cp, cr))
                root = cr;
        }
        const PathVerdict v = checkPath(p, root.isEmpty() ? QStringLiteral("/") : root, env, removeSelf);
        if (v != PathVerdict::Allowed) {
            refused = verdictText(v) + QStringLiteral(": ") + p;
            continue;
        }
        int f = 0;
        r.freed += removeTree(p, removeSelf, &f);
        failures += f;
    }
    r.ok = refused.isEmpty();
    if (!refused.isEmpty())
        r.message = refused;
    else if (failures > 0)
        r.message = (failures == 1 ? tr_("1 item em uso ou protegido não pôde ser apagado")
                                   : tr_("%1 itens em uso ou protegidos não puderam ser apagados").arg(failures));
    return r;
}

bool safeForAutomation(const Item &item)
{
    return item.cleanable() && item.defaultSelected && !item.reinstall && item.busyReason.isEmpty()
        && item.warning.isEmpty() && (item.action == Action::Command || item.action == Action::EmptyDirs);
}

// ======================================================================
// for QML

QVariantMap toVariant(const Item &item)
{
    QVariantList children = item.children;
    return QVariantMap {
        {QStringLiteral("id"), item.id},
        {QStringLiteral("group"), item.group},
        {QStringLiteral("groupTitle"), groupTitle(item.group)},
        {QStringLiteral("title"), item.title},
        {QStringLiteral("description"), item.description},
        {QStringLiteral("comesBack"), item.comesBack},
        {QStringLiteral("warning"), item.warning},
        {QStringLiteral("path"), item.path},
        {QStringLiteral("paths"), item.paths},
        {QStringLiteral("size"), double(item.size)},
        {QStringLiteral("partial"), item.partial},
        {QStringLiteral("upTo"), item.upTo},
        {QStringLiteral("reinstall"), item.reinstall},
        {QStringLiteral("current"), item.current},
        {QStringLiteral("defaultSelected"), item.defaultSelected && item.busyReason.isEmpty()},
        {QStringLiteral("cleanable"), item.cleanable()},
        {QStringLiteral("listOnly"), item.action == Action::None},
        {QStringLiteral("root"), item.action == Action::Root},
        {QStringLiteral("blockedReason"), item.blockedReason},
        {QStringLiteral("busyReason"), item.busyReason},
        {QStringLiteral("command"), item.action == Action::Command || item.action == Action::Root ? item.commandText : QString()},
        {QStringLiteral("link"), item.link},
        {QStringLiteral("children"), children},
        {QStringLiteral("ageDays"), double(item.ageDays)},
    };
}

QStringList groupOrder()
{
    return {QStringLiteral("packages"), QStringLiteral("docker"), QStringLiteral("podman"), QStringLiteral("go"),
            QStringLiteral("node"), QStringLiteral("python"), QStringLiteral("rust"), QStringLiteral("java"),
            QStringLiteral("jetbrains"), QStringLiteral("ai"), QStringLiteral("builds")};
}

QString groupTitle(const QString &group)
{
    static const QHash<QString, const char *> titles = {
        {QStringLiteral("packages"), QT_TRANSLATE_NOOP("DevCaches", "Pacotes (pacman e AUR)")},
        {QStringLiteral("docker"), QT_TRANSLATE_NOOP("DevCaches", "Docker")},
        {QStringLiteral("podman"), QT_TRANSLATE_NOOP("DevCaches", "Podman")},
        {QStringLiteral("go"), QT_TRANSLATE_NOOP("DevCaches", "Go")},
        {QStringLiteral("node"), QT_TRANSLATE_NOOP("DevCaches", "Node.js")},
        {QStringLiteral("python"), QT_TRANSLATE_NOOP("DevCaches", "Python")},
        {QStringLiteral("rust"), QT_TRANSLATE_NOOP("DevCaches", "Rust")},
        {QStringLiteral("java"), QT_TRANSLATE_NOOP("DevCaches", "Java e Android")},
        {QStringLiteral("jetbrains"), QT_TRANSLATE_NOOP("DevCaches", "JetBrains")},
        {QStringLiteral("ai"), QT_TRANSLATE_NOOP("DevCaches", "Modelos de IA")},
        {QStringLiteral("builds"), QT_TRANSLATE_NOOP("DevCaches", "Builds antigos de projetos")},
    };
    return titles.contains(group) ? tr_(titles.value(group)) : group;
}

QString groupIcon(const QString &group)
{
    static const QHash<QString, QString> icons = {
        {QStringLiteral("packages"), QStringLiteral("package-x-generic")},
        {QStringLiteral("docker"), QStringLiteral("application-x-executable")},
        {QStringLiteral("podman"), QStringLiteral("application-x-executable")},
        {QStringLiteral("go"), QStringLiteral("text-x-go")},
        {QStringLiteral("node"), QStringLiteral("text-x-javascript")},
        {QStringLiteral("python"), QStringLiteral("text-x-python")},
        {QStringLiteral("rust"), QStringLiteral("text-x-rust")},
        {QStringLiteral("java"), QStringLiteral("text-x-java")},
        {QStringLiteral("jetbrains"), QStringLiteral("applications-development")},
        {QStringLiteral("ai"), QStringLiteral("applications-science")},
        {QStringLiteral("builds"), QStringLiteral("folder-development")},
    };
    return icons.value(group, QStringLiteral("applications-development"));
}

} // namespace DevCaches
