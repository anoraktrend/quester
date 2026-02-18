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
#include <QSystemTrayIcon>
#include <QMenu>
#include <QAction>
#include <mpd/client.h>
#include <QElapsedTimer>
#include <QMutex>
#include <QFutureWatcher>
#include <QJSValue>
#include "statistics.h"
#include <QDataStream>

struct AlbumItem {
    QString artist; // Added artist for more accurate searches
    QString artistSortName; // Artist sort name for proper sorting (e.g., "Beatles, The" instead of "The Beatles")
    QString name;
    QString artUrl;
    QString mbid; // MusicBrainz ID for precise album identification
    QString uri;
    bool artLoading = false;
    int year = 0;
};

QDataStream &operator<<(QDataStream &out, const AlbumItem &item);
QDataStream &operator>>(QDataStream &in, AlbumItem &item);

struct TrackItem {
    QString title;
    QString duration;
    QString uri;
};

struct PlaylistItem {
    QString title;
    QString creator;
    QString identifier;
    QString date;
    int trackCount = 0;
};

struct PlaylistTrackItem {
    QString title;
    QString creator;
    QString album;
    QString duration;
    QString identifier; // JSPF identifier (URL)
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
    unsigned durationSecs = 0;
};

class AlbumModel : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)
    friend class MpdClient;
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
    
    // QML-accessible method to get album data as a variant map
    Q_INVOKABLE [[nodiscard]] QVariantMap get(int index) const;
    
    // Thread-safe access for internal use
    [[nodiscard]] auto albums() const -> QList<AlbumItem>;
    void setAlbumsInternal(const QList<AlbumItem> &albums);

Q_SIGNALS:
    void countChanged();

private:
    QList<AlbumItem> m_albums;
    mutable QMutex m_mutex;
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

class PlaylistModel : public QAbstractListModel
{
    Q_OBJECT
public:
    enum class PlaylistRoles : std::uint16_t {
        TitleRole = Qt::UserRole + 1,
        CreatorRole,
        IdentifierRole,
        DateRole,
        TrackCountRole
    };
    Q_ENUM(PlaylistRoles)

    explicit PlaylistModel(QObject *parent = nullptr) : QAbstractListModel(parent) {}
    [[nodiscard]] auto rowCount(const QModelIndex &parent = QModelIndex()) const -> int override;
    [[nodiscard]] auto data(const QModelIndex &index, int role = Qt::DisplayRole) const -> QVariant override;
    [[nodiscard]] auto roleNames() const -> QHash<int, QByteArray> override;
    void setPlaylists(const QList<PlaylistItem> &playlists);
    QList<PlaylistItem> m_playlists;
};

class PlaylistTrackModel : public QAbstractListModel
{
    Q_OBJECT
public:
    enum class PlaylistTrackRoles : std::uint16_t {
        TitleRole = Qt::UserRole + 1,
        CreatorRole,
        AlbumRole,
        DurationRole,
        IdentifierRole
    };
    Q_ENUM(PlaylistTrackRoles)

    explicit PlaylistTrackModel(QObject *parent = nullptr) : QAbstractListModel(parent) {}
    [[nodiscard]] auto rowCount(const QModelIndex &parent = QModelIndex()) const -> int override;
    [[nodiscard]] auto data(const QModelIndex &index, int role = Qt::DisplayRole) const -> QVariant override;
    [[nodiscard]] auto roleNames() const -> QHash<int, QByteArray> override;
    void setTracks(const QList<PlaylistTrackItem> &tracks);
    QList<PlaylistTrackItem> m_tracks;
};

