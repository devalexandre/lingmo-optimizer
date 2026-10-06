#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QtTest>

#include "core/diskusage.h"
#include "core/format.h"
#include "core/hogs.h"
#include "core/policy.h"
#include "core/procparse.h"

#include <sys/stat.h>
#include <unistd.h>

using namespace ProcParse;

class TestOptimizer : public QObject
{
    Q_OBJECT

private:
    static void writeFile(const QString &path, qint64 size)
    {
        QDir().mkpath(QFileInfo(path).absolutePath());
        QFile f(path);
        QVERIFY(f.open(QIODevice::WriteOnly));
        // Real data (not a sparse file), so st_blocks matches the size
        const QByteArray chunk(64 * 1024, 'x');
        qint64 left = size;
        while (left > 0) {
            const qint64 n = qMin<qint64>(left, chunk.size());
            f.write(chunk.constData(), n);
            left -= n;
        }
    }
    static qint64 blocks(const QString &path)
    {
        struct stat st;
        return ::lstat(QFile::encodeName(path).constData(), &st) == 0 ? qint64(st.st_blocks) * 512 : -1;
    }

private slots:
    // ------------------------------------------------------------ /proc parsing
    void meminfo()
    {
        const QByteArray text =
            "MemTotal:       32768000 kB\n"
            "MemFree:         1024000 kB\n"
            "MemAvailable:   14000000 kB\n"
            "Buffers:          512000 kB\n"
            "Cached:         10000000 kB\n"
            "SwapCached:            0 kB\n"
            "Shmem:            300000 kB\n"
            "SReclaimable:     800000 kB\n"
            "SwapTotal:       8388604 kB\n"
            "SwapFree:        8000000 kB\n";
        const MemInfo m = parseMeminfo(text);
        QVERIFY(m.valid());
        QCOMPARE(m.total, 32768000LL * 1024);
        QCOMPARE(m.available, 14000000LL * 1024);
        // "used" is MemTotal - MemAvailable, not total - free - buffers - cached
        QCOMPARE(m.used(), (32768000LL - 14000000LL) * 1024);
        QCOMPARE(m.cache(), (512000LL + 10000000LL + 800000LL - 300000LL) * 1024);
        QCOMPARE(m.swapUsed(), (8388604LL - 8000000LL) * 1024);
    }

    void meminfoWithoutAvailable()
    {
        // Very old kernels: fall back to free + buffers + cached
        const MemInfo m = parseMeminfo("MemTotal: 1000 kB\nMemFree: 100 kB\nBuffers: 100 kB\nCached: 300 kB\n");
        QCOMPARE(m.used(), 500LL * 1024);
        QVERIFY(!parseMeminfo("").valid());
    }

    void readProcFileReadsSizeZeroFiles()
    {
        // procfs reports st_size 0; the old atEnd() loop read nothing
        const QByteArray data = readProcFile(QStringLiteral("/proc/meminfo"));
        QVERIFY(data.contains("MemTotal:"));
        QVERIFY(parseMeminfo(data).valid());
        QVERIFY(parseMeminfo(data).available > 0);
    }

    void stat()
    {
        const QByteArray first =
            "cpu  100 10 50 800 40 5 5 0 20 0\n"
            "cpu0 50 5 25 400 20 3 2 0 10 0\n"
            "cpu1 50 5 25 400 20 2 3 0 10 0\n"
            "intr 12345\n";
        const QVector<CpuTimes> a = parseStat(first);
        QCOMPARE(a.size(), 3);
        // user+nice+system+idle+iowait+irq+softirq+steal (guest is inside user)
        QCOMPARE(a.at(0).total, quint64(100 + 10 + 50 + 800 + 40 + 5 + 5 + 0));
        QCOMPARE(a.at(0).idle, quint64(800 + 40));

        const QByteArray second =
            "cpu  200 10 100 850 50 10 10 20 20 0\n"
            "cpu0 100 5 50 425 25 5 5 10 10 0\n"
            "cpu1 100 5 50 425 25 5 5 10 10 0\n";
        const QVector<CpuTimes> b = parseStat(second);
        // delta total = 100+0+50+50+10+5+5+20 = 240, delta idle = 50+10 = 60
        QCOMPARE(cpuUsage(a.at(0), b.at(0)), (240.0 - 60.0) / 240.0);
        QCOMPARE(cpuUsage(b.at(0), b.at(0)), 0.0);
    }

