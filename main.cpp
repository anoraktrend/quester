#include "audiovisualizer.h"
#include "quester.h"
#include <QFileInfo>
#include <QGuiApplication>
#include <QIcon>
#include <QQmlApplicationEngine>
#include <QTranslator>
#include <QLocale>
#include <QQmlContext>
#include <QQuickImageProvider>
#include <QQuickWindow>
#include <QStandardPaths>
#include <QUrl>
#include <QtGlobal>
#include <hwy/highway.h> // Highway SIMD library

using namespace Qt::StringLiterals;

constexpr int DEFAULT_ICON_SIZE = 32;
constexpr int BLUR_THUMB_SIZE = 64;

class ThemeImageProvider : public QQuickImageProvider
{
public:
    ThemeImageProvider() : QQuickImageProvider(QQuickImageProvider::Pixmap) {}

    auto requestPixmap(const QString &id, QSize *size, const QSize &requestedSize) -> QPixmap override
    {
        int width = requestedSize.width() > 0 ? requestedSize.width() : DEFAULT_ICON_SIZE;
        int height = requestedSize.height() > 0 ? requestedSize.height() : DEFAULT_ICON_SIZE;
        if (size)
            *size = QSize(width, height);

        return QIcon::fromTheme(id).pixmap(width, height);
    }
};

class BlurImageProvider : public QQuickImageProvider
{
public:
    BlurImageProvider() : QQuickImageProvider(QQuickImageProvider::Image) {}

    auto requestImage(const QString &id, QSize *size, const QSize &requestedSize) -> QImage override
    {
        QImage img;
        if (id.startsWith("data:")) {
            QString base64 = id.mid(id.indexOf(",") + 1);
            img.loadFromData(QByteArray::fromBase64(base64.toLatin1()));
        } else {
            QUrl url(id);
            img.load(url.isLocalFile() ? url.toLocalFile() : id);
        }

        if (img.isNull()) return {};

        // "Highway" Blur: Fast path using downscaling
        // Scaling down averages pixels (box blur), scaling up interpolates (smooths).
        // This is extremely efficient and provides a high-quality background blur.
        if (img.width() > BLUR_THUMB_SIZE) {
            img = img.scaledToWidth(BLUR_THUMB_SIZE, Qt::SmoothTransformation);
        }

        return img;
    }
};

auto main(int argc, char *argv[]) -> int
{
    QGuiApplication app(argc, argv);
    QString iconPath;
#ifdef QT_QML_SOURCE_DIR
    iconPath = QStringLiteral(QT_QML_SOURCE_DIR) + "/Quester.svg";
#endif
    app.setWindowIcon(QIcon::fromTheme("quester", QIcon(iconPath)));

    QTranslator translator;
    const QStringList uiLanguages = QLocale::system().uiLanguages();
    for (const QString &locale : uiLanguages) {
        const QString baseName = "Quester_" + QLocale(locale).name();
        if (translator.load(":/i18n/" + baseName)) {
            app.installTranslator(&translator);
            break;
        }
    }

    MpdClient mpdClient;
    AudioVisualizer audioVisualizer;
    bool startVisualizer = app.arguments().contains("--visualizer");

    QQmlApplicationEngine engine;
    engine.addImageProvider("theme", new ThemeImageProvider); // NOLINT(cppcoreguidelines-owning-memory)
    engine.addImageProvider("blur", new BlurImageProvider); // NOLINT(cppcoreguidelines-owning-memory)
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
        [&](QObject *obj, const QUrl &objUrl) -> void {
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