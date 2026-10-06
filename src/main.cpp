#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include "optimizer.h"

int main(int argc, char *argv[]) {
    QGuiApplication app(argc, argv);
    QGuiApplication::setOrganizationName("lingmoos");
    QGuiApplication::setApplicationName("lingmo-optimizer");

    Optimizer optimizer;

    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty("optimizer", &optimizer);
    engine.load(QUrl(QStringLiteral("qrc:/qml/Main.qml")));

    if (engine.rootObjects().isEmpty())
        return -1;

    return app.exec();
}