    void pidStat()
    {
        // comm with spaces and parentheses: only the LAST ')' ends it
        const QByteArray text = "4242 (Web Content (x)) S 1000 4242 4242 0 -1 4194560 100 0 0 0 "
                                "1500 300 0 0 20 0 30 0 987654 4096000 25000 18446744073709551615";
        const PidStat s = parsePidStat(text);
        QVERIFY(s.ok);
        QCOMPARE(s.pid, 4242);
        QCOMPARE(s.comm, QByteArray("Web Content (x)"));
        QCOMPARE(s.state, 'S');
        QCOMPARE(s.ppid, 1000);
        QCOMPARE(s.utime, quint64(1500));
        QCOMPARE(s.stime, quint64(300));
        QCOMPARE(s.startTime, quint64(987654));
        QCOMPARE(s.rssPages, qint64(25000));
        QVERIFY(!s.isKernelThread());

        const PidStat k = parsePidStat("2 (kthreadd) S 0 0 0 0 -1 2129984 0 0 0 0 0 0 0 0 20 0 1 0 2 0 0 0");
        QVERIFY(k.ok);
        QVERIFY(k.isKernelThread());

        QVERIFY(!parsePidStat("garbage").ok);
        QVERIFY(!parsePidStat("12 (short) S 1").ok);
    }

    void smapsAndCmdline()
    {
        QCOMPARE(parseSmapsRollupPss("Rss:  2048 kB\nPss:  1024 kB\nPss_Anon: 512 kB\n"), 1024LL * 1024);
        QCOMPARE(parseSmapsRollupPss("Rss: 10 kB\n"), -1LL);
        QCOMPARE(parseCmdline(QByteArray("/usr/bin/python3\0-m\0foo\0", 24)),
                 QStringList({QStringLiteral("/usr/bin/python3"), QStringLiteral("-m"), QStringLiteral("foo")}));
        QCOMPARE(parseLoadavg("0.52 1.10 2.00 1/123 4567\n"), QVector<double>({0.52, 1.10, 2.00}));
    }

    void format()
    {
        QCOMPARE(Format::bytes(0), QStringLiteral("0 B"));
        QCOMPARE(Format::bytes(695LL * 1024 * 1024 * 1024), QStringLiteral("695 GB"));
        QCOMPARE(Format::bytes(qint64(17.9 * 1024 * 1024 * 1024)), QStringLiteral("17,9 GB"));
        QCOMPARE(Format::bytes(512LL * 1024 * 1024), QStringLiteral("512 MB"));
        QCOMPARE(Format::frequency(2400), QStringLiteral("2,4 GHz"));
        QCOMPARE(Format::percent(0.79), QStringLiteral("79%"));
    }

    // ------------------------------------------------------------ disk usage
    void diskUsageTree()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        const QString root = tmp.path();
        writeFile(root + QStringLiteral("/a/big.bin"), 3 * 1024 * 1024);
        writeFile(root + QStringLiteral("/a/small.txt"), 10 * 1024);
        writeFile(root + QStringLiteral("/b/file.bin"), 1024 * 1024);
        writeFile(root + QStringLiteral("/outside/huge.bin"), 4 * 1024 * 1024);
        // a hard link to big.bin (counted once) and symlinks (never followed)
        QVERIFY(::link(QFile::encodeName(root + QStringLiteral("/a/big.bin")).constData(),
                       QFile::encodeName(root + QStringLiteral("/b/big-link.bin")).constData()) == 0);
        QVERIFY(QFile::link(root + QStringLiteral("/outside"), root + QStringLiteral("/b/dir-symlink")));
        QVERIFY(QFile::link(root + QStringLiteral("/outside/huge.bin"), root + QStringLiteral("/a/file-symlink")));

