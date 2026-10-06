#include "desktopapps.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>

static QString execBaseName(const QString &exec)
{
    // Exec=env FOO=1 /usr/bin/app %U  |  flatpak run --branch=stable org.app.Id
    const QStringList tokens = exec.split(QLatin1Char(' '), Qt::SkipEmptyParts);
    int i = 0;
    if (i < tokens.size() && QFileInfo(tokens.at(i)).fileName() == QLatin1String("env")) {
        ++i;
        while (i < tokens.size() && tokens.at(i).contains(QLatin1Char('=')))
            ++i;
    }
    if (i >= tokens.size())
        return QString();
    QString program = tokens.at(i);
    program.remove(QLatin1Char('"'));
    return QFileInfo(program).fileName();
}

void DesktopApps::addKey(const QString &key, const App &app, bool weak)
{
    const QString lower = key.toLower();
    if (lower.isEmpty())
        return;
    // Visible entries win over NoDisplay helpers; otherwise the first one wins
    if (m_apps.contains(lower) && !(m_weak.value(lower) && !weak))
        return;
    m_apps.insert(lower, app);
    m_weak.insert(lower, weak);
}

void DesktopApps::load()
{
    m_apps.clear();
    m_weak.clear();
    const QStringList dirs = QStandardPaths::standardLocations(QStandardPaths::ApplicationsLocation);
    for (const QString &dirPath : dirs) {
        QDir dir(dirPath);
        const QFileInfoList files = dir.entryInfoList({QStringLiteral("*.desktop")}, QDir::Files);
        for (const QFileInfo &info : files) {
            QFile file(info.filePath());
            if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
                continue;
            QString name, localName, icon, exec, wmClass;
            bool noDisplay = false, inEntry = false;
            while (!file.atEnd()) {
                const QString line = QString::fromUtf8(file.readLine()).trimmed();
                if (line.startsWith(QLatin1Char('['))) {
                    inEntry = line == QLatin1String("[Desktop Entry]");
                    continue;
                }
                if (!inEntry)
                    continue;
                const int eq = line.indexOf(QLatin1Char('='));
                if (eq < 0)
                    continue;
                const QString key = line.left(eq).trimmed();
                const QString value = line.mid(eq + 1).trimmed();
                if (key == QLatin1String("Name"))
                    name = value;
                else if (key == QLatin1String("Name[pt_BR]") || (key == QLatin1String("Name[pt]") && localName.isEmpty()))
                    localName = value;
                else if (key == QLatin1String("Icon"))
                    icon = value;
                else if (key == QLatin1String("Exec"))
                    exec = value;
                else if (key == QLatin1String("StartupWMClass"))
                    wmClass = value;
                else if (key == QLatin1String("NoDisplay"))
                    noDisplay = value == QLatin1String("true");
            }
            if (name.isEmpty())
                continue;
            const App app {localName.isEmpty() ? name : localName, icon};
            const QString id = info.completeBaseName();
            const QString program = execBaseName(exec);
            static const QStringList launchers = {QStringLiteral("flatpak"), QStringLiteral("sh"), QStringLiteral("bash"),
                                                  QStringLiteral("env"), QStringLiteral("python3"), QStringLiteral("python"),
                                                  QStringLiteral("java"), QStringLiteral("xdg-open"), QStringLiteral("pkexec")};
            if (!launchers.contains(program))
                addKey(program, app, noDisplay);
            addKey(wmClass, app, noDisplay);
            addKey(id, app, noDisplay);
            addKey(id.section(QLatin1Char('.'), -1), app, true); // org.kde.dolphin -> dolphin
        }
    }
}

DesktopApps::App DesktopApps::find(const QString &executable) const
{
    return m_apps.value(executable.toLower());
}
