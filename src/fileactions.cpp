#include "fileactions.h"

#include "core/diskusage.h"
#include "core/format.h"
#include "core/policy.h"

#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QStandardPaths>
#include <QUrl>

#include <unistd.h>

FileActions::FileActions(QObject *parent)
    : QObject(parent)
{
}

QString FileActions::homePath() const
{
    return QDir::homePath();
}

bool FileActions::canMoveToTrash(const QString &path) const
{
    return Policy::mayMoveToTrash(path, QDir::homePath(), ::getuid());
}

QString FileActions::trashBlockReason(const QString &path) const
{
    using V = Policy::TrashVerdict;
    switch (Policy::trashVerdict(path, QDir::homePath(), ::getuid())) {
    case V::Allowed: return QString();
    case V::Missing: return tr("O item não existe mais");
    case V::NotAbsolute:
    case V::OutsideHome: return tr("Fora da sua pasta pessoal: o Otimizador não apaga arquivos do sistema");
    case V::IsHome: return tr("É a sua pasta pessoal");
    case V::Protected: return tr("Configurações e dados de programas: apague pelo próprio programa");
    case V::InTrash: return tr("Já está na lixeira");
    case V::NotOwned: return tr("Pertence a outro usuário");
    case V::SymlinkEscape: return tr("Está atrás de um link simbólico que sai da pasta pessoal");
    }
    return QString();
}

bool FileActions::moveToTrash(const QString &path)
{
    if (!canMoveToTrash(path)) {
        emit error(trashBlockReason(path));
        return false;
    }
    // QFile::moveToTrash renames the entry itself: a symlink goes as a link
    if (!QFile::moveToTrash(path)) {
        emit error(tr("Não foi possível mover \"%1\" para a lixeira").arg(QFileInfo(path).fileName()));
        return false;
    }
    emit movedToTrash(path);
    return true;
}

void FileActions::openInFileManager(const QString &path)
{
    const QFileInfo info(path);
    const bool isDir = info.isDir() && !info.isSymLink();
    const QString folder = isDir ? info.absoluteFilePath() : info.absolutePath();

    // Lingmo's own file manager (it opens folders; it cannot select a file)
    const QString lingmoFm = QStandardPaths::findExecutable(QStringLiteral("lingmo-filemanager"));
    if (!lingmoFm.isEmpty()) {
        QProcess::startDetached(lingmoFm, {folder});
        return;
    }
    if (isDir) {
        QDesktopServices::openUrl(QUrl::fromLocalFile(folder));
        return;
    }
    // Other desktops: ask the file manager to show the file selected
    QDBusMessage call = QDBusMessage::createMethodCall(QStringLiteral("org.freedesktop.FileManager1"),
                                                       QStringLiteral("/org/freedesktop/FileManager1"),
                                                       QStringLiteral("org.freedesktop.FileManager1"),
                                                       QStringLiteral("ShowItems"));
    call << QStringList {QUrl::fromLocalFile(info.absoluteFilePath()).toString()} << QString();
    auto *watcher = new QDBusPendingCallWatcher(QDBusConnection::sessionBus().asyncCall(call, 5000), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [folder](QDBusPendingCallWatcher *w) {
        if (w->isError())
            QDesktopServices::openUrl(QUrl::fromLocalFile(folder));
        w->deleteLater();
    });
}

double FileActions::emptyTrash()
{
    const QString trash = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + QStringLiteral("/Trash");
    qint64 freed = 0;
    for (const char *sub : {"files", "info", "expunged"})
        freed += removeDirectoryContents(trash + QLatin1Char('/') + QLatin1String(sub));
    QFile::remove(trash + QStringLiteral("/directorysizes"));
    return double(freed);
}

QString FileActions::formatBytes(double bytes) const
{
    return Format::bytes(qint64(bytes));
}

QString FileActions::formatPercent(double fraction) const
{
    return Format::percent(fraction);
}
