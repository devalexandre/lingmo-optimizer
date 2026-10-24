#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>

#include "cleaner.h"
#include "devtools.h"
#include "fileactions.h"
#include "processmonitor.h"
#include "storageanalyzer.h"
#include "suggestions.h"
#include "systeminfo.h"

#ifdef HAVE_LINGMO_INTENTS
#include "optimizerintents.h"
#endif

int main(int argc, char *argv[])
{
#ifdef HAVE_LINGMO_INTENTS
    // D-Bus activation for the app actions: no window
    if (OptimizerIntents::requested(argc, argv))
        return OptimizerIntents::run(argc, argv);
#endif
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
    DevTools devTools;
    Cleaner cleaner;
    cleaner.setDevTools(&devTools);
    FileActions files;

    // --page cpu|memory|storage|developer: open straight at that page
    QString startPage;
    const QStringList args = app.arguments();
    const int pageArg = args.indexOf(QStringLiteral("--page"));
    if (pageArg > 0 && pageArg + 1 < args.size())
        startPage = args.at(pageArg + 1);

    QQmlApplicationEngine engine;
    QQmlContext *context = engine.rootContext();
    context->setContextProperty("startPage", startPage);
    context->setContextProperty("systemInfo", &systemInfo);
    context->setContextProperty("processes", &processes);
    context->setContextProperty("storage", &storage);
    context->setContextProperty("suggestions", &suggestions);
    context->setContextProperty("cleaner", &cleaner);
    context->setContextProperty("devtools", &devTools);
    context->setContextProperty("files", &files);
    engine.load(QUrl(QStringLiteral("qrc:/qml/Main.qml")));

    if (engine.rootObjects().isEmpty())
        return -1;

    return app.exec();
}
