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
#include <QLocalServer>
#include <QLocalSocket>
#include <QFile>
#include <QTextStream>
#include <QDateTime>
#include <QDir>
#include <QMutex>
#include <csignal>
#include <execinfo.h>


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

        // Fast Blur: Fast path using downscaling
        // Scaling down averages pixels (box blur), scaling up interpolates (smooths).
        if (img.width() > BLUR_THUMB_SIZE) {
            img = img.scaledToWidth(BLUR_THUMB_SIZE, Qt::SmoothTransformation);
        }

        return img;
    }
};

static QFile *g_logFile = nullptr;
static QMutex g_logMutex;
static bool g_isDetached = false;

void crashHandler(int sig)
{
    // KISS: A minimal crash handler using standard POSIX calls.
    // It avoids allocating memory (which is unsafe in signal handlers) and writes directly to the file descriptor.
    if (g_logFile) {
        int fd = g_logFile->handle();
        if (fd != -1) {
            const char msg[] = "\nFATAL: Application crashed. Stack trace:\n";
            if (write(fd, msg, sizeof(msg) - 1) == -1) {}
            
            void *array[64];
            int size = backtrace(array, 64);
            backtrace_symbols_fd(array, size, fd);
            fsync(fd);
        }
    }
    signal(sig, SIG_DFL);
    raise(sig);
}

void myMessageHandler(QtMsgType type, const QMessageLogContext &context, const QString &msg)
{
    Q_UNUSED(context);
    QMutexLocker locker(&g_logMutex);
    
    QString txt;
    switch (type) {
    case QtDebugMsg:    txt = QStringLiteral("DEBUG: %1").arg(msg); break;
    case QtInfoMsg:     txt = QStringLiteral("INFO: %1").arg(msg); break;
    case QtWarningMsg:  txt = QStringLiteral("WARNING: %1").arg(msg); break;
    case QtCriticalMsg: txt = QStringLiteral("CRITICAL: %1").arg(msg); break;
    case QtFatalMsg:    txt = QStringLiteral("FATAL: %1").arg(msg); break;
    }

    if (g_logFile && g_logFile->isOpen()) {
        QTextStream out(g_logFile);
        out << QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss.zzz ")) << txt << Qt::endl;
    }
    
    // Also write to stderr for console output
    if (!g_isDetached) {
        fprintf(stderr, "%s\n", txt.toLocal8Bit().constData());
        fflush(stderr);
    }
}

