#include "audiovisualizer.h"
#include "quester.h"
#include <QFileInfo>
#include <QGuiApplication>
#include <QIcon>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickImageProvider>
#include <QQuickWindow>
#include <QStandardPaths>
#include <QUrl>
#include <QtGlobal>

using namespace Qt::StringLiterals;

class ThemeImageProvider : public QQuickImageProvider
{
public:
    ThemeImageProvider() : QQuickImageProvider(QQuickImageProvider::Pixmap) {}

    QPixmap requestPixmap(const QString &id, QSize *size, const QSize &requestedSize) override
    {
        int width = requestedSize.width() > 0 ? requestedSize.width() : 32;
        int height = requestedSize.height() > 0 ? requestedSize.height() : 32;
        if (size)
            *size = QSize(width, height);

        return QIcon::fromTheme(id).pixmap(width, height);
    }
};

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
    bool startVisualizer = app.arguments().contains("--visualizer");

    QQmlApplicationEngine engine;
    engine.addImageProvider("theme", new ThemeImageProvider);
    engine.rootContext()->setContextProperty("mpdClient", &mpdClient);
    engine.rootContext()->setContextProperty("AudioVisualizer", &audioVisualizer);
    engine.rootContext()->setContextProperty("startInVisualizer", startVisualizer);

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
        QString qmlPath
            = QStandardPaths::locate(QStandardPaths::GenericDataLocation, "Quester/main.qml");
        if (!qmlPath.isEmpty()) {
            engine.addImportPath(QFileInfo(qmlPath).absolutePath());
            url = QUrl::fromLocalFile(qmlPath);
        } else {
            qWarning() << "Could not find main.qml in standard data locations.";
        }
    }
    QObject::connect(
        &engine,
        &QQmlApplicationEngine::objectCreated,
        &app,
        [&](QObject *obj, const QUrl &objUrl) {
            if (!obj && url == objUrl)
                QCoreApplication::exit(-1);

            // Get the main window object from QML and pass it to the MpdClient
            auto window = qobject_cast<QQuickWindow *>(obj);
            if (window) {
                mpdClient.setWindow(window);
            }
        },
        Qt::QueuedConnection);
    engine.load(url);

    return app.exec();
}