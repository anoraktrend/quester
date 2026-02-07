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
    QString mbid; // MusicBrainz ID for precise album identification
    QString uri;
    bool artLoading = false;
    int year = 0;
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

struct QueueItem {
    int id;
    QString title;
    QString artist;
    QString album;
    QString duration;
    QString uri;
};

class AlbumModel : public QAbstractListModel
{
    Q_OBJECT
public:
    enum class AlbumRoles : std::uint16_t {
        NameRole = Qt::UserRole + 1,
        ArtRole,
        ArtistRole,
        YearRole,
        MbidRole
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

class QueueModel : public QAbstractListModel
{
    Q_OBJECT
public:
    enum class QueueRoles : std::uint16_t {
        IdRole = Qt::UserRole + 1,
        TitleRole,
        ArtistRole,
        AlbumRole,
        DurationRole,
        UriRole,
        IsCurrentRole
    };
    Q_ENUM(QueueRoles)

    explicit QueueModel(QObject *parent = nullptr) : QAbstractListModel(parent) {}
    [[nodiscard]] auto rowCount(const QModelIndex &parent = QModelIndex()) const -> int override;
    [[nodiscard]] auto data(const QModelIndex &index, int role = Qt::DisplayRole) const -> QVariant override;
    [[nodiscard]] auto roleNames() const -> QHash<int, QByteArray> override;
    void setQueue(const QList<QueueItem> &queue);
    void setCurrentSongId(int id);
    QList<QueueItem> m_queue;
    int m_currentSongId = -1;
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
    Q_PROPERTY(QueueModel* queueModel READ queueModel CONSTANT)
    Q_PROPERTY(int currentAlbumIndex READ currentAlbumIndex NOTIFY currentAlbumIndexChanged)
    Q_PROPERTY(QString currentPath READ currentPath NOTIFY currentPathChanged)
    Q_PROPERTY(bool repeat READ repeat WRITE setRepeat NOTIFY repeatChanged)
    Q_PROPERTY(bool random READ random WRITE setRandom NOTIFY randomChanged)
    Q_PROPERTY(bool single READ single WRITE setSingle NOTIFY singleChanged)
    Q_PROPERTY(bool consume READ consume WRITE setConsume NOTIFY consumeChanged)
    Q_PROPERTY(int volume READ volume WRITE setVolume NOTIFY volumeChanged)
    Q_PROPERTY(QStringList playlists READ playlists NOTIFY playlistsChanged)
    Q_PROPERTY(SortMode sortMode READ sortMode WRITE setSortMode NOTIFY sortModeChanged)

public:
    explicit MpdClient(QObject *parent = nullptr);
    ~MpdClient() override;
    MpdClient(const MpdClient&) = delete;
    auto operator=(const MpdClient&) -> MpdClient& = delete;
    MpdClient(MpdClient&&) = delete;
    auto operator=(MpdClient&&) -> MpdClient& = delete;

    enum class SortMode : std::uint8_t {
        Artist,
        Album,
        ArtistYear
    };
    Q_ENUM(SortMode)

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
    [[nodiscard]] auto queueModel() const -> QueueModel*;
    [[nodiscard]] auto currentAlbumIndex() const -> int;
    [[nodiscard]] auto currentPath() const -> QString;
    [[nodiscard]] auto repeat() const -> bool;
    [[nodiscard]] auto random() const -> bool;
    [[nodiscard]] auto single() const -> bool;
    [[nodiscard]] auto consume() const -> bool;
    [[nodiscard]] auto volume() const -> int;
    [[nodiscard]] auto playlists() const -> QStringList;
    [[nodiscard]] auto sortMode() const -> SortMode;
    [[nodiscard]] auto uri() const -> QString;

    void setWindow(QQuickWindow *window);
    [[nodiscard]] auto window() const -> QQuickWindow* { return m_window; }

public Q_SLOTS:
    void setArtist(const QString &artist);
    void setTitle(const QString &title);
    void setAlbum(const QString &album);
    void setState(const QString &state);
    void setRepeat(bool on);
    void setRandom(bool on);
    void setSingle(bool on);
    void setConsume(bool on);
    void setVolume(int volume);
    void setSortMode(SortMode mode);
    void cleanup();

    // Playback controls
    void play();
    void pause();
    void togglePlayPause();
    void stop();
    void next();
    void previous();
    void seek(qint64 time);
    void seekTo(qint64 time);

    // Library
    void refreshLibrary(); // Existing
    void loadAlbumTracks(int index);
    Q_INVOKABLE void playTrack(const QString &uri);
    Q_INVOKABLE void playAlbum(const QString &artistName, const QString &albumName, const QString &mbid = QString()); // New slot
    Q_INVOKABLE void addAlbum(const QString &artistName, const QString &albumName, const QString &mbid = QString());
    Q_INVOKABLE void refreshQueue();
    Q_INVOKABLE void playQueueId(int id);
    Q_INVOKABLE void addTrack(const QString &uri);
    Q_INVOKABLE void addPath(const QString &path);
    Q_INVOKABLE void browsePath(const QString &path);
    Q_INVOKABLE void refreshPlaylists();
    Q_INVOKABLE void loadPlaylist(const QString &name);
    Q_INVOKABLE void savePlaylist(const QString &name);
    Q_INVOKABLE void removePlaylist(const QString &name);
    Q_INVOKABLE void clearQueue();
    Q_INVOKABLE void removeId(int id);

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
    void repeatChanged();
    void randomChanged();
    void singleChanged();
    void consumeChanged();
    void volumeChanged();
    void playlistsChanged();
    void sortModeChanged();

private Q_SLOTS:
    void updateStatus();
    void handleMpdEvent();

private:
    void fetchAlbumArt(const QString &album);
    void fetchCoverForModel(int index, const QString &albumName);
    struct FetchParams {
        QString artist;
        QString album;
        QString mbid;
        QString cachePath;
        bool isMainArt;
        int modelIndex;
    };
    void fetchAlbumArtFromAPIs(const FetchParams &params);
    auto getCachePath(const QString &artist, const QString &album, const QString &mbid = QString()) -> QString;
    void sortAlbums(QList<AlbumItem> &albums);
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
    auto getSongsForAlbum(const QString &artistName, const QString &albumName, const QString &mbid = QString()) -> QList<SortableSong>;
    struct mpd_connection *m_connection;
    QSocketNotifier *m_notifier;
    QNetworkAccessManager *m_networkManager;
    QTimer *m_timer;

    QString m_artist;
    QString m_title;
    QString m_album;
    QString m_state;
    QString m_albumArt;
    QString m_currentMbid;
    qint64 m_duration = 0;
    qint64 m_elapsed = 0;
    int m_currentSongId = -1;
    QString m_currentUri;
    int m_currentAlbumIndex = -1;
    bool m_isIdle = false;

    bool m_repeat = false;
    bool m_random = false;
    bool m_single = false;
    bool m_consume = false;
    int m_volume = 100;
    QStringList m_playlists;
    SortMode m_sortMode = SortMode::Artist;

    QQuickWindow *m_window = nullptr;

    AlbumModel *m_albumModel;
    TrackModel *m_trackModel;
    BrowserModel *m_browserModel;
    QueueModel *m_queueModel;
    QString m_currentPath;
};

#endif // QUESTER_H