auto main(int argc, char *argv[]) -> int
{
    // Command-line argument parsing
    bool shouldDetach = true;
    bool showHelp = false;
    bool startMinimized = false;
    bool startFullscreen = false;
    QString presetPath;
    QString audioSource;
    QString startView = "library";  // Possible values: library, visualizer, queue, playlists, wrapped
    QString startViewMode = "flow"; // Possible values: flow, grid, browser

    int newArgc = 0;
    for (int i = 0; i < argc; ++i) {
        const char *arg = argv[i];
        
        if (std::strcmp(arg, "--help") == 0 || std::strcmp(arg, "-h") == 0) {
            showHelp = true;
        } else if (std::strcmp(arg, "--no-detach") == 0) {
            shouldDetach = false;
        } else if (std::strcmp(arg, "--visualizer") == 0 || std::strcmp(arg, "-v") == 0) {
            startView = "visualizer";
        } else if (std::strcmp(arg, "--minimized") == 0 || std::strcmp(arg, "-m") == 0) {
            startMinimized = true;
        } else if (std::strcmp(arg, "--fullscreen") == 0 || std::strcmp(arg, "-f") == 0) {
            startFullscreen = true;
        } else if (std::strcmp(arg, "--preset-path") == 0 && (i + 1 < argc)) {
            presetPath = QString::fromUtf8(argv[++i]);
        } else if (std::strcmp(arg, "--audio-source") == 0 && (i + 1 < argc)) {
            audioSource = QString::fromUtf8(argv[++i]);
        } else if (std::strcmp(arg, "--view") == 0 && (i + 1 < argc)) {
            startView = QString::fromUtf8(argv[++i]).toLower();
        } else if (std::strcmp(arg, "--view-mode") == 0 && (i + 1 < argc)) {
            startViewMode = QString::fromUtf8(argv[++i]).toLower();
        } else {
            argv[newArgc++] = argv[i];
        }
    }
    argc = newArgc;
    argv[argc] = nullptr;

    // Show help
    if (showHelp) {
        printf("Quester - Music Player with Visualization\n\n");
        printf("Usage: quester [OPTIONS]\n\n");
        printf("Options:\n");
        printf("  --help, -h                Show this help message\n");
        printf("  --no-detach               Run in foreground (do not detach from terminal)\n");
        printf("  --visualizer, -v          Start directly in visualizer mode\n");
        printf("  --view <view>             Start in specific view (library, visualizer, queue, playlists, wrapped)\n");
        printf("  --view-mode <mode>        Start with specific coverflow mode (flow, grid, browser)\n");
        printf("  --minimized, -m           Start minimized to system tray\n");
        printf("  --fullscreen, -f          Start in fullscreen mode\n");
        printf("  --preset-path <path>      Set custom projectM preset path\n");
        printf("  --audio-source <source>   Set audio input source (pulseaudio, pipewire, fifo)\n");
        printf("\n");
        printf("Examples:\n");
        printf("  quester                   Start normally in library flow view\n");
        printf("  quester --no-detach       Run in terminal\n");
        printf("  quester --visualizer      Start in visualizer mode\n");
        printf("  quester --view queue      Start in queue view\n");
        printf("  quester --view library --view-mode grid  Start in library grid view\n");
        printf("  quester --preset-path ~/my-presets --audio-source pipewire\n");
        return 0;
    }

    if (shouldDetach) {
        pid_t pid = fork();
        if (pid < 0) return 1;
        if (pid > 0) return 0; // Parent exits

        setsid(); // Create new session

        // Second fork to prevent acquiring controlling terminal
        pid = fork();
        if (pid < 0) return 1;
        if (pid > 0) return 0; // First child exits

        // Redirect standard streams to /dev/null
        int devNull = open("/dev/null", O_RDWR);
        if (devNull != -1) {
            dup2(devNull, STDIN_FILENO);
            dup2(devNull, STDOUT_FILENO);
            dup2(devNull, STDERR_FILENO);
            if (devNull > 2) close(devNull);
        }
    }

    g_isDetached = shouldDetach;

    QApplication app(argc, argv);
    app.setOrganizationName(QStringLiteral("Quester"));
    app.setOrganizationDomain(QStringLiteral("helltop.net"));
    app.setApplicationName(QStringLiteral("Quester"));

    // Setup Logging
    QString cacheDir = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
    QDir dir(cacheDir);
    if (!dir.exists()) dir.mkpath(QStringLiteral("."));
    
    QString logPath = cacheDir + "/quest.log";

    // 1. Detect unrenamed file (crash/unclean exit)
    if (QFile::exists(logPath)) {
        QFileInfo info(logPath);
        QString dateStr = info.lastModified().toString(QStringLiteral("yyyy-MM-dd-HH-mm-ss"));
        QString crashName = dir.filePath(QStringLiteral("quest-%1-crash.log").arg(dateStr));
        QFile::rename(logPath, crashName);
    }

    // KISS: Simple file-based log rotation. No need for a complex logging framework
    // when we just want to keep the last few runs for debugging.
    // 2. Rotate logs (Keep last 5)
    dir.setNameFilters(QStringList() << "quest-*.log");
    dir.setSorting(QDir::Time); // Newest first
    QFileInfoList logs = dir.entryInfoList();
    while (logs.size() >= 5) {
        QFile::remove(logs.last().absoluteFilePath());
        logs.removeLast();
    }

    g_logFile = new QFile(logPath);
    if (g_logFile->open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
        // Redirect stdout/stderr to log file if detached to capture library output
        if (g_isDetached) {
            int fd = g_logFile->handle();
            if (fd != -1) {
                dup2(fd, STDOUT_FILENO);
                dup2(fd, STDERR_FILENO);
            }
        }
        qInstallMessageHandler(myMessageHandler);
        
        // Install Signal Handlers
        signal(SIGSEGV, crashHandler);
        signal(SIGABRT, crashHandler);
        signal(SIGFPE, crashHandler);
        signal(SIGILL, crashHandler);
        signal(SIGBUS, crashHandler);
    }

    QObject::connect(&app, &QCoreApplication::aboutToQuit, []() {
        QMutexLocker locker(&g_logMutex);
        if (g_logFile) {
            QString oldName = g_logFile->fileName();
            g_logFile->close();
            delete g_logFile;
            g_logFile = nullptr;
            
            QString dateStr = QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd-HH-mm-ss"));
            QString newName = QFileInfo(oldName).dir().filePath(QStringLiteral("quest-%1.log").arg(dateStr));
            
            if (QFile::exists(newName)) QFile::remove(newName);
            QFile::rename(oldName, newName);
        }
    });

    // Single Instance Check
    QString serverName = QStringLiteral("QuesterSingleInstance");
    QString user = qgetenv("USER");
    if (user.isEmpty()) {
        serverName += QStringLiteral("-") + QString::number(getuid());
    } else {
        serverName += QStringLiteral("-") + user;
    }

    QLocalSocket socket;
    socket.connectToServer(serverName);
    if (socket.waitForConnected(500)) {
        // Another instance is running
        socket.write("ACTIVATE");
        socket.waitForBytesWritten(1000);
        socket.disconnectFromServer();
        return 0;
    }

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

    // Start Single Instance Server
    QLocalServer singleInstanceServer;
    QLocalServer::removeServer(serverName); // Clean up stale socket
    if (singleInstanceServer.listen(serverName)) {
        QObject::connect(&singleInstanceServer, &QLocalServer::newConnection, &mpdClient, [&singleInstanceServer, &mpdClient]() {
            QLocalSocket *clientConnection = singleInstanceServer.nextPendingConnection();
            QObject::connect(clientConnection, &QLocalSocket::readyRead, [clientConnection, &mpdClient]() {
                QByteArray data = clientConnection->readAll();
                if (data.startsWith("ACTIVATE")) {
                    if (auto *win = mpdClient.window()) {
                        win->show();
                        win->raise();
                        win->requestActivate();
                    }
                }
            });
            QObject::connect(clientConnection, &QLocalSocket::disconnected, clientConnection, &QLocalSocket::deleteLater);
        });
    }

    qDBusRegisterMetaType<QList<QDBusObjectPath>>();
    qDBusRegisterMetaType<QList<QVariantMap>>();
    qRegisterMetaType<MprisPlaylist>("MprisPlaylist");
    qDBusRegisterMetaType<MprisPlaylist>();
    qRegisterMetaType<QList<MprisPlaylist>>("QList<MprisPlaylist>");
    qDBusRegisterMetaType<QList<MprisPlaylist>>();
    qRegisterMetaType<MprisActivePlaylist>("MprisActivePlaylist");
    qDBusRegisterMetaType<MprisActivePlaylist>();

    // Apply command-line settings to QSettings for persistence
    QSettings settings("Quester", "Quester");
    if (!presetPath.isEmpty()) {
        settings.setValue("projectMPresetPath", presetPath);
    }
    if (!audioSource.isEmpty()) {
        settings.setValue("audioSource", audioSource);
        mpdClient.setAudioSource(audioSource);
        audioVisualizer.setAudioSource(audioSource);
    }

    qmlRegisterUncreatableType<MpdClient>("Quester", 1, 0, "MpdClient", "Enums");
    qmlRegisterUncreatableType<DBusService>("Quester", 1, 0, "DBusService", "Enums");
    qmlRegisterUncreatableType<StatisticsManager>("Quester", 1, 0, "StatisticsManager", "Managed by MpdClient");
    qmlRegisterType<ProjectMVisualizer>("Quester", 1, 0, "ProjectMVisualizer");

    QQmlApplicationEngine engine;
    engine.addImageProvider(QStringLiteral("theme"), new ThemeImageProvider); // NOLINT(cppcoreguidelines-owning-memory)
    engine.addImageProvider(QStringLiteral("blur"), new BlurImageProvider); // NOLINT(cppcoreguidelines-owning-memory)
    engine.rootContext()->setContextProperty(QStringLiteral("mpdClient"), &mpdClient);
    engine.rootContext()->setContextProperty(QStringLiteral("dbusService"), &dbusService);
    engine.rootContext()->setContextProperty(QStringLiteral("AudioVisualizer"), &audioVisualizer);
    engine.rootContext()->setContextProperty(QStringLiteral("startView"), startView);
    engine.rootContext()->setContextProperty(QStringLiteral("startViewMode"), startViewMode);
    engine.rootContext()->setContextProperty(QStringLiteral("startMinimized"), startMinimized);
    engine.rootContext()->setContextProperty(QStringLiteral("startFullscreen"), startFullscreen);

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

    QObject::connect(&mpdClient, &MpdClient::audioSourceChanged, &audioVisualizer, [&mpdClient, &audioVisualizer]() {
        audioVisualizer.setAudioSource(mpdClient.audioSource());
    });

    QObject::connect(&app, &QCoreApplication::aboutToQuit, &mpdClient, &MpdClient::cleanup);
    QObject::connect(&app, &QCoreApplication::aboutToQuit, &audioVisualizer, &AudioVisualizer::stop);

    engine.load(url);

    return app.exec();
}