struct SortableSong {
    QString title;
    QString duration;
    QString uri;
    int disc{};
    int track{};
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
    Q_PROPERTY(QVariantMap weeklyStats READ weeklyStats NOTIFY weeklyStatsChanged)
    Q_PROPERTY(QVariantMap monthlyStats READ monthlyStats NOTIFY monthlyStatsChanged)
    Q_PROPERTY(QVariantMap yearlyStats READ yearlyStats NOTIFY yearlyStatsChanged)
    Q_PROPERTY(QVariantMap allTimeStats READ allTimeStats NOTIFY allTimeStatsChanged)
    Q_PROPERTY(QString audioSource READ audioSource WRITE setAudioSource NOTIFY audioSourceChanged)
    Q_PROPERTY(StatisticsManager* statistics READ statistics CONSTANT)
    Q_PROPERTY(PlaylistModel* playlistModel READ playlistModel CONSTANT)
    Q_PROPERTY(PlaylistTrackModel* playlistTrackModel READ playlistTrackModel CONSTANT)

private Q_SLOTS:
    void updateStatus();
    void handleMpdEvent();
    void handleLibraryUpdate(const QList<AlbumItem> &albums);
    void handleAlbumTracksLoaded(const QList<TrackItem> &tracks);
    void playAlbumInternal(const QList<SortableSong> &songs);
    void addAlbumInternal(const QList<SortableSong> &songs);
    void handleQueueUpdate(const QList<QueueItem> &queue);
    void handleBrowseUpdate(const QList<BrowserItem> &items);

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
    QString getCachePath(const QString &artist, const QString &album, const QString &mbid = QString());
    void sortAlbums(QList<AlbumItem> &albums);
    QByteArray getMpdPicture(const QString &uri);
    void connectToMpd();
    void sendIdle();
    void leaveIdle();
    void saveLibraryToCache(const QList<AlbumItem> &albums);
    static QList<AlbumItem> loadLibraryFromCacheInternal();
    QList<SortableSong> getSongsForAlbum(struct mpd_connection *conn, const QString &artistName, const QString &albumName, const QString &mbid = QString());
    
    enum class SortMode : std::uint8_t {
        Artist,
        Album,
        ArtistYear
    };
    Q_ENUM(SortMode)

    [[nodiscard]] qint64 duration() const;
    [[nodiscard]] int currentAlbumIndex() const;
    [[nodiscard]] QString currentPath() const;
    [[nodiscard]] bool consume() const;
    [[nodiscard]] SortMode sortMode() const;
    [[nodiscard]] QString uri() const;
    [[nodiscard]] QVariantMap weeklyStats() const;
    [[nodiscard]] QVariantMap monthlyStats() const;
    [[nodiscard]] QVariantMap yearlyStats() const;
    [[nodiscard]] QVariantMap allTimeStats() const;
    [[nodiscard]] StatisticsManager* statistics() const { return m_stats; }
    [[nodiscard]] AlbumModel* albumModel() const;
    [[nodiscard]] TrackModel* trackModel() const;
    [[nodiscard]] BrowserModel* browserModel() const;
    [[nodiscard]] PlaylistModel* playlistModel() const;
    [[nodiscard]] PlaylistTrackModel* playlistTrackModel() const;

    // Artist image fetching
    QString getArtistImageCachePath(const QString &artistName);


    // ListenBrainz JSPF playlist methods
    Q_INVOKABLE void fetchJspfPlaylist(const QString &playlistIdentifier);
    Q_INVOKABLE void saveJspfPlaylistToCache(const QString &identifier);

    Q_PROPERTY(QString listenBrainzToken READ listenBrainzToken WRITE setListenBrainzToken NOTIFY listenBrainzTokenChanged)
    Q_PROPERTY(QString listenBrainzUsername READ listenBrainzUsername WRITE setListenBrainzUsername NOTIFY listenBrainzUsernameChanged)
    Q_PROPERTY(bool lastfmCredentialsValid READ lastfmCredentialsValid NOTIFY lastfmCredentialsValidChanged)
    [[nodiscard]] QString listenBrainzToken() const;
    [[nodiscard]] QString listenBrainzUsername() const;
    void setListenBrainzToken(const QString &token);
    void setListenBrainzUsername(const QString &username);
    [[nodiscard]] bool lastfmCredentialsValid() const;
    Q_INVOKABLE void setLastfmCredentials(const QString &apiKey, const QString &secret, const QString &sessionKey);
    Q_INVOKABLE void authenticateLastfm(const QString &username, const QString &password);

public:
    explicit MpdClient(QObject *parent = nullptr);
    ~MpdClient() override;

    MpdClient(const MpdClient&) = delete;
    MpdClient& operator=(const MpdClient&) = delete;
    MpdClient(MpdClient&&) = delete;
    MpdClient& operator=(MpdClient&&) = delete;

