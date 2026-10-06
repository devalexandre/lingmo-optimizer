#pragma once

#include <QFuture>
#include <QObject>
#include <QVariantList>

#include <atomic>
#include <memory>

struct ScanProgress;

// "Sugestões": known space hogs, each with its size, an explanation and the
// right action. Computed in the background (it walks the home folder).
//
// Item: {id, title, description, size, path, icon, action, command, children}
//   action: "pacman-cache" | "journal" | "flatpak" (Cleaner steps),
//           "user-cache", "empty-trash", "open", "none"
//   children: [{name, path, size, detail, trashPaths, canTrash, ollamaName}]
class Suggestions : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QVariantList items READ items NOTIFY itemsChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(bool loaded READ loaded NOTIFY itemsChanged)

public:
    explicit Suggestions(QObject *parent = nullptr);
    ~Suggestions() override;

    QVariantList items() const { return m_items; }
    bool busy() const { return m_busy; }
    bool loaded() const { return m_loaded; }

    Q_INVOKABLE void refresh();
    Q_INVOKABLE void removeOllamaModel(const QString &name);

signals:
    void itemsChanged();
    void busyChanged();
    void ollamaRemoved(const QString &name, bool ok, const QString &message);

private:
    QVariantList m_items;
    bool m_busy = false;
    bool m_loaded = false;
    std::shared_ptr<ScanProgress> m_progress;
    QFuture<QVariantList> m_future;
};
