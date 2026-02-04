#ifndef QUESTER_H
#define QUESTER_H

#include <QObject>
#include <QString>
#include <QTimer>
#include <QSocketNotifier>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QAbstractListModel>
#include <QDir>
#include <QStandardPaths>
#include <QQuickWindow>
#include <mpd/client.h>
#include <hwy/highway.h> // Highway SIMD library

struct AlbumItem {
    QString artist; // Added artist for more accurate searches
    QString name;
    QString artUrl;
    QString uri;
    bool artLoading = false;
};

struct TrackItem {
    QString title;
    QString duration;
    QString uri;
};

struct BrowserItem {
    QString name;
    QString path;
    bool isDir;
};

class AlbumModel : public QAbstractListModel
{
    Q_OBJECT
public:
    enum class AlbumRoles : std::uint16_t {
        NameRole = Qt::UserRole + 1,
        ArtRole,
        ArtistRole // Added ArtistRole
    };
    Q_ENUM(AlbumRoles)

    explicit AlbumModel(QObject *parent = nullptr) : QAbstractListModel(parent) {}
    [[nodiscard]] auto rowCount(const QModelIndex &parent = QModelIndex()) const -> int override;
    [[nodiscard]] auto data(const QModelIndex &index, int role = Qt::DisplayRole) const -> QVariant override;
    [[nodiscard]] auto roleNames() const -> QHash<int, QByteArray> override;
    void setAlbums(const QList<AlbumItem> &albums);
    void updateArt(int index, const QString &url);
    QList<AlbumItem> m_albums;
};

class TrackModel : public QAbstractListModel
{
    Q_OBJECT
public:
    enum class TrackRoles : std::uint16_t {
        TitleRole = Qt::UserRole + 1,
        DurationRole,
        UriRole
    };
    Q_ENUM(TrackRoles)

    explicit TrackModel(QObject *parent = nullptr) : QAbstractListModel(parent) {}
    [[nodiscard]] auto rowCount(const QModelIndex &parent = QModelIndex()) const -> int override;
    [[nodiscard]] auto data(const QModelIndex &index, int role = Qt::DisplayRole) const -> QVariant override;
    [[nodiscard]] auto roleNames() const -> QHash<int, QByteArray> override;
    void setTracks(const QList<TrackItem> &tracks);
    QList<TrackItem> m_tracks;
};

class BrowserModel : public QAbstractListModel
{
    Q_OBJECT
public:
    enum class BrowserRoles : std::uint16_t {
        NameRole = Qt::UserRole + 1,
        PathRole,
        IsDirRole
    };
    Q_ENUM(BrowserRoles)

    explicit BrowserModel(QObject *parent = nullptr) : QAbstractListModel(parent) {}
    [[nodiscard]] auto rowCount(const QModelIndex &parent = QModelIndex()) const -> int override;
    [[nodiscard]] auto data(const QModelIndex &index, int role = Qt::DisplayRole) const -> QVariant override;
    [[nodiscard]] auto roleNames() const -> QHash<int, QByteArray> override;
    void setItems(const QList<BrowserItem> &items);
    QList<BrowserItem> m_items;
};

class MpdClient : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString artist READ artist WRITE setArtist NOTIFY artistChanged)
    Q_PROPERTY(QString title READ title WRITE setTitle NOTIFY titleChanged)
    Q_PROPERTY(QString album READ album WRITE setAlbum NOTIFY albumChanged)
    Q_PROPERTY(QString state READ state WRITE setState NOTIFY stateChanged)
    Q_PROPERTY(QString albumArt READ albumArt NOTIFY albumArtChanged)
    Q_PROPERTY(qint64 duration READ duration NOTIFY durationChanged)
    Q_PROPERTY(qint64 elapsed READ elapsed NOTIFY elapsedChanged)
    Q_PROPERTY(AlbumModel* albumModel READ albumModel CONSTANT)
    Q_PROPERTY(TrackModel* trackModel READ trackModel CONSTANT)
    Q_PROPERTY(BrowserModel* browserModel READ browserModel CONSTANT)
    Q_PROPERTY(int currentAlbumIndex READ currentAlbumIndex NOTIFY currentAlbumIndexChanged)
    Q_PROPERTY(QString currentPath READ currentPath NOTIFY currentPathChanged)