    void setWindow(QQuickWindow *window);
    [[nodiscard]] auto window() const -> QQuickWindow* { return m_window; }
    [[nodiscard]] QString artist() const;
    [[nodiscard]] QString title() const;
    [[nodiscard]] QString album() const;
    [[nodiscard]] QString albumArt() const;
    [[nodiscard]] auto random() const -> bool { return m_random; }
    [[nodiscard]] auto audioSource() const -> QString { return m_audioSource; }
    [[nodiscard]] auto playlists() const -> QStringList { return m_playlists; }
    [[nodiscard]] auto state() const -> QString { return m_state; }
    [[nodiscard]] auto single() const -> bool { return m_single; }
    [[nodiscard]] auto repeat() const -> bool { return m_repeat; }
    [[nodiscard]] auto elapsed() const -> qint64 { return m_elapsed; }
    [[nodiscard]] auto queueModel() const -> QueueModel* { return m_queueModel; }
    [[nodiscard]] auto volume() const -> int { return m_volume; }

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
    void setAudioSource(const QString &source);
    void cleanup();

    // Playback controls
    void play();
    void pause();
    void togglePlayPause();
    void stop();
    void next();
    void previous();
    void seek(double time);
    void seekTo(double time);

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
    Q_INVOKABLE void openFileLocation(const QString &path);
    [[nodiscard]] auto mpdMusicDirectory() -> QString;

    // Deduplicator - Find and remove duplicate tracks
    Q_INVOKABLE void findDuplicates();
    Q_INVOKABLE void deleteSelectedDuplicates(const QVariantList &uris);
    void refreshLibraryAfterDelete();

    // Application/Window controls
    Q_INVOKABLE void quitApplication();
    Q_INVOKABLE void toggleFullscreen();
    Q_INVOKABLE void toggleWindow();
    Q_INVOKABLE void loadMostPlayedPlaylist();

    // Artist image fetching
    Q_INVOKABLE void fetchArtistImage(const QString &artistName, QJSValue callback);

Q_SIGNALS:
    void artistChanged();
    void titleChanged();
    void albumChanged();
    void stateChanged();
    void albumArtChanged();
    void durationChanged();
    void elapsedChanged();
    void currentAlbumIndexChanged();
    void currentSongChanged();
    void currentPathChanged();
    void repeatChanged();
    void randomChanged();
    void singleChanged();
    void consumeChanged();
    void volumeChanged();
    void playlistsChanged();
    void sortModeChanged();
    void weeklyStatsChanged();
    void monthlyStatsChanged();
    void yearlyStatsChanged();
    void allTimeStatsChanged();
    void audioSourceChanged();
    void listenBrainzTokenChanged();
    void listenBrainzUsernameChanged();
    void lastfmCredentialsValidChanged();
    void playlistSaved(const QString &title, const QString &path);
    
    // Deduplicator signals
    void duplicatesFound(const QVariantList &duplicates);
    void duplicatesDeleted(int count);
    void libraryUpdated(const QList<AlbumItem> &albums);
    void albumTracksLoaded(const QList<TrackItem> &tracks);

private:
    struct mpd_connection *m_connection{nullptr};
    QSocketNotifier *m_notifier{nullptr};
    QNetworkAccessManager *m_networkManager;
    QTimer *m_timer;
    QFutureWatcher<QList<QueueItem>> m_queueWatcher;
    QFutureWatcher<QList<BrowserItem>> m_browseWatcher;
    bool m_isIdle = false;

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
    const int volMax = 100;
    bool m_repeat = false;
    bool m_random = false;
    bool m_single = false;
    bool m_consume = false;
    int m_volume = volMax;
    QStringList m_playlists;
    SortMode m_sortMode = SortMode::Artist;
    QString m_audioSource;

    QQuickWindow *m_window = nullptr;

    AlbumModel *m_albumModel;
    TrackModel *m_trackModel;
    BrowserModel *m_browserModel;
    QueueModel *m_queueModel;
    PlaylistModel *m_playlistModel;
    PlaylistTrackModel *m_playlistTrackModel;
    QString m_currentPath;
    QMap<QString, QVariantMap> m_cachedJspfPlaylists; // identifier -> full playlist data
    QString m_lbToken; // ListenBrainz token for API access

    // System tray
    QSystemTrayIcon *m_trayIcon = nullptr;
    QMenu *m_trayMenu = nullptr;
    QAction *m_showAction = nullptr;
    QAction *m_playPauseAction = nullptr;
    QAction *m_nextAction = nullptr;
    QAction *m_prevAction = nullptr;
    QAction *m_quitAction = nullptr;

    // Statistics
    StatisticsManager *m_stats;
    QElapsedTimer m_playTimer;
    qint64 m_currentSongPlayTime = 0;
    QString m_lastArtist;
    QString m_lastTitle;
    QString m_lastAlbum;

public:
    void setupSystemTray();
    void updateTrayIcon();
    void updateTrayTooltip();
};

#endif // QUESTER_H