        DiskScanner scanner;
        const auto tree = scanner.scan(root + QStringLiteral("/a"));
        const auto treeB = scanner.scan(root + QStringLiteral("/b"));

        // /a: big + small + its own folder block + the symlink's (tiny) inode
        const qint64 expectedA = blocks(root + QStringLiteral("/a")) + blocks(root + QStringLiteral("/a/big.bin"))
            + blocks(root + QStringLiteral("/a/small.txt")) + blocks(root + QStringLiteral("/a/file-symlink"));
        QCOMPARE(tree->size, expectedA);
        QVERIFY(tree->size < 4 * 1024 * 1024); // huge.bin was not followed
        QCOMPARE(tree->children.front()->name, QByteArray("big.bin")); // largest first

        // /b on its own: the hard link counts (first time this scan sees the inode)
        QVERIFY(treeB->size >= 3 * 1024 * 1024);

        // Whole tree: big.bin + its hard link once; dir-symlink not followed
        const auto all = scanner.scan(root);
        qint64 expected = blocks(root);
        for (const QString &p : {QStringLiteral("/a"), QStringLiteral("/a/big.bin"), QStringLiteral("/a/small.txt"),
                                 QStringLiteral("/a/file-symlink"), QStringLiteral("/b"), QStringLiteral("/b/file.bin"),
                                 QStringLiteral("/b/dir-symlink"), QStringLiteral("/outside"), QStringLiteral("/outside/huge.bin")})
            expected += blocks(root + p);
        QCOMPARE(all->size, expected);

        DuNode *a = all->child(QStringLiteral("a"));
        DuNode *b = all->child(QStringLiteral("b"));
        QVERIFY(a && b);
        QVERIFY(a->isDir());
        QCOMPARE(a->path(), root + QStringLiteral("/a"));
        QCOMPARE(b->child(QStringLiteral("dir-symlink"))->kind, DuNode::File); // a link, not a folder

