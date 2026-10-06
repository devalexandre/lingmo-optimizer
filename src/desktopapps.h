#pragma once

#include <QHash>
#include <QString>

// Maps executable names to the application's name and icon, from the
// installed .desktop files ("firefox" -> "Firefox", icon "firefox").
class DesktopApps
{
public:
    struct App {
        QString name;
        QString icon;
    };

    void load();                         // reads every applications/ folder
    App find(const QString &executable) const;
    bool isEmpty() const { return m_apps.isEmpty(); }

private:
    void addKey(const QString &key, const App &app, bool weak);
    QHash<QString, App> m_apps;          // lower-case key -> app
    QHash<QString, bool> m_weak;         // key came from a NoDisplay entry
};
