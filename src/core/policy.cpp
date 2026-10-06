#include "policy.h"

#include <QDir>
#include <QFileInfo>

#include <sys/stat.h>

namespace Policy {

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

bool isProtectedHomePath(const QString &relativePath)
{
    const QString rel = QDir::cleanPath(relativePath);
    if (rel.isEmpty() || rel == QLatin1String(".") || rel.startsWith(QLatin1String("..")))
        return true;
    const QString first = rel.section(QLatin1Char('/'), 0, 0);
    if (!first.startsWith(QLatin1Char('.')))
        return false;
    // ~/.cache/<something> is safe to throw away; ~/.cache itself is not offered
    if (first == QLatin1String(".cache") && rel.contains(QLatin1Char('/')))
        return false;
    return true;
}

TrashVerdict trashVerdict(const QString &path, const QString &home, uint uid)
{
    if (!path.startsWith(QLatin1Char('/')) || !home.startsWith(QLatin1Char('/')))
        return TrashVerdict::NotAbsolute;

    const QString cleanHome = stripSlash(QDir::cleanPath(home));
    const QString cleanPath = stripSlash(QDir::cleanPath(path));
    if (cleanPath == cleanHome)
        return TrashVerdict::IsHome;
    if (!isInside(cleanPath, cleanHome))
        return TrashVerdict::OutsideHome;

    const QString rel = cleanPath.mid(cleanHome.size() + 1);
    if (rel == QLatin1String(".local/share/Trash") || rel.startsWith(QLatin1String(".local/share/Trash/")))
        return TrashVerdict::InTrash;
    if (isProtectedHomePath(rel))
        return TrashVerdict::Protected;

    struct stat st;
    if (::lstat(QFile::encodeName(cleanPath).constData(), &st) != 0)
        return TrashVerdict::Missing;

    // The parent folder must really be inside home: a symlinked folder on the
    // way (~/link -> /usr) must not let us trash things elsewhere
    const QString realHome = stripSlash(QFileInfo(cleanHome).canonicalFilePath());
    const QString parent = QFileInfo(cleanPath).absolutePath();
    const QString realParent = stripSlash(QFileInfo(parent).canonicalFilePath());
    if (realHome.isEmpty() || realParent.isEmpty())
        return TrashVerdict::Missing;
    if (realParent != realHome && !isInside(realParent, realHome))
        return TrashVerdict::SymlinkEscape;
    // ...and the resolved location must not land in a protected place either
    const QString realRel = realParent == realHome
        ? QFileInfo(cleanPath).fileName()
        : realParent.mid(realHome.size() + 1) + QLatin1Char('/') + QFileInfo(cleanPath).fileName();
    if (realRel.startsWith(QLatin1String(".local/share/Trash")))
        return TrashVerdict::InTrash;
    if (isProtectedHomePath(realRel))
        return TrashVerdict::Protected;

    if (st.st_uid != uid)
        return TrashVerdict::NotOwned;
    return TrashVerdict::Allowed;
}

bool isProtectedProcess(const QString &rawName, const QStringList &args)
{
    const QString name = rawName.toLower();
    static const QStringList exact = {
        // display server, session and system plumbing
        QStringLiteral("xorg"), QStringLiteral("x"), QStringLiteral("xwayland"), QStringLiteral("xvfb"),
        QStringLiteral("plasmashell"), QStringLiteral("ksmserver"), QStringLiteral("kded6"), QStringLiteral("kded5"),
        QStringLiteral("init"), QStringLiteral("login"), QStringLiteral("agetty"),
        QStringLiteral("wireplumber"), QStringLiteral("pulseaudio"),
        QStringLiteral("gnome-keyring-daemon"), QStringLiteral("kwalletd6"), QStringLiteral("kwalletd5"),
        QStringLiteral("ssh-agent"), QStringLiteral("gpg-agent"), QStringLiteral("polkitd"),
        QStringLiteral("networkmanager"), QStringLiteral("upowerd"), QStringLiteral("udisksd"),
        QStringLiteral("accounts-daemon"), QStringLiteral("rtkit-daemon"), QStringLiteral("bluetoothd"),
        // Lingmo session (lingmo-core, shell and the optimizer itself)
        QStringLiteral("lingmo-session"), QStringLiteral("lingmo-settings-daemon"),
        QStringLiteral("lingmo-xembedsniproxy"), QStringLiteral("lingmo-gmenuproxy"),
        QStringLiteral("lingmo-powerman"), QStringLiteral("lingmo-clipboard"),
        QStringLiteral("lingmo-notificationd"), QStringLiteral("lingmo-polkit-agent"),
        QStringLiteral("lingmo-chotkeys"), QStringLiteral("lingmo-cpufreq"),
        QStringLiteral("lingmo-screen-brightness"), QStringLiteral("lingmo-sddm-helper"),
        QStringLiteral("lingmo-statusbar"), QStringLiteral("lingmo-dock"), QStringLiteral("lingmo-launcher"),
        QStringLiteral("lingmo-screenlocker"), QStringLiteral("lingmo-shutdown"),
        QStringLiteral("lingmo-optimizer"),
    };
    static const QStringList prefixes = {
        QStringLiteral("kwin"), QStringLiteral("sddm"), QStringLiteral("systemd"), QStringLiteral("dbus"),
        QStringLiteral("pipewire"), QStringLiteral("xdg-desktop-portal"), QStringLiteral("xdg-document-portal"),
        QStringLiteral("xdg-permission-store"), QStringLiteral("at-spi"), QStringLiteral("polkit"),
        QStringLiteral("lingmo-session"),
    };
    if (exact.contains(name))
        return true;
    for (const QString &prefix : prefixes) {
        if (name.startsWith(prefix))
            return true;
    }
    // The file manager also draws the desktop icons when started with --desktop
    if (name == QLatin1String("lingmo-filemanager") && args.contains(QStringLiteral("--desktop")))
        return true;
    return false;
}

bool mayTerminate(const QString &name, const QStringList &args, uint processUid, uint myUid,
                  int pid, int myPid, bool kernelThread)
{
    if (kernelThread || pid <= 1 || pid == myPid)
        return false;
    if (processUid != myUid)
        return false;
    return !isProtectedProcess(name, args);
}

} // namespace Policy