public:
    explicit MpdClient(QObject *parent = nullptr);
    ~MpdClient() override;
    MpdClient(const MpdClient&) = delete;
    auto operator=(const MpdClient&) -> MpdClient& = delete;
    MpdClient(MpdClient&&) = delete;
    auto operator=(MpdClient&&) -> MpdClient& = delete;

    [[nodiscard]] auto artist() const -> QString;
    [[nodiscard]] auto title() const -> QString;
    [[nodiscard]] auto album() const -> QString;
    [[nodiscard]] auto state() const -> QString;
    [[nodiscard]] auto albumArt() const -> QString;
    [[nodiscard]] auto duration() const -> qint64;
    [[nodiscard]] auto elapsed() const -> qint64;
    [[nodiscard]] auto albumModel() const -> AlbumModel*;
    [[nodiscard]] auto trackModel() const -> TrackModel*;
    [[nodiscard]] auto browserModel() const -> BrowserModel*;
    [[nodiscard]] auto currentAlbumIndex() const -> int;
    [[nodiscard]] auto currentPath() const -> QString;

    void setWindow(QQuickWindow *window);

public Q_SLOTS:
    void setArtist(const QString &artist);
    void setTitle(const QString &title);
    void setAlbum(const QString &album);
    void setState(const QString &state);

    // Playback controls
    void play();
    void pause();
    void togglePlayPause();
    void next();
    void previous();

    // Library
    void refreshLibrary(); // Existing
    void loadAlbumTracks(int index);
    Q_INVOKABLE void playTrack(const QString &uri);
    Q_INVOKABLE void playAlbum(const QString &artistName, const QString &albumName); // New slot
    Q_INVOKABLE void browsePath(const QString &path);

    // Application/Window controls
    Q_INVOKABLE void quitApplication();
    Q_INVOKABLE void toggleFullscreen();

Q_SIGNALS:
    void artistChanged();
    void titleChanged();
    void albumChanged();
    void stateChanged();
    void albumArtChanged();
    void durationChanged();
    void elapsedChanged();
    void currentAlbumIndexChanged();
    void currentPathChanged();

private Q_SLOTS:
    void updateStatus();
    void handleMpdEvent();

private:
    void fetchAlbumArt(const QString &album);
    void fetchCoverForModel(int index, const QString &albumName);
    void fetchAlbumArtFromAPIs(const QString &artist, const QString &album, const QString &cachePath, bool isMainArt, int modelIndex); // New helper
    auto getMpdPicture(const QString &uri) -> QByteArray;
    void connect();
    void sendIdle();
    void leaveIdle();
    void saveLibraryToCache(const QList<AlbumItem> &albums);
    void loadLibraryFromCache();
    struct SortableSong {
        QString title;
        QString duration;
        QString uri;
        int disc;
        int track;
    };
    auto getSongsForAlbum(const QString &artistName, const QString &albumName) -> QList<SortableSong>;
    struct mpd_connection *m_connection;
    QSocketNotifier *m_notifier;
    QNetworkAccessManager *m_networkManager;
    QTimer *m_timer;

    QString m_artist;
    QString m_title;
    QString m_album;
    QString m_state;
    QString m_albumArt;
    qint64 m_duration = 0;
    qint64 m_elapsed = 0;
    int m_currentSongId = -1;
    QString m_currentUri;
    int m_currentAlbumIndex = -1;
    bool m_isIdle = false;

    QQuickWindow *m_window = nullptr;

    AlbumModel *m_albumModel;
    TrackModel *m_trackModel;
    BrowserModel *m_browserModel;
    QString m_currentPath;
};

#endif // QUESTER_H