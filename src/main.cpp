#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>

#include "cleaner.h"
#include "fileactions.h"
#include "processmonitor.h"
#include "storageanalyzer.h"
#include "suggestions.h"
#include "systeminfo.h"

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);
    QGuiApplication::setOrganizationName("lingmoos");
    QGuiApplication::setApplicationName("lingmo-optimizer");
    QGuiApplication::setApplicationDisplayName(QStringLiteral("Otimizador"));
    QGuiApplication::setDesktopFileName(QStringLiteral("lingmo-optimizer"));

    qmlRegisterUncreatableType<AppGroupModel>("Lingmo.Optimizer", 1, 0, "AppGroupModel", QStringLiteral("from processes"));
    qmlRegisterUncreatableType<DirModel>("Lingmo.Optimizer", 1, 0, "DirModel", QStringLiteral("from storage"));

    SystemInfo systemInfo;
    ProcessMonitor processes;
    StorageAnalyzer storage;
    Suggestions suggestions;
    Cleaner cleaner;
    FileActions files;

    QQmlApplicationEngine engine;
    QQmlContext *context = engine.rootContext();
    context->setContextProperty("systemInfo", &systemInfo);
    context->setContextProperty("processes", &processes);
    context->setContextProperty("storage", &storage);
    context->setContextProperty("suggestions", &suggestions);
    context->setContextProperty("cleaner", &cleaner);
    context->setContextProperty("files", &files);
    engine.load(QUrl(QStringLiteral("qrc:/qml/Main.qml")));

    if (engine.rootObjects().isEmpty())
        return -1;

    return app.exec();
}
