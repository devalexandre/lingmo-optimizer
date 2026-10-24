#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QtTest>

#include "core/devcaches.h"

#include <sys/stat.h>
#include <sys/time.h>
#include <unistd.h>

using namespace DevCaches;

// Everything runs against a scratch HOME with fake tools on PATH that only
// record their arguments: nothing of the real machine is measured or cleaned.
class TestDevCaches : public QObject
{
    Q_OBJECT

private:
    QTemporaryDir m_tmp;
    QString m_home, m_bin, m_log, m_outside, m_proc, m_pacman;

    static void writeFile(const QString &path, qint64 size = 4096, const QByteArray &fill = "x")
    {
        QDir().mkpath(QFileInfo(path).absolutePath());
        QFile f(path);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(QByteArray(size, fill.at(0)));
    }
    static void writeText(const QString &path, const QByteArray &text)
    {
        QDir().mkpath(QFileInfo(path).absolutePath());
        QFile f(path);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(text);
    }
    static void script(const QString &path, const QByteArray &body)
    {
        writeText(path, "#!/bin/sh\n" + body);
        QFile::setPermissions(path, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
    }
    static void age(const QString &path, int days)
    {
        struct timeval tv[2];
        tv[0].tv_sec = tv[1].tv_sec = time(nullptr) - qint64(days) * 86400;
        tv[0].tv_usec = tv[1].tv_usec = 0;
        ::lutimes(QFile::encodeName(path).constData(), tv);
    }
    static bool symlink(const QString &target, const QString &link)
    {
        QDir().mkpath(QFileInfo(link).absolutePath());
        return ::symlink(QFile::encodeName(target).constData(), QFile::encodeName(link).constData()) == 0;
    }
    QString log() const
    {
        QFile f(m_log);
        return f.open(QIODevice::ReadOnly) ? QString::fromUtf8(f.readAll()) : QString();
    }
    void clearLog() { QFile::remove(m_log); }

    Env env() const
    {
        Env e;
        e.home = m_home;
        e.path = {m_bin};                  // only the fakes: never a real tool
        e.vars.insert(QStringLiteral("FAKE_LOG"), m_log);
        e.now = QDateTime::currentSecsSinceEpoch();
        e.uid = ::getuid();
        e.userDirs = {m_home + QStringLiteral("/Documents"), m_home + QStringLiteral("/Downloads")};
        e.pacmanCache = m_pacman;
        e.pacmanLock = m_tmp.path() + QStringLiteral("/db.lck");
        e.procDir = m_proc;
        e.commandTimeoutMs = 10000;
        return e;
    }
    static const Item *find(const QList<Item> &items, const QString &id)
    {
        for (const Item &item : items) {
            if (item.id == id)
                return &item;
        }
        return nullptr;
    }

private slots:
    void initTestCase()
    {
        QVERIFY(m_tmp.isValid());
        m_home = m_tmp.path() + QStringLiteral("/home/ana");
        m_bin = m_tmp.path() + QStringLiteral("/bin");
        m_log = m_tmp.path() + QStringLiteral("/calls.log");
        m_outside = m_tmp.path() + QStringLiteral("/outside");
        m_proc = m_tmp.path() + QStringLiteral("/proc");
        m_pacman = m_tmp.path() + QStringLiteral("/pacman-pkg");
        QDir().mkpath(m_home);
        QDir().mkpath(m_proc);
        QDir().mkpath(m_pacman);
        writeFile(m_outside + QStringLiteral("/precious.txt"), 100);
        writeFile(m_home + QStringLiteral("/Documents/thesis.odt"), 100);

        // ---- fake tools (they only log)
        script(m_bin + QStringLiteral("/npm"), "echo \"npm $*\" >> \"$FAKE_LOG\"\n");
        script(m_bin + QStringLiteral("/nvm"), "echo \"nvm $*\" >> \"$FAKE_LOG\"\n");
        script(m_bin + QStringLiteral("/paccache"),
               "echo \"paccache $*\" >> \"$FAKE_LOG\"\n"
               "case \"$1\" in\n"
               "  -dk1) echo '==> finished dry run: 4 candidates (disk space saved: 1.50 GiB)' ;;\n"
               "  -duk0) echo '==> finished dry run: 2 candidates (disk space saved: 300.00 MiB)' ;;\n"
               "esac\n");
        script(m_bin + QStringLiteral("/docker"),
               "echo \"docker $*\" >> \"$FAKE_LOG\"\n"
               "case \"$1 $2\" in\n"
               "  'system df') printf 'Images\\t5.2GB\\t3.1GB (59%%)\\nContainers\\t120MB\\t100MB (83%%)\\n"
               "Local Volumes\\t2GB\\t1GB (50%%)\\nBuild Cache\\t800MB\\t800MB\\n' ;;\n"
               "  'volume ls') if [ \"$3\" = -q ]; then echo pgdata_old; else printf 'pgdata\\npgdata_old\\n'; fi ;;\n"
               "  'image prune') echo 'Total reclaimed space: 3.1GB' ;;\n"
               "  'container prune') echo 'Total reclaimed space: 100MB' ;;\n"
               "  'builder prune') echo 'Total reclaimed space: 800MB' ;;\n"
               "esac\n");

        // ---- Go: caches, and gvm with three versions; `go` on PATH is go1.22.1
        writeFile(m_home + QStringLiteral("/.cache/go-build/ab/abcdef-d"), 64 * 1024);
        writeFile(m_home + QStringLiteral("/go/pkg/mod/github.com/x/y@v1.0.0/y.go"), 32 * 1024);
        for (const char *v : {"go1.20.14", "go1.21.5", "go1.22.1"})
            writeFile(m_home + QStringLiteral("/.gvm/gos/%1/bin/gofmt").arg(QLatin1String(v)), 8192);
        writeFile(m_home + QStringLiteral("/.gvm/pkgsets/go1.20.14/global/pkg/a.a"), 8192);
        writeText(m_home + QStringLiteral("/.gvm/environments/default"), "export gvm_go_name=\"go1.21.5\"\n");
        script(m_home + QStringLiteral("/.gvm/gos/go1.22.1/bin/go"),
               "echo \"go $*\" >> \"$FAKE_LOG\"\n"
               "if [ \"$1\" = env ]; then echo \"$HOME/.cache/go-build\"; echo \"$HOME/go/pkg/mod\"; fi\n");
        QVERIFY(symlink(m_home + QStringLiteral("/.gvm/gos/go1.22.1/bin/go"), m_bin + QStringLiteral("/go")));

        // ---- Node: npm cache, nvm (current v18 on PATH, default lts/* = iron = v20)
        writeFile(m_home + QStringLiteral("/.npm/_cacache/content-v2/sha512/aa"), 16 * 1024);
        for (const char *v : {"v16.20.2", "v18.20.4", "v20.11.1"})
            writeFile(m_home + QStringLiteral("/.nvm/versions/node/%1/lib/libnode.so").arg(QLatin1String(v)), 8192);
        script(m_home + QStringLiteral("/.nvm/versions/node/v18.20.4/bin/node"), "exit 0\n");
        QVERIFY(symlink(m_home + QStringLiteral("/.nvm/versions/node/v18.20.4/bin/node"), m_bin + QStringLiteral("/node")));
        writeText(m_home + QStringLiteral("/.nvm/alias/default"), "lts/*\n");
        writeText(m_home + QStringLiteral("/.nvm/alias/lts/*"), "lts/iron\n");
        writeText(m_home + QStringLiteral("/.nvm/alias/lts/iron"), "v20.11.1\n");

        // ---- Python: pip cache, pyenv (global 3.12.1)
        writeFile(m_home + QStringLiteral("/.cache/pip/http-v2/a/b"), 16 * 1024);
        for (const char *v : {"3.10.13", "3.11.7", "3.12.1"})
            writeFile(m_home + QStringLiteral("/.pyenv/versions/%1/bin/python3").arg(QLatin1String(v)), 8192);
        writeText(m_home + QStringLiteral("/.pyenv/version"), "3.12.1\n");

        // ---- Rust
        writeFile(m_home + QStringLiteral("/.cargo/registry/cache/index.crates.io/serde.crate"), 16 * 1024);
        writeFile(m_home + QStringLiteral("/.cargo/bin/cargo"), 100);   // must never be touched
        for (const char *t : {"stable-x86_64-unknown-linux-gnu", "nightly-2024-01-01-x86_64-unknown-linux-gnu",
                              "1.70.0-x86_64-unknown-linux-gnu"})
            writeFile(m_home + QStringLiteral("/.rustup/toolchains/%1/lib/librustc.so").arg(QLatin1String(t)), 8192);
        writeText(m_home + QStringLiteral("/.rustup/settings.toml"),
                  "version = \"12\"\ndefault_toolchain = \"stable-x86_64-unknown-linux-gnu\"\n\n[overrides]\n"
                  "\"/home/ana/projects/old\" = \"nightly-2024-01-01-x86_64-unknown-linux-gnu\"\n");

        // ---- JetBrains: two GoLand versions, one PyCharm
        for (const char *d : {".cache/JetBrains/GoLand2023.3/index/a", ".cache/JetBrains/GoLand2024.2/index/a",
                              ".local/share/JetBrains/GoLand2023.3/plugins/p", ".cache/JetBrains/PyCharm2024.1/index/a"})
            writeFile(m_home + QLatin1Char('/') + QLatin1String(d), 8192);

        // ---- AUR
        writeFile(m_home + QStringLiteral("/.cache/yay/some-pkg/some-pkg.tar.zst"), 16 * 1024);

        // ---- Projects: an old and a recent node_modules, an old venv, an old CMake build
        writeText(m_home + QStringLiteral("/projects/old/package.json"), "{}");
        writeFile(m_home + QStringLiteral("/projects/old/node_modules/left-pad/index.js"), 8192);
        QVERIFY(symlink(m_outside, m_home + QStringLiteral("/projects/old/node_modules/.bin-escape")));
        writeText(m_home + QStringLiteral("/projects/new/package.json"), "{}");
        writeFile(m_home + QStringLiteral("/projects/new/node_modules/react/index.js"), 8192);
        writeFile(m_home + QStringLiteral("/projects/py/.venv/pyvenv.cfg"), 100);
        writeFile(m_home + QStringLiteral("/projects/py/.venv/bin/python"), 100);
        writeFile(m_home + QStringLiteral("/projects/lingmo-x/build-agent/CMakeCache.txt"), 100);
        writeFile(m_home + QStringLiteral("/projects/lingmo-x/build-agent/app"), 8192);
        writeFile(m_home + QStringLiteral("/projects/lingmo-y/build/CMakeCache.txt"), 100);   // recent
        for (const char *p : {"projects/old/node_modules/left-pad/index.js", "projects/old/node_modules/left-pad",
                              "projects/old/node_modules", "projects/old/package.json"})
            age(m_home + QLatin1Char('/') + QLatin1String(p), 200);
        for (const char *p : {"projects/py/.venv/pyvenv.cfg", "projects/py/.venv/bin", "projects/py/.venv"})
            age(m_home + QLatin1Char('/') + QLatin1String(p), 120);
        age(m_home + QStringLiteral("/projects/lingmo-x/build-agent/CMakeCache.txt"), 45);
        // A node_modules in Documents (user files) is never offered
        writeText(m_home + QStringLiteral("/Documents/site/package.json"), "{}");
        writeFile(m_home + QStringLiteral("/Documents/site/node_modules/x/index.js"), 100);
    }

    // ------------------------------------------------------------ pure helpers
    void versions()
    {
        QCOMPARE(compareVersions(QStringLiteral("v18.20.4"), QStringLiteral("v18.3.0")), 1);
        QCOMPARE(compareVersions(QStringLiteral("go1.9"), QStringLiteral("go1.21.5")), -1);
        QCOMPARE(compareVersions(QStringLiteral("3.12.1"), QStringLiteral("3.12.1")), 0);
    }

    void jetbrains()
    {
        JetBrainsDir d;
        QVERIFY(parseJetBrainsDir(QStringLiteral("IntelliJIdea2024.1"), &d));
        QCOMPARE(d.product, QStringLiteral("IntelliJIdea"));
        QCOMPARE(d.year, 2024);
        QCOMPARE(d.minor, 1);
        QVERIFY(parseJetBrainsDir(QStringLiteral("PyCharmCE2023.3"), &d));
        QCOMPARE(d.product, QStringLiteral("PyCharmCE"));
        QVERIFY(!parseJetBrainsDir(QStringLiteral("Toolbox"), nullptr));
        QVERIFY(!parseJetBrainsDir(QStringLiteral("consentOptions"), nullptr));
        const QStringList old = oldJetBrainsDirs({QStringLiteral("GoLand2023.3"), QStringLiteral("GoLand2024.2"),
                                                  QStringLiteral("GoLand2024.10"), QStringLiteral("PyCharm2024.1"),
                                                  QStringLiteral("PyCharmCE2022.1"), QStringLiteral("Toolbox")});
        // minor 10 is newer than 2; each product keeps its newest
        QCOMPARE(old, (QStringList {QStringLiteral("GoLand2023.3"), QStringLiteral("GoLand2024.2")}));
    }

    void nvmCurrentAndDefault()
    {
        const QString nvm = m_home + QStringLiteral("/.nvm");
        const QStringList installed = {QStringLiteral("v16.20.2"), QStringLiteral("v18.20.4"), QStringLiteral("v20.11.1")};
        QCOMPARE(resolveNvmAlias(nvm, QStringLiteral("lts/*"), installed), QStringLiteral("v20.11.1"));
        QCOMPARE(resolveNvmAlias(nvm, QStringLiteral("18"), installed), QStringLiteral("v18.20.4"));
        QCOMPARE(resolveNvmAlias(nvm, QStringLiteral("node"), installed), QStringLiteral("v20.11.1"));
        QCOMPARE(resolveNvmAlias(nvm, QStringLiteral("v14"), installed), QString());
        QCOMPARE(resolveNvmAlias(nvm, QStringLiteral("lts/../../etc"), installed), QString());
        const QString node = QFileInfo(m_bin + QStringLiteral("/node")).canonicalFilePath();
        QStringList keep = nvmProtected(nvm, installed, node, QString());
        keep.sort();
        QCOMPARE(keep, (QStringList {QStringLiteral("v18.20.4"), QStringLiteral("v20.11.1")}));
    }

    void gvmPyenvRustupCurrent()
    {
        const QString gvm = m_home + QStringLiteral("/.gvm");
        const QStringList gos = {QStringLiteral("go1.20.14"), QStringLiteral("go1.21.5"), QStringLiteral("go1.22.1")};
        QStringList keep = gvmProtected(gvm, gos, QFileInfo(m_bin + QStringLiteral("/go")).canonicalFilePath(), QString());
        keep.sort();
        QCOMPARE(keep, (QStringList {QStringLiteral("go1.21.5"), QStringLiteral("go1.22.1")}));
        QCOMPARE(gvmProtected(gvm, gos, QString(), gvm + QStringLiteral("/gos/go1.20.14")).first(), QStringLiteral("go1.20.14"));

        const QString pyenv = m_home + QStringLiteral("/.pyenv");
        const QStringList pys = {QStringLiteral("3.10.13"), QStringLiteral("3.11.7"), QStringLiteral("3.12.1")};
        keep = pyenvProtected(pyenv, pys, QStringLiteral("3.11"), QString());
        keep.sort();
        QCOMPARE(keep, (QStringList {QStringLiteral("3.11.7"), QStringLiteral("3.12.1")}));
        QCOMPARE(pyenvProtected(pyenv, pys, QString(), pyenv + QStringLiteral("/versions/3.10.13/bin/python3")).size(), 2);

        const QStringList tcs = {QStringLiteral("stable-x86_64-unknown-linux-gnu"),
                                 QStringLiteral("nightly-2024-01-01-x86_64-unknown-linux-gnu"),
                                 QStringLiteral("1.70.0-x86_64-unknown-linux-gnu")};
        keep = rustupProtected(QStringLiteral("default_toolchain = \"stable\"\n[overrides]\n\"/p\" = \"nightly-2024-01-01\"\n"), tcs, QString());
        QCOMPARE(keep, tcs.mid(0, 2));
        // Without a default we cannot tell: keep everything
        QCOMPARE(rustupProtected(QString(), tcs, QString()), tcs);
    }

    void parsers()
    {
        QCOMPARE(parsePaccacheSaved(QStringLiteral("==> finished dry run: 4 candidates (disk space saved: 1.50 GiB)")),
                 qint64(1.5 * 1024 * 1024 * 1024));
        QCOMPARE(parsePaccacheSaved(QStringLiteral("==> no candidate packages found for pruning")), qint64(-1));
        QCOMPARE(parseReclaimed(QStringLiteral("Deleted Images:\n...\nTotal reclaimed space: 3.1GB\n")), qint64(3100000000LL));
        QCOMPARE(parseReclaimed(QStringLiteral("nothing")), qint64(-1));
    }

    // ------------------------------------------------------------ path safety
    void pathSafety()
    {
        const Env e = env();
        const QString cache = m_home + QStringLiteral("/.cache");
        QCOMPARE(checkPath(cache + QStringLiteral("/yay"), cache + QStringLiteral("/yay"), e, false), PathVerdict::Allowed);
        // removing the root itself is not allowed, only what is inside
        QCOMPARE(checkPath(cache + QStringLiteral("/yay"), cache + QStringLiteral("/yay"), e, true), PathVerdict::OutsideRoot);
        QCOMPARE(checkPath(m_home, cache, e, false), PathVerdict::IsHome);
        QCOMPARE(checkPath(m_outside, m_outside, e, false), PathVerdict::OutsideHome);
        QCOMPARE(checkPath(m_home + QStringLiteral("/../../outside"), cache, e, false), PathVerdict::OutsideHome);
        QCOMPARE(checkPath(QStringLiteral("relative/path"), cache, e, false), PathVerdict::NotAbsolute);
        // a path outside the item's cache location
        QCOMPARE(checkPath(m_home + QStringLiteral("/projects"), cache, e, false), PathVerdict::OutsideRoot);
        // the root must be strictly inside home
        QCOMPARE(checkPath(m_home + QStringLiteral("/projects"), m_home, e, false), PathVerdict::OutsideRoot);
        // user folders and anything inside or holding them
        QCOMPARE(checkPath(m_home + QStringLiteral("/Documents/site/node_modules"), m_home + QStringLiteral("/Documents"), e, true),
                 PathVerdict::UserFolder);

        // symlink escape: a cache folder that is a link to elsewhere
        QVERIFY(symlink(m_outside, cache + QStringLiteral("/linked")));
        QCOMPARE(checkPath(cache + QStringLiteral("/linked"), cache + QStringLiteral("/linked"), e, false), PathVerdict::Symlink);
        // ...and a folder reached through that link
        QDir().mkpath(m_outside + QStringLiteral("/sub"));
        QCOMPARE(checkPath(cache + QStringLiteral("/linked/sub"), cache, e, true), PathVerdict::SymlinkEscape);
        QCOMPARE(checkPath(cache + QStringLiteral("/missing"), cache, e, true), PathVerdict::Missing);
        QFile::remove(cache + QStringLiteral("/linked"));
    }

    void removeTreeStaysInside()
    {
        const QString dir = m_tmp.path() + QStringLiteral("/home/ana/.cache/rmtest");
        writeFile(dir + QStringLiteral("/a/b/file"), 8192);
        QVERIFY(symlink(m_outside, dir + QStringLiteral("/escape")));
        QVERIFY(symlink(m_outside + QStringLiteral("/precious.txt"), dir + QStringLiteral("/a/file-link")));
        // Read-only folders like the Go module cache
        writeFile(dir + QStringLiteral("/ro/mod/x.go"), 4096);
        QFile::setPermissions(dir + QStringLiteral("/ro/mod"), QFile::ReadOwner | QFile::ExeOwner);
        QFile::setPermissions(dir + QStringLiteral("/ro"), QFile::ReadOwner | QFile::ExeOwner);
        int failures = -1;
        const qint64 freed = removeTree(dir, false, &failures);
        QCOMPARE(failures, 0);
        QVERIFY(freed >= 8192 + 4096);
        QVERIFY(QFileInfo(dir).isDir());
        QVERIFY(QDir(dir).isEmpty(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System));
        QVERIFY(QFile::exists(m_outside + QStringLiteral("/precious.txt")));
        QVERIFY(QFileInfo(m_outside + QStringLiteral("/sub")).isDir());
        removeTree(dir, true, &failures);
        QVERIFY(!QFileInfo::exists(dir));
    }

    // ------------------------------------------------------------ detection
    void detection()
    {
        clearLog();
        const QList<Item> items = detect(env());

        // pacman (paccache dry runs, nothing removed)
        const Item *old = find(items, QStringLiteral("pacman-old"));
        QVERIFY(old);
        QCOMPARE(old->action, Action::Root);
        QCOMPARE(old->size, qint64(1.5 * 1024 * 1024 * 1024));
        QVERIFY(old->defaultSelected);
        QVERIFY(find(items, QStringLiteral("pacman-uninstalled")));
        QVERIFY(log().contains(QStringLiteral("paccache -dk1 --nocolor")));
        QVERIFY(log().contains(QStringLiteral("paccache -duk0 --nocolor")));
        QVERIFY(!log().contains(QStringLiteral("-rk1")));
        QVERIFY(find(items, QStringLiteral("yay")));
        QVERIFY(!find(items, QStringLiteral("paru")));    // only what exists

        // Docker: images/build preselected, containers not, volumes only listed
        const Item *images = find(items, QStringLiteral("docker-images"));
        QVERIFY(images);
        QCOMPARE(images->size, Hogs_parse("3.1GB"));
        QVERIFY(images->defaultSelected);
        QCOMPARE(images->args, (QStringList {QStringLiteral("image"), QStringLiteral("prune"), QStringLiteral("-a"), QStringLiteral("-f")}));
        QVERIFY(find(items, QStringLiteral("docker-build"))->defaultSelected);
        QVERIFY(!find(items, QStringLiteral("docker-containers"))->defaultSelected);
        const Item *volumes = find(items, QStringLiteral("docker-volumes"));
        QVERIFY(volumes);
        QCOMPARE(volumes->action, Action::None);
        QVERIFY(!volumes->defaultSelected);
        QVERIFY(volumes->warning.contains(QStringLiteral("bancos de dados")));
        QCOMPARE(volumes->children.size(), 2);
        QVERIFY(!log().contains(QStringLiteral("prune")));   // detection never cleans

        // Go: the tool's own cleaner, gvm versions with the current ones kept
        const Item *gobuild = find(items, QStringLiteral("go-build"));
        QVERIFY(gobuild);
        QCOMPARE(gobuild->action, Action::Command);
        QCOMPARE(gobuild->commandText, QStringLiteral("go clean -cache"));
        QVERIFY(gobuild->defaultSelected);
        QCOMPARE(find(items, QStringLiteral("go-mod"))->commandText, QStringLiteral("go clean -modcache"));
        QVERIFY(find(items, QStringLiteral("gvm:go1.22.1"))->current);    // go on PATH
        QVERIFY(find(items, QStringLiteral("gvm:go1.21.5"))->current);    // gvm default
        const Item *go120 = find(items, QStringLiteral("gvm:go1.20.14"));
        QVERIFY(!go120->current);
        QVERIFY(go120->reinstall);
        QVERIFY(!go120->defaultSelected);
        QCOMPARE(go120->action, Action::RemoveDirs);   // no gvm binary: folders removed
        QCOMPARE(go120->paths.size(), 2);              // gos/ and pkgsets/

        // Node
        QCOMPARE(find(items, QStringLiteral("npm-cache"))->commandText, QStringLiteral("npm cache clean --force"));
        QVERIFY(find(items, QStringLiteral("nvm:v18.20.4"))->current);
        QVERIFY(find(items, QStringLiteral("nvm:v20.11.1"))->current);
        const Item *v16 = find(items, QStringLiteral("nvm:v16.20.2"));
        QVERIFY(v16 && !v16->current && !v16->defaultSelected && v16->reinstall);
        QCOMPARE(v16->action, Action::Command);        // the fake nvm on PATH
        const Item *nm = find(items, QStringLiteral("nm:") + m_home + QStringLiteral("/projects/old/node_modules"));
        QVERIFY(nm);
        QVERIFY(!nm->defaultSelected);
        QVERIFY(nm->reinstall);
        QVERIFY(nm->ageDays >= 199);
        QVERIFY(!find(items, QStringLiteral("nm:") + m_home + QStringLiteral("/projects/new/node_modules")));
        for (const Item &item : items)
            QVERIFY2(!item.path.contains(QStringLiteral("/Documents/")), qPrintable(item.id));

        // Python
        QCOMPARE(find(items, QStringLiteral("pip"))->action, Action::EmptyDirs);   // no pip: the folder is emptied
        QVERIFY(find(items, QStringLiteral("pyenv:3.12.1"))->current);
        QVERIFY(!find(items, QStringLiteral("pyenv:3.10.13"))->current);
        const Item *venv = find(items, QStringLiteral("venv:") + m_home + QStringLiteral("/projects/py/.venv"));
        QVERIFY(venv && !venv->defaultSelected && venv->reinstall);

        // Rust
        const Item *cargo = find(items, QStringLiteral("cargo"));
        QVERIFY(cargo);
        QVERIFY(!cargo->paths.contains(m_home + QStringLiteral("/.cargo/bin")));
        QVERIFY(find(items, QStringLiteral("rustup:stable-x86_64-unknown-linux-gnu"))->current);
        QVERIFY(find(items, QStringLiteral("rustup:nightly-2024-01-01-x86_64-unknown-linux-gnu"))->current);
        QVERIFY(!find(items, QStringLiteral("rustup:1.70.0-x86_64-unknown-linux-gnu"))->current);

        // JetBrains: the old GoLand (both folders), never the newest or the only PyCharm
        const Item *goland = find(items, QStringLiteral("jetbrains:GoLand2023.3"));
        QVERIFY(goland);
        QCOMPARE(goland->paths.size(), 2);
        QVERIFY(goland->defaultSelected);
        QVERIFY(!find(items, QStringLiteral("jetbrains:GoLand2024.2")));
        QVERIFY(!find(items, QStringLiteral("jetbrains:PyCharm2024.1")));

        // CMake builds older than 30 days: listed only
        const Item *build = find(items, QStringLiteral("build:") + m_home + QStringLiteral("/projects/lingmo-x/build-agent"));
        QVERIFY(build);
        QCOMPARE(build->action, Action::None);
        QVERIFY(!find(items, QStringLiteral("build:") + m_home + QStringLiteral("/projects/lingmo-y/build")));

        // The automatic (intent) selection never takes versions, environments,
        // projects, containers, volumes or root items
        QStringList automatic;
        for (const Item &item : items) {
            if (safeForAutomation(item))
                automatic.append(item.id);
        }
        automatic.sort();
        QCOMPARE(automatic, (QStringList {QStringLiteral("cargo"), QStringLiteral("docker-build"), QStringLiteral("docker-images"),
                                          QStringLiteral("go-build"), QStringLiteral("go-mod"), QStringLiteral("npm-cache"),
                                          QStringLiteral("pip"), QStringLiteral("yay")}));
    }

    void projectsCanBeLeftOut()
    {
        Env e = env();
        e.scanProjects = false;
        for (const Item &item : detect(e)) {
            QVERIFY(!item.id.startsWith(QLatin1String("nm:")));
            QVERIFY(!item.id.startsWith(QLatin1String("venv:")));
            QVERIFY(!item.id.startsWith(QLatin1String("build:")));
        }
    }

    void measureWithTimeLimit()
    {
        QList<Item> items = detect(env());
        measure(items, env(), nullptr, 5000, 30000, {QStringLiteral("go-build"), QStringLiteral("gvm:go1.20.14")});
        QVERIFY(find(items, QStringLiteral("go-build"))->size >= 64 * 1024);
        QVERIFY(!find(items, QStringLiteral("go-build"))->partial);
        QVERIFY(find(items, QStringLiteral("gvm:go1.20.14"))->size >= 2 * 8192);
        QCOMPARE(find(items, QStringLiteral("npm-cache"))->size, qint64(-1));   // not asked
        // No time left: nothing measured, marked partial
        measure(items, env(), nullptr, 5000, 0, {QStringLiteral("npm-cache")});
        QVERIFY(find(items, QStringLiteral("npm-cache"))->partial);
    }

    void pacmanBusyWhileLocked()
    {
        writeText(m_tmp.path() + QStringLiteral("/db.lck"), "");
        const QList<Item> items = detect(env());
        QCOMPARE(find(items, QStringLiteral("pacman-old"))->busyReason, QStringLiteral("pacman"));
        QFile::remove(m_tmp.path() + QStringLiteral("/db.lck"));
    }

    // ------------------------------------------------------------ cleaning
    void cleanUsesTheToolsOwnCommand()
    {
        QList<Item> items = detect(env());
        clearLog();
        Result r = clean(*find(items, QStringLiteral("go-build")), env());
        QVERIFY2(r.ok, qPrintable(r.message));
        QVERIFY(log().contains(QStringLiteral("go clean -cache")));
        r = clean(*find(items, QStringLiteral("npm-cache")), env());
        QVERIFY(r.ok);
        QVERIFY(log().contains(QStringLiteral("npm cache clean --force")));
        r = clean(*find(items, QStringLiteral("nvm:v16.20.2")), env());
        QVERIFY(r.ok);
        QVERIFY(log().contains(QStringLiteral("nvm uninstall v16.20.2")));
        // Docker: images first, the daemon reports what it freed
        r = clean(*find(items, QStringLiteral("docker-images")), env());
        QVERIFY(r.ok);
        QCOMPARE(r.freed, qint64(3100000000LL));
        QVERIFY(log().contains(QStringLiteral("docker image prune -a -f")));
        QVERIFY(!log().contains(QStringLiteral("volume prune")));
        QVERIFY(!log().contains(QStringLiteral("system prune")));
        // the fakes did not delete anything
        QVERIFY(QFile::exists(m_home + QStringLiteral("/.cache/go-build/ab/abcdef-d")));
    }

    void cleanRefusesWhatIsNotOffered()
    {
        QList<Item> items = detect(env());
        clearLog();
        // the version in use
        Result r = clean(*find(items, QStringLiteral("nvm:v18.20.4")), env());
        QVERIFY(!r.ok);
        QVERIFY(log().isEmpty());
        // list only
        r = clean(*find(items, QStringLiteral("docker-volumes")), env());
        QVERIFY(!r.ok);
        // root steps are the helper's job
        r = clean(*find(items, QStringLiteral("pacman-old")), env());
        QVERIFY(!r.ok);
        QVERIFY(!log().contains(QStringLiteral("-rk1")));
    }

    void cleanFallsBackToTheFolder()
    {
        // go missing from PATH: the cache folder is emptied (not removed)
        Env e = env();
        QList<Item> items = detect(e);
        Item gobuild = *find(items, QStringLiteral("go-build"));
        QFile::rename(m_bin + QStringLiteral("/go"), m_tmp.path() + QStringLiteral("/go-hidden"));
        gobuild.program = m_tmp.path() + QStringLiteral("/no-such-go");
        const Result r = clean(gobuild, e);
        QFile::rename(m_tmp.path() + QStringLiteral("/go-hidden"), m_bin + QStringLiteral("/go"));
        QVERIFY2(r.ok, qPrintable(r.message));
        QVERIFY(r.freed >= 64 * 1024);
        QVERIFY(QFileInfo(m_home + QStringLiteral("/.cache/go-build")).isDir());
        QVERIFY(!QFile::exists(m_home + QStringLiteral("/.cache/go-build/ab/abcdef-d")));
    }

    void cleanRemovesVersionsAndProjectsSafely()
    {
        QList<Item> items = detect(env());
        // gvm version without gvm: gos/ and pkgsets/ folders go, the rest stays
        Result r = clean(*find(items, QStringLiteral("gvm:go1.20.14")), env());
        QVERIFY2(r.ok, qPrintable(r.message));
        QVERIFY(!QFileInfo::exists(m_home + QStringLiteral("/.gvm/gos/go1.20.14")));
        QVERIFY(!QFileInfo::exists(m_home + QStringLiteral("/.gvm/pkgsets/go1.20.14")));
        QVERIFY(QFileInfo::exists(m_home + QStringLiteral("/.gvm/gos/go1.21.5")));
        // an old node_modules holding a link out of home: the link goes, the target stays
        r = clean(*find(items, QStringLiteral("nm:") + m_home + QStringLiteral("/projects/old/node_modules")), env());
        QVERIFY2(r.ok, qPrintable(r.message));
        QVERIFY(!QFileInfo::exists(m_home + QStringLiteral("/projects/old/node_modules")));
        QVERIFY(QFile::exists(m_home + QStringLiteral("/projects/old/package.json")));
        QVERIFY(QFile::exists(m_outside + QStringLiteral("/precious.txt")));
        QVERIFY(QFile::exists(m_home + QStringLiteral("/Documents/thesis.odt")));
        // A project item whose path no longer is a node_modules is refused
        Item fake = *find(items, QStringLiteral("venv:") + m_home + QStringLiteral("/projects/py/.venv"));
        fake.id = QStringLiteral("nm:") + m_home + QStringLiteral("/projects/py");
        fake.paths = {m_home + QStringLiteral("/projects/py")};
        QVERIFY(!clean(fake, env()).ok);
        QVERIFY(QFileInfo::exists(m_home + QStringLiteral("/projects/py/.venv/pyvenv.cfg")));
    }

    void cleanRefusesSymlinkEscape()
    {
        // ~/.cache/yay replaced by a link to another place after detection
        QList<Item> items = detect(env());
        const Item yay = *find(items, QStringLiteral("yay"));
        const QString dir = m_home + QStringLiteral("/.cache/yay");
        removeTree(dir, true);
        QVERIFY(symlink(m_outside, dir));
        const Result r = clean(yay, env());
        QVERIFY(!r.ok);
        QVERIFY(QFile::exists(m_outside + QStringLiteral("/precious.txt")));
        // and detection now shows it as blocked, never preselected
        items = detect(env());
        const Item *again = find(items, QStringLiteral("yay"));
        QVERIFY(again);
        QVERIFY(!again->blockedReason.isEmpty());
        QVERIFY(!again->cleanable());
        QVERIFY(!again->defaultSelected);
        QFile::remove(dir);
        // an item pointing outside its cache location is refused too
        Item evil = yay;
        evil.paths = {m_outside};
        QVERIFY(!clean(evil, env()).ok);
        evil.paths = {m_home + QStringLiteral("/Documents")};
        evil.roots = {m_home + QStringLiteral("/Documents")};
        QVERIFY(!clean(evil, env()).ok);
        QVERIFY(QFile::exists(m_home + QStringLiteral("/Documents/thesis.odt")));
        QVERIFY(QFile::exists(m_outside + QStringLiteral("/precious.txt")));
    }

    void cleanSkipsWhatIsRunning()
    {
        // A fake /proc with a running `go` build
        writeText(m_proc + QStringLiteral("/4242/comm"), "go\n");
        writeText(m_proc + QStringLiteral("/4242/cmdline"), QByteArray("go\0build\0./...", 14));
        QList<Item> items = detect(env());
        const Item *gobuild = find(items, QStringLiteral("go-build"));
        QCOMPARE(gobuild->busyReason, QStringLiteral("go"));
        clearLog();
        const Result r = clean(*gobuild, env());
        QVERIFY(!r.ok);
        QVERIFY(r.message.contains(QStringLiteral("go")));
        QVERIFY(!log().contains(QStringLiteral("clean -cache")));
        removeTree(m_proc + QStringLiteral("/4242"), true);
    }

private:
    static qint64 Hogs_parse(const char *text);
};

#include "core/hogs.h"
qint64 TestDevCaches::Hogs_parse(const char *text)
{
    return Hogs::parseHumanSize(QLatin1String(text));
}

QTEST_GUILESS_MAIN(TestDevCaches)
#include "tst_devcaches.moc"
