#pragma once

#include <QObject>
#include <QString>

// Actions on files shown by the storage pages: open in the file manager and
// move to the trash (only what Policy::trashVerdict allows).
class FileActions : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString homePath READ homePath CONSTANT)

public:
    explicit FileActions(QObject *parent = nullptr);

    QString homePath() const;

    Q_INVOKABLE bool canMoveToTrash(const QString &path) const;
    // Why the trash is not offered, in Portuguese (empty when it is)
    Q_INVOKABLE QString trashBlockReason(const QString &path) const;
    Q_INVOKABLE bool moveToTrash(const QString &path);
    Q_INVOKABLE void openInFileManager(const QString &path);
    // Empties ~/.local/share/Trash; returns the bytes freed
    Q_INVOKABLE double emptyTrash();
    Q_INVOKABLE QString formatBytes(double bytes) const;
    Q_INVOKABLE QString formatPercent(double fraction) const;

signals:
    void movedToTrash(const QString &path);
    void error(const QString &message);
};
