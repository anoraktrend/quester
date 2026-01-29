#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QUrl>
#include <QtGlobal>
#include <QIcon>
#include <QQuickWindow>
#include <QStandardPaths>
#include <QFileInfo>
#include "quester.h"

using namespace Qt::StringLiterals;

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);
    app.setWindowIcon(QIcon("/usr/share/icons/hicolor/scalable/apps/quester.svg"));

    QQmlApplicationEngine engine;

    MpdClient mpdClient;
    engine.rootContext()->setContextProperty("mpdClient", &mpdClient);

    QUrl url;
    // The QT_QML_SOURCE_DIR macro is set by CMake to the project's source directory.
    // This allows the application to run directly from the build directory for development.
#ifdef QT_QML_SOURCE_DIR
    url = QUrl::fromLocalFile(QStringLiteral(QT_QML_SOURCE_DIR) + "/main.qml");
#else
    // When installed, the QML file is in a standard data location.
    // We use QStandardPaths to locate it robustly.
    QString qmlPath = QStandardPaths::locate(QStandardPaths::GenericDataLocation, "Quester/main.qml");
    if (!qmlPath.isEmpty()) {
        engine.addImportPath(QFileInfo(qmlPath).absolutePath());
        url = QUrl::fromLocalFile(qmlPath);
    } else {
        qWarning() << "Could not find main.qml in standard data locations.";
    }
#endif
    QObject::connect(&engine, &QQmlApplicationEngine::objectCreated,
        &app, [&](QObject *obj, const QUrl &objUrl) {
            if (!obj && url == objUrl)
                QCoreApplication::exit(-1);

            // Get the main window object from QML and pass it to the MpdClient
            auto window = qobject_cast<QQuickWindow*>(obj);
            if (window) {
                mpdClient.setWindow(window);
            }
        }, Qt::QueuedConnection);
    engine.load(url);

    return app.exec();
}