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
#include "audiovisualizer.h"

using namespace Qt::StringLiterals;

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);
    QString iconPath;
#ifdef QT_QML_SOURCE_DIR
    iconPath = QStringLiteral(QT_QML_SOURCE_DIR) + "/Quester.svg";
#endif
    app.setWindowIcon(QIcon::fromTheme("quester", QIcon(iconPath)));

    MpdClient mpdClient;
    AudioVisualizer audioVisualizer;

    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty("mpdClient", &mpdClient);
    engine.rootContext()->setContextProperty("AudioVisualizer", &audioVisualizer);

    QUrl url;
    bool found = false;

    // The QT_QML_SOURCE_DIR macro is set by CMake to the project's source directory.
    // This allows the application to run directly from the build directory for development.
#ifdef QT_QML_SOURCE_DIR
    QFileInfo srcEntry(QStringLiteral(QT_QML_SOURCE_DIR) + "/main.qml");
    if (srcEntry.exists()) {
        url = QUrl::fromLocalFile(srcEntry.absoluteFilePath());
        found = true;
    }
#endif

    if (!found) {
#ifdef APP_DATADIR
        QFileInfo appDataEntry(QStringLiteral(APP_DATADIR) + "/main.qml");
        if (appDataEntry.exists()) {
            engine.addImportPath(appDataEntry.absolutePath());
            url = QUrl::fromLocalFile(appDataEntry.absoluteFilePath());
            found = true;
        }
#endif
    }

    if (!found) {
    // When installed, the QML file is in a standard data location.
    // We use QStandardPaths to locate it robustly.
    QString qmlPath = QStandardPaths::locate(QStandardPaths::GenericDataLocation, "Quester/main.qml");
    if (!qmlPath.isEmpty()) {
        engine.addImportPath(QFileInfo(qmlPath).absolutePath());
        url = QUrl::fromLocalFile(qmlPath);
    } else {
        qWarning() << "Could not find main.qml in standard data locations.";
    }
    }
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