        // removeChild keeps the ancestors' totals right
        const qint64 before = all->size;
        const qint64 bigSize = a->child(QStringLiteral("big.bin"))->size;
        a->removeChild(a->child(QStringLiteral("big.bin")));
        QCOMPARE(all->size, before - bigSize);
    }

    void diskUsageProgressiveAndDenied()
    {
        if (::getuid() == 0)
            QSKIP("root can read everything");
        QTemporaryDir tmp;
        const QString root = tmp.path();
        writeFile(root + QStringLiteral("/open/x.bin"), 2 * 1024 * 1024);
        writeFile(root + QStringLiteral("/locked/secret.bin"), 1024 * 1024);
        writeFile(root + QStringLiteral("/top.bin"), 1024 * 1024);
        QVERIFY(::chmod(QFile::encodeName(root + QStringLiteral("/locked")).constData(), 0) == 0);

        QStringList received;
        qint64 receivedBytes = 0;
        DiskScanner scanner;
        ScanProgress progress;
        const auto rest = scanner.scan(root, &progress, [&](std::unique_ptr<DuNode> child) {
            received.append(QString::fromLocal8Bit(child->name));
            receivedBytes += child->size;
            if (child->name == "locked") {
                QVERIFY(child->denied);           // shown as "sem permissão"
                QCOMPARE(child->size, blocks(root + QStringLiteral("/locked"))); // partial: its own block
            }
        });
        ::chmod(QFile::encodeName(root + QStringLiteral("/locked")).constData(), 0755);
        received.sort();
        QCOMPARE(received, QStringList({QStringLiteral("locked"), QStringLiteral("open"), QStringLiteral("top.bin")}));
        QVERIFY(rest->children.empty());
        QVERIFY(rest->incomplete);
        QCOMPARE(rest->size, receivedBytes + blocks(root));
        QVERIFY(progress.bytes.load() > 0);
    }

    void diskUsageCancel()
    {
        QTemporaryDir tmp;
        writeFile(tmp.path() + QStringLiteral("/f.bin"), 1024);
        ScanProgress progress;
        progress.cancel = true;
        const auto tree = DiskScanner().scan(tmp.path(), &progress);
        QVERIFY(tree->incomplete);
    }

    void smallFilesGrouped()
    {
        QTemporaryDir tmp;
        for (int i = 0; i < 150; ++i)
            writeFile(tmp.path() + QStringLiteral("/f%1").arg(i), 100);
        writeFile(tmp.path() + QStringLiteral("/large"), 2 * 1024 * 1024);
        const auto tree = DiskScanner().scan(tmp.path());
        QCOMPARE(int(tree->children.size()), 2);
        QCOMPARE(tree->children.front()->name, QByteArray("large"));
        QCOMPARE(tree->children.back()->kind, DuNode::Others);
        QCOMPARE(tree->children.back()->entries, 150LL);
    }

    void removeContentsDoesNotFollowSymlinks()
    {
        QTemporaryDir tmp;
        const QString cache = tmp.path() + QStringLiteral("/cache");
        const QString keep = tmp.path() + QStringLiteral("/keep");
        writeFile(cache + QStringLiteral("/sub/a.bin"), 256 * 1024);
        writeFile(keep + QStringLiteral("/precious.txt"), 1024);
        QVERIFY(QFile::link(keep, cache + QStringLiteral("/link-to-keep")));
        int failures = -1;
        const qint64 freed = removeDirectoryContents(cache, &failures);
        QCOMPARE(failures, 0);
        QVERIFY(freed >= 256 * 1024);
        QVERIFY(QFileInfo(cache).isDir());                     // the folder itself stays
        QVERIFY(QDir(cache).isEmpty(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System));
        QVERIFY(QFile::exists(keep + QStringLiteral("/precious.txt"))); // symlink target untouched
    }

    // ------------------------------------------------------------ trash policy
    void trashPolicy()
    {
        QTemporaryDir tmp;
        const QString home = tmp.path() + QStringLiteral("/home");
        const QString outside = tmp.path() + QStringLiteral("/elsewhere");
        writeFile(home + QStringLiteral("/Videos/movie.mkv"), 10);
        writeFile(home + QStringLiteral("/projects/app/node_modules/x.js"), 10);
        writeFile(home + QStringLiteral("/.config/app.conf"), 10);
        writeFile(home + QStringLiteral("/.ssh/id_ed25519"), 10);
        writeFile(home + QStringLiteral("/.local/share/Steam/game.bin"), 10);
        writeFile(home + QStringLiteral("/.local/share/Trash/files/old.txt"), 10);
        writeFile(home + QStringLiteral("/.cache/thumbnails/a.png"), 10);
        writeFile(home + QStringLiteral("/.bashrc"), 10);
        writeFile(outside + QStringLiteral("/system.bin"), 10);
        QVERIFY(QFile::link(outside, home + QStringLiteral("/escape")));
        QVERIFY(QFile::link(home + QStringLiteral("/.config"), home + QStringLiteral("/config-link")));
        QVERIFY(QFile::link(outside + QStringLiteral("/system.bin"), home + QStringLiteral("/file-link")));
        const uint uid = ::getuid();
        using V = Policy::TrashVerdict;

        QCOMPARE(Policy::trashVerdict(home + QStringLiteral("/Videos/movie.mkv"), home, uid), V::Allowed);
        QCOMPARE(Policy::trashVerdict(home + QStringLiteral("/Videos"), home, uid), V::Allowed);
        QCOMPARE(Policy::trashVerdict(home + QStringLiteral("/projects/app/node_modules"), home, uid), V::Allowed);
        QCOMPARE(Policy::trashVerdict(home + QStringLiteral("/.cache/thumbnails"), home, uid), V::Allowed);
        QCOMPARE(Policy::trashVerdict(home, home, uid), V::IsHome);
        QCOMPARE(Policy::trashVerdict(home + QStringLiteral("/"), home, uid), V::IsHome);
        QCOMPARE(Policy::trashVerdict(outside + QStringLiteral("/system.bin"), home, uid), V::OutsideHome);
        QCOMPARE(Policy::trashVerdict(QStringLiteral("/usr/bin/ls"), home, uid), V::OutsideHome);
        QCOMPARE(Policy::trashVerdict(home + QStringLiteral("/../elsewhere/system.bin"), home, uid), V::OutsideHome);
        QCOMPARE(Policy::trashVerdict(home + QStringLiteral("x/file"), home, uid), V::OutsideHome); // /home/userx
        QCOMPARE(Policy::trashVerdict(QStringLiteral("relative/path"), home, uid), V::NotAbsolute);
        QCOMPARE(Policy::trashVerdict(home + QStringLiteral("/.config"), home, uid), V::Protected);
        QCOMPARE(Policy::trashVerdict(home + QStringLiteral("/.config/app.conf"), home, uid), V::Protected);
        QCOMPARE(Policy::trashVerdict(home + QStringLiteral("/.ssh/id_ed25519"), home, uid), V::Protected);
        QCOMPARE(Policy::trashVerdict(home + QStringLiteral("/.local/share/Steam"), home, uid), V::Protected);
        QCOMPARE(Policy::trashVerdict(home + QStringLiteral("/.bashrc"), home, uid), V::Protected);
        QCOMPARE(Policy::trashVerdict(home + QStringLiteral("/.cache"), home, uid), V::Protected);
        QCOMPARE(Policy::trashVerdict(home + QStringLiteral("/.local/share/Trash/files/old.txt"), home, uid), V::InTrash);
        QCOMPARE(Policy::trashVerdict(home + QStringLiteral("/Videos/missing.mkv"), home, uid), V::Missing);
        // Symlinked folder leading out of home: its contents are not ours to trash
        QCOMPARE(Policy::trashVerdict(home + QStringLiteral("/escape/system.bin"), home, uid), V::SymlinkEscape);
        // ...nor into a protected folder
        QCOMPARE(Policy::trashVerdict(home + QStringLiteral("/config-link/app.conf"), home, uid), V::Protected);
        // A symlink itself is trashed as a link (the target is never touched)
        QCOMPARE(Policy::trashVerdict(home + QStringLiteral("/escape"), home, uid), V::Allowed);
        QCOMPARE(Policy::trashVerdict(home + QStringLiteral("/file-link"), home, uid), V::Allowed);
        // Another owner
        QCOMPARE(Policy::trashVerdict(home + QStringLiteral("/Videos/movie.mkv"), home, uid + 1), V::NotOwned);

        QVERIFY(Policy::isProtectedHomePath(QStringLiteral(".gnupg/x")));
        QVERIFY(!Policy::isProtectedHomePath(QStringLiteral("Downloads/x.iso")));
        QVERIFY(!Policy::isProtectedHomePath(QStringLiteral("projects/app/.venv")));
        QVERIFY(Policy::isProtectedHomePath(QStringLiteral("../etc")));
    }

    // ------------------------------------------------------------ process policy
    void processPolicy()
    {
        for (const char *name : {"lingmo-session", "kwin_x11", "kwin_wayland", "Xorg", "Xwayland", "plasmashell", "sddm",
                                 "systemd", "systemd-logind", "dbus-daemon", "dbus-broker", "pipewire", "pipewire-pulse",
                                 "wireplumber", "lingmo-statusbar", "lingmo-dock", "lingmo-launcher",
                                 "lingmo-settings-daemon", "lingmo-polkit-agent", "lingmo-optimizer"})
            QVERIFY2(Policy::isProtectedProcess(QString::fromLatin1(name)), name);
        for (const char *name : {"firefox", "chrome", "code", "ollama", "lingmo-terminal", "lingmo-store", "python3"})
            QVERIFY2(!Policy::isProtectedProcess(QString::fromLatin1(name)), name);
        QVERIFY(Policy::isProtectedProcess(QStringLiteral("lingmo-filemanager"), {QStringLiteral("lingmo-filemanager"), QStringLiteral("--desktop")}));
        QVERIFY(!Policy::isProtectedProcess(QStringLiteral("lingmo-filemanager"), {QStringLiteral("lingmo-filemanager")}));

        const uint me = 1000;
        QVERIFY(Policy::mayTerminate(QStringLiteral("firefox"), {}, me, me, 4242, 100, false));
        QVERIFY(!Policy::mayTerminate(QStringLiteral("firefox"), {}, 0, me, 4242, 100, false));      // root's
        QVERIFY(!Policy::mayTerminate(QStringLiteral("firefox"), {}, 1001, me, 4242, 100, false));   // other user
        QVERIFY(!Policy::mayTerminate(QStringLiteral("kworker/0:1"), {}, me, me, 4242, 100, true));  // kernel thread
        QVERIFY(!Policy::mayTerminate(QStringLiteral("whatever"), {}, me, me, 100, 100, false));     // ourselves
        QVERIFY(!Policy::mayTerminate(QStringLiteral("init"), {}, me, me, 1, 100, false));
        QVERIFY(!Policy::mayTerminate(QStringLiteral("kwin_x11"), {}, me, me, 4242, 100, false));
    }

    // ------------------------------------------------------------ suggestions helpers
    void hogs()
    {
        QTemporaryDir tmp;
        const QString p = tmp.path();
        writeFile(p + QStringLiteral("/rust/Cargo.toml"), 10);
        QDir().mkpath(p + QStringLiteral("/rust/target"));
        QDir().mkpath(p + QStringLiteral("/photos/target"));
        writeFile(p + QStringLiteral("/cpp/CMakeLists.txt"), 10);
        QDir().mkpath(p + QStringLiteral("/cpp/build"));
        QDir().mkpath(p + QStringLiteral("/docs/build"));
        writeFile(p + QStringLiteral("/py/.venv/pyvenv.cfg"), 10);
        QDir().mkpath(p + QStringLiteral("/web/node_modules"));
        QVERIFY(Hogs::isBuildDir(p + QStringLiteral("/rust/target")));
        QVERIFY(!Hogs::isBuildDir(p + QStringLiteral("/photos/target")));
        QVERIFY(Hogs::isBuildDir(p + QStringLiteral("/cpp/build")));
        QVERIFY(!Hogs::isBuildDir(p + QStringLiteral("/docs/build")));
        QVERIFY(Hogs::isBuildDir(p + QStringLiteral("/py/.venv")));
        QVERIFY(Hogs::isBuildDir(p + QStringLiteral("/web/node_modules")));

        QCOMPARE(Hogs::parseHumanSize(QStringLiteral("4.7 GB")), qint64(4.7e9));
        QCOMPARE(Hogs::parseHumanSize(QStringLiteral("12.3GB")), qint64(12.3e9));
        QCOMPARE(Hogs::parseHumanSize(QStringLiteral("1 GiB")), 1024LL * 1024 * 1024);
        QCOMPARE(Hogs::parseHumanSize(QStringLiteral("0B")), 0LL);
        QCOMPARE(Hogs::parseHumanSize(QStringLiteral("abc")), -1LL);
    }
};

QTEST_GUILESS_MAIN(TestOptimizer)
#include "tst_optimizer.moc"
