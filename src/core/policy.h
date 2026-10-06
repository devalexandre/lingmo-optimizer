#pragma once

#include <QString>
#include <QStringList>

// Safety rules shared by the UI and the tests. Kept free of any UI code.
namespace Policy {

enum class TrashVerdict {
    Allowed,
    Missing,          // path does not exist (lstat failed)
    NotAbsolute,
    OutsideHome,      // not inside the home folder (after resolving the parent)
    IsHome,           // the home folder itself
    Protected,        // dotfile/config dir (~/.config, ~/.ssh, ~/.local/share...)
    InTrash,          // already in ~/.local/share/Trash
    NotOwned,         // not owned by the user
    SymlinkEscape     // a parent folder resolves outside the home folder
};

// May `path` be moved to the trash by the user `uid` whose home is `home`?
// Only paths strictly inside home, owned by the user, whose parents do not
// leave home through symlinks, and outside protected dotfiles/dot-dirs. The
// last component is never followed (a symlink is trashed as a link).
TrashVerdict trashVerdict(const QString &path, const QString &home, uint uid);
inline bool mayMoveToTrash(const QString &path, const QString &home, uint uid)
{
    return trashVerdict(path, home, uid) == TrashVerdict::Allowed;
}

// Purely lexical part of the rule above: is this path (relative to home,
// e.g. ".config/foo") one we never offer to delete? Top-level dotfiles and
// dot-dirs are protected, except the contents of ~/.cache.
bool isProtectedHomePath(const QString &relativePath);

// Processes we never offer to end: they keep the session alive (or are us).
// `name` is the executable's base name; `args` its command line.
bool isProtectedProcess(const QString &name, const QStringList &args = {});

// Every rule for the "Encerrar" button: our own user's process, not a kernel
// thread, not protected and not the optimizer itself.
bool mayTerminate(const QString &name, const QStringList &args, uint processUid, uint myUid,
                  int pid, int myPid, bool kernelThread);

} // namespace Policy
