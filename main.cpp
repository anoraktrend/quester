#include "audiovisualizer.h"
#include "projectmvisualizer.h"
#include "quester.h"
#include "dbus.h"
#include <QFileInfo>
#include <QApplication>
#include <QIcon>
#include <QQmlApplicationEngine>
#include <QTranslator>
#include <QLocale>
#include <QQmlContext>
#include <QtQml>
#include <QQuickImageProvider>
#include <QQuickWindow>
#include <QtQuickControls2/QQuickStyle>
#include <QStandardPaths>
#include <QUrl>
#include <QtGlobal>
#include <QDBusMetaType>
#include <unistd.h>
#include <fcntl.h>
#include <cstring>


using namespace Qt::StringLiterals;

constexpr int DEFAULT_ICON_SIZE = 32;
constexpr int BLUR_THUMB_SIZE = 64;

class ThemeImageProvider : public QQuickImageProvider
{
public:
    ThemeImageProvider() : QQuickImageProvider(QQuickImageProvider::Pixmap) {}

    auto requestPixmap(const QString &id, QSize * /*size*/, const QSize &requestedSize) -> QPixmap override
    {
        int width = requestedSize.width() > 0 ? requestedSize.width() : DEFAULT_ICON_SIZE;
        int height = requestedSize.height() > 0 ? requestedSize.height() : DEFAULT_ICON_SIZE;

        return QIcon::fromTheme(id).pixmap(width, height);
    }
};

class BlurImageProvider : public QQuickImageProvider
{
public:
    BlurImageProvider() : QQuickImageProvider(QQuickImageProvider::Image) {}

    auto requestImage(const QString &id, QSize * /*size*/, const QSize & /*requestedSize*/) -> QImage override
    {
        QImage img;
        if (id.startsWith(QStringLiteral("data:"))) {
            QString base64 = id.mid(id.indexOf(u',') + 1);
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
    // Check for --detach argument
    bool shouldDetach = false;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--detach") == 0) {
            shouldDetach = true;
            break;
        }
    }

    if (shouldDetach) {
        if (fork() > 0) return 0; // Parent exits
        setsid(); // Create new session

        // Redirect standard streams to /dev/null
        int devNull = open("/dev/null", O_RDWR);
        if (devNull != -1) {
            dup2(devNull, STDIN_FILENO);
            dup2(devNull, STDOUT_FILENO);
            dup2(devNull, STDERR_FILENO);
            if (devNull > 2) close(devNull);
        }
    }

    QApplication app(argc, argv);
    app.setOrganizationName(QStringLiteral("Quester"));
    app.setOrganizationDomain(QStringLiteral("helltop.net"));
    app.setApplicationName(QStringLiteral("Quester"));

    if (qEnvironmentVariableIsEmpty("QT_QUICK_CONTROLS_STYLE")) {
        QQuickStyle::setStyle("Fusion");
    }

    QString iconPath;
#ifdef QT_QML_SOURCE_DIR
    iconPath = QStringLiteral(QT_QML_SOURCE_DIR) + "/Quester.svg";
#endif
    app.setWindowIcon(QIcon::fromTheme(QStringLiteral("quester"), QIcon(iconPath)));

    QTranslator translator;
    const QStringList uiLanguages = QLocale::system().uiLanguages();
    for (const QString &locale : uiLanguages) {
        const QString baseName = QStringLiteral("Quester_") + QLocale(locale).name();
        if (translator.load(QStringLiteral(":/i18n/") + baseName)) {
            app.installTranslator(&translator);
            break;
        }
    }

    MpdClient mpdClient;
    DBusService dbusService(&mpdClient);
    AudioVisualizer audioVisualizer;

    qDBusRegisterMetaType<QList<QDBusObjectPath>>();
    qDBusRegisterMetaType<QList<QVariantMap>>();
    qRegisterMetaType<MprisPlaylist>("MprisPlaylist");
    qDBusRegisterMetaType<MprisPlaylist>();
    qRegisterMetaType<QList<MprisPlaylist>>("QList<MprisPlaylist>");
    qDBusRegisterMetaType<QList<MprisPlaylist>>();
    qRegisterMetaType<MprisActivePlaylist>("MprisActivePlaylist");
    qDBusRegisterMetaType<MprisActivePlaylist>();

    bool startVisualizer = app.arguments().contains(QStringLiteral("--visualizer"));

    qmlRegisterUncreatableType<MpdClient>("Quester", 1, 0, "MpdClient", "Enums");
    qmlRegisterUncreatableType<DBusService>("Quester", 1, 0, "DBusService", "Enums");
    qmlRegisterType<ProjectMVisualizer>("Quester", 1, 0, "ProjectMVisualizer");

    QQmlApplicationEngine engine;
    engine.addImageProvider(QStringLiteral("theme"), new ThemeImageProvider); // NOLINT(cppcoreguidelines-owning-memory)
    engine.addImageProvider(QStringLiteral("blur"), new BlurImageProvider); // NOLINT(cppcoreguidelines-owning-memory)
    engine.rootContext()->setContextProperty(QStringLiteral("mpdClient"), &mpdClient);
    engine.rootContext()->setContextProperty(QStringLiteral("dbusService"), &dbusService);
    engine.rootContext()->setContextProperty(QStringLiteral("AudioVisualizer"), &audioVisualizer);
    engine.rootContext()->setContextProperty(QStringLiteral("startInVisualizer"), startVisualizer);

const QUrl url(u"qrc:/qml/net/helltop/quester/qml/main.qml"_s);

    QObject::connect(
        &engine,
        &QQmlApplicationEngine::objectCreated,
        &app,
        [=, &mpdClient](QObject *obj, const QUrl &objUrl) -> void {
            if (!obj && url == objUrl) {
                QCoreApplication::exit(-1);
            }
            // Get the main window object from QML and pass it to the MpdClient
            auto window = qobject_cast<QQuickWindow *>(obj);
            if (window) {
                mpdClient.setWindow(window);
                // Initialize system tray after window is available
                mpdClient.setupSystemTray();
            }
        },
        Qt::QueuedConnection);

    QObject::connect(&app, &QCoreApplication::aboutToQuit, &mpdClient, &MpdClient::cleanup);
    QObject::connect(&app, &QCoreApplication::aboutToQuit, &audioVisualizer, &AudioVisualizer::stop);

    engine.load(url);

    return app.exec();
}
