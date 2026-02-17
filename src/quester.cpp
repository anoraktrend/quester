#include "quester.h"
#include <mpd/connection.h>
#include <mpd/pair.h>
#include <mpd/recv.h>
#include <mpd/directory.h>
#include <mpd/entity.h>
#include <mpd/player.h>
#include <mpd/playlist.h>
#include <mpd/queue.h>
#include <mpd/response.h>
#include <mpd/search.h>
#include <mpd/song.h>
#include <mpd/status.h>
#include <mpd/tag.h>
#include <QByteArray>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDebug>
#include <QCollator>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>
#include <QSettings>
#include <QStandardPaths>
#include <QUrlQuery>
#include <algorithm>
#include <QPainter>
#include <QImage>
#include <QDesktopServices>
#include <QRegularExpression>
#include <QtConcurrent/QtConcurrent>
#include <QThreadPool>

constexpr int TIMER_INTERVAL = 100;
constexpr int MPD_PORT = 6600;
constexpr int MPD_TIMEOUT_MS = 30000;
constexpr int SECONDS_PER_MINUTE = 60;
constexpr int DECIMAL_BASE = 10;
constexpr int INITIAL_COVER_FETCH_LIMIT = 20; // Limit initial cover fetches for faster startup

const int PLAY_LOG_THRESHOLD_MS = 5000;
const int MS_PER_SECOND = 1000;
const int MS_PER_MINUTE = 60000;
const int DEFAULT_LIMIT = 10;
const QString FILE_SCHEME = "file://";

// --- AlbumModel Implementation ---
auto AlbumModel::rowCount(const QModelIndex &parent) const -> int
{
    if (parent.isValid())
        return 0;
    return static_cast<int>(m_albums.count());
}

auto AlbumModel::data(const QModelIndex &index, int role) const -> QVariant
{
    if (!index.isValid() || index.row() >= m_albums.count())
        return {};
    const AlbumItem &item = m_albums[index.row()];
    if (role == static_cast<int>(AlbumRoles::NameRole))
        return item.name;
    if (role == static_cast<int>(AlbumRoles::ArtRole))
        return item.artUrl;
    if (role == static_cast<int>(AlbumRoles::ArtistRole))
        return item.artist;
    if (role == static_cast<int>(AlbumRoles::YearRole))
        return item.year;
    if (role == static_cast<int>(AlbumRoles::MbidRole))
        return item.mbid;
    return {};
}

auto AlbumModel::roleNames() const -> QHash<int, QByteArray>
{
    QHash<int, QByteArray> roles;
    roles[static_cast<int>(AlbumRoles::NameRole)] = "name";
    roles[static_cast<int>(AlbumRoles::ArtRole)] = "art";
    roles[static_cast<int>(AlbumRoles::ArtistRole)] = "artist";
    roles[static_cast<int>(AlbumRoles::YearRole)] = "year";
    roles[static_cast<int>(AlbumRoles::MbidRole)] = "mbid";
    return roles;
}

void AlbumModel::setAlbums(const QList<AlbumItem> &albums)
{
    beginResetModel();
    m_albums = albums;
    endResetModel();
}

void AlbumModel::updateArt(int index, const QString &url)
{
    QMutexLocker locker(&m_mutex);
    if (index < 0 || index >= m_albums.count())
        return;
    m_albums[index].artUrl = url;
    m_albums[index].artLoading = false;
    locker.unlock();
    emit dataChanged(this->index(index), this->index(index), {static_cast<int>(AlbumRoles::ArtRole)});
}

auto AlbumModel::albums() const -> QList<AlbumItem>
{
    QMutexLocker locker(&m_mutex);
    return m_albums;
}

void AlbumModel::setAlbumsInternal(const QList<AlbumItem> &albums)
{
    QMutexLocker locker(&m_mutex);
    m_albums = albums;
}

// --- TrackModel Implementation ---
auto TrackModel::rowCount(const QModelIndex &parent) const -> int
{
    if (parent.isValid())
        return 0;
    return static_cast<int>(m_tracks.count());
}

auto TrackModel::data(const QModelIndex &index, int role) const -> QVariant
{
    if (!index.isValid() || index.row() >= m_tracks.count())
        return {};
    const TrackItem &item = m_tracks[index.row()];
    if (role == static_cast<int>(TrackRoles::TitleRole))
        return item.title;
    if (role == static_cast<int>(TrackRoles::DurationRole))
        return item.duration;
    if (role == static_cast<int>(TrackRoles::UriRole))
        return item.uri;
    return {};
}

auto TrackModel::roleNames() const -> QHash<int, QByteArray>
{
    QHash<int, QByteArray> roles;
    roles[static_cast<int>(TrackRoles::TitleRole)] = "title";
    roles[static_cast<int>(TrackRoles::DurationRole)] = "duration";
    roles[static_cast<int>(TrackRoles::UriRole)] = "uri";
    return roles;
}

void TrackModel::setTracks(const QList<TrackItem> &tracks)
{
    beginResetModel();
    m_tracks = tracks;
    endResetModel();
}

// --- BrowserModel Implementation ---
auto BrowserModel::rowCount(const QModelIndex &parent) const -> int
{
    if (parent.isValid())
        return 0;
    return static_cast<int>(m_items.count());
}

auto BrowserModel::data(const QModelIndex &index, int role) const -> QVariant
{
    if (!index.isValid() || index.row() >= m_items.count())
        return {};
    const BrowserItem &item = m_items[index.row()];
    if (role == static_cast<int>(BrowserRoles::NameRole))
        return item.name;
    if (role == static_cast<int>(BrowserRoles::PathRole))
        return item.path;
    if (role == static_cast<int>(BrowserRoles::IsDirRole))
        return item.isDir;
    return {};
}

auto BrowserModel::roleNames() const -> QHash<int, QByteArray>
{
    QHash<int, QByteArray> roles;
    roles[static_cast<int>(BrowserRoles::NameRole)] = "name";
    roles[static_cast<int>(BrowserRoles::PathRole)] = "path";
    roles[static_cast<int>(BrowserRoles::IsDirRole)] = "isDir";
    return roles;
}

void BrowserModel::setItems(const QList<BrowserItem> &items)
{
    beginResetModel();
    m_items = items;
    endResetModel();
}

// --- QueueModel Implementation ---
auto QueueModel::rowCount(const QModelIndex &parent) const -> int
{
    if (parent.isValid())
        return 0;
    return static_cast<int>(m_queue.count());
}

auto QueueModel::data(const QModelIndex &index, int role) const -> QVariant
{
    if (!index.isValid() || index.row() >= m_queue.count())
        return {};
    const QueueItem &item = m_queue[index.row()];
    if (role == static_cast<int>(QueueRoles::IdRole))
        return item.id;
    if (role == static_cast<int>(QueueRoles::TitleRole))
        return item.title;
    if (role == static_cast<int>(QueueRoles::ArtistRole))
        return item.artist;
    if (role == static_cast<int>(QueueRoles::AlbumRole))
        return item.album;
    if (role == static_cast<int>(QueueRoles::DurationRole))
        return item.duration;
    if (role == static_cast<int>(QueueRoles::UriRole))
        return item.uri;
    if (role == static_cast<int>(QueueRoles::IsCurrentRole))
        return item.id == m_currentSongId;
    return {};
}

auto QueueModel::roleNames() const -> QHash<int, QByteArray>
{
    QHash<int, QByteArray> roles;
    roles[static_cast<int>(QueueRoles::IdRole)] = "id";
    roles[static_cast<int>(QueueRoles::TitleRole)] = "title";
    roles[static_cast<int>(QueueRoles::ArtistRole)] = "artist";
    roles[static_cast<int>(QueueRoles::AlbumRole)] = "album";
    roles[static_cast<int>(QueueRoles::DurationRole)] = "duration";
    roles[static_cast<int>(QueueRoles::UriRole)] = "uri";
    roles[static_cast<int>(QueueRoles::IsCurrentRole)] = "isCurrent";
    return roles;
}

void QueueModel::setQueue(const QList<QueueItem> &queue)
{
    beginResetModel();
    m_queue = queue;
    endResetModel();
}

void QueueModel::setCurrentSongId(int id)
{
    if (m_currentSongId == id)
        return;
    m_currentSongId = id;
    emit dataChanged(index(0), index(static_cast<int>(m_queue.count()) - 1), {static_cast<int>(QueueRoles::IsCurrentRole)});
}

// --- PlaylistModel Implementation ---
auto PlaylistModel::rowCount(const QModelIndex &parent) const -> int
{
    if (parent.isValid())
        return 0;
    return static_cast<int>(m_playlists.count());
}

auto PlaylistModel::data(const QModelIndex &index, int role) const -> QVariant
{
    if (!index.isValid() || index.row() >= m_playlists.count())
        return {};
    const PlaylistItem &item = m_playlists[index.row()];
    if (role == static_cast<int>(PlaylistRoles::TitleRole))
        return item.title;
    if (role == static_cast<int>(PlaylistRoles::CreatorRole))
        return item.creator;
    if (role == static_cast<int>(PlaylistRoles::IdentifierRole))
        return item.identifier;
    if (role == static_cast<int>(PlaylistRoles::DateRole))
        return item.date;
    if (role == static_cast<int>(PlaylistRoles::TrackCountRole))
        return item.trackCount;
    return {};
}

auto PlaylistModel::roleNames() const -> QHash<int, QByteArray>
{
    QHash<int, QByteArray> roles;
    roles[static_cast<int>(PlaylistRoles::TitleRole)] = "title";
    roles[static_cast<int>(PlaylistRoles::CreatorRole)] = "creator";
    roles[static_cast<int>(PlaylistRoles::IdentifierRole)] = "identifier";
    roles[static_cast<int>(PlaylistRoles::DateRole)] = "date";
    roles[static_cast<int>(PlaylistRoles::TrackCountRole)] = "trackCount";
    return roles;
}

void PlaylistModel::setPlaylists(const QList<PlaylistItem> &playlists)
{
    beginResetModel();
    m_playlists = playlists;
    endResetModel();
}

// --- PlaylistTrackModel Implementation ---
auto PlaylistTrackModel::rowCount(const QModelIndex &parent) const -> int
{
    if (parent.isValid())
        return 0;
    return static_cast<int>(m_tracks.count());
}

auto PlaylistTrackModel::data(const QModelIndex &index, int role) const -> QVariant
{
    if (!index.isValid() || index.row() >= m_tracks.count())
        return {};
    const PlaylistTrackItem &item = m_tracks[index.row()];
    if (role == static_cast<int>(PlaylistTrackRoles::TitleRole))
        return item.title;
    if (role == static_cast<int>(PlaylistTrackRoles::CreatorRole))
        return item.creator;
    if (role == static_cast<int>(PlaylistTrackRoles::AlbumRole))
        return item.album;
    if (role == static_cast<int>(PlaylistTrackRoles::DurationRole))
        return item.duration;
    if (role == static_cast<int>(PlaylistTrackRoles::IdentifierRole))
        return item.identifier;
    return {};
}

auto PlaylistTrackModel::roleNames() const -> QHash<int, QByteArray>
{
    QHash<int, QByteArray> roles;
    roles[static_cast<int>(PlaylistTrackRoles::TitleRole)] = "title";
    roles[static_cast<int>(PlaylistTrackRoles::CreatorRole)] = "creator";
    roles[static_cast<int>(PlaylistTrackRoles::AlbumRole)] = "album";
    roles[static_cast<int>(PlaylistTrackRoles::DurationRole)] = "duration";
    roles[static_cast<int>(PlaylistTrackRoles::IdentifierRole)] = "identifier";
    return roles;
}

void PlaylistTrackModel::setTracks(const QList<PlaylistTrackItem> &tracks)
{
    beginResetModel();
    m_tracks = tracks;
    endResetModel();
}

MpdClient::MpdClient(QObject *parent)
    : QObject(parent)
    , 
     m_networkManager(new QNetworkAccessManager(this))
    , m_albumModel(new AlbumModel(this))
    , m_trackModel(new TrackModel(this))
    , m_browserModel(new BrowserModel(this))
    , m_queueModel(new QueueModel(this))
    , m_playlistModel(new PlaylistModel(this))
    , m_playlistTrackModel(new PlaylistTrackModel(this))
    , m_timer(new QTimer(this))
    , 
     m_stats(new StatisticsManager(this))
{
    qRegisterMetaType<QList<AlbumItem>>("QList<AlbumItem>");
    qRegisterMetaType<QList<TrackItem>>("QList<TrackItem>");
    qRegisterMetaType<QList<SortableSong>>("QList<SortableSong>");
    connect(this, &MpdClient::libraryUpdated, this, &MpdClient::handleLibraryUpdate);
    connect(this, &MpdClient::albumTracksLoaded, this, &MpdClient::handleAlbumTracksLoaded);

    QSettings settings("Quester", "Quester");
    m_sortMode = static_cast<SortMode>(settings.value("sortMode", static_cast<int>(SortMode::Artist)).toInt());
    m_audioSource = settings.value("audioSource", "pipewire").toString();

    m_stats->setListenBrainzCredentials(settings.value("listenBrainzToken").toString(),
                                        settings.value("listenBrainzUsername").toString());

    // Load Last.fm credentials (from settings or use defaults)
    QString lastfmApiKey = settings.value("lastfmApiKey").toString();
    QString lastfmSecret = settings.value("lastfmSecret").toString();
    QString lastfmSessionKey = settings.value("lastfmSessionKey").toString();
    
    // Save default credentials if not already set
    if (lastfmApiKey.isEmpty()) {
        lastfmApiKey = "5b184bbfb5f3d1ac3a4955a6676d7dc3";
        settings.setValue("lastfmApiKey", lastfmApiKey);
    }
    if (lastfmSecret.isEmpty()) {
        lastfmSecret = "cc7e582cd3e1f5e79c0e9098fbc019ff";
        settings.setValue("lastfmSecret", lastfmSecret);
    }
    
    m_stats->setLastfmCredentials(lastfmApiKey, lastfmSecret, lastfmSessionKey);

    connect(m_stats, &StatisticsManager::playLogged, this, [this]() -> void {
        emit weeklyStatsChanged();
        emit monthlyStatsChanged();
        emit yearlyStatsChanged();
        emit allTimeStatsChanged();
    });

    QObject::connect(m_timer, &QTimer::timeout, this, &MpdClient::updateStatus);

    // KISS: Defer MPD connection and library refresh to allow UI to load first.
    // This significantly improves startup time by not blocking on MPD connection.
    QTimer::singleShot(100, this, [this]() -> void {
        // Load library from cache first for instant display
        QList<AlbumItem> cachedAlbums = loadLibraryFromCacheInternal();
        if (!cachedAlbums.isEmpty()) {
            handleLibraryUpdate(cachedAlbums);
        }
        
        // Then connect to MPD and refresh in background
        connectToMpd();
        m_timer->start(TIMER_INTERVAL);
    });
}

MpdClient::~MpdClient()
{
    if (m_notifier)
        delete m_notifier;
    if (m_connection) {
        mpd_connection_free(m_connection);
    }
}

void MpdClient::connectToMpd()
{
    m_connection = mpd_connection_new("localhost", MPD_PORT, MPD_TIMEOUT_MS);
    if (mpd_connection_get_error(m_connection) != MPD_ERROR_SUCCESS) {
        qWarning() << "Failed to connect to MPD:" << mpd_connection_get_error_message(m_connection);
        mpd_connection_free(m_connection);
        m_connection = nullptr;
    } else {
        qInfo() << "Successfully connected to MPD.";

        int fd = mpd_connection_get_fd(m_connection);
        if (m_notifier)
            delete m_notifier;
        m_notifier = new QSocketNotifier(fd, QSocketNotifier::Read, this); // NOLINT(cppcoreguidelines-owning-memory)
        QObject::connect(m_notifier, &QSocketNotifier::activated, this, &MpdClient::handleMpdEvent);

        updateStatus();
        refreshQueue();
        sendIdle();
    }
}

void MpdClient::sendIdle()
{
    if (m_connection && !m_isIdle) {
        if (mpd_send_idle(m_connection)) {
            m_isIdle = true;
        }
    }
}

void MpdClient::leaveIdle()
{
    if (m_connection && m_isIdle) {
        mpd_run_noidle(m_connection);
        m_isIdle = false;
    }
}

void MpdClient::handleMpdEvent()
{
    if (!m_connection)
        return;

    if (!m_isIdle)
        return;
    m_isIdle = false;

    enum mpd_idle events = mpd_recv_idle(m_connection, true);

    if (events & (MPD_IDLE_PLAYER | MPD_IDLE_MIXER | MPD_IDLE_OPTIONS)) {
        updateStatus();
    }
    if (events & MPD_IDLE_STORED_PLAYLIST) {
        refreshPlaylists();
    }
    if (events & MPD_IDLE_QUEUE) {
        refreshQueue();
    }

    sendIdle();
}

void MpdClient::updateStatus()
{
    // KISS: We use a simple timer to poll for status updates (like elapsed time)
    // rather than implementing complex local clock synchronization and drift correction.
    // The overhead of querying MPD status every 100ms is negligible for a local/LAN connection.
    if (!m_connection) {
        return;
    }

    bool wasIdle = m_isIdle;
    if (wasIdle) {
        leaveIdle();
    }

    struct mpd_status *status = mpd_run_status(m_connection);
    if (status) {
        mpd_state player_state = mpd_status_get_state(status);
        switch (player_state) {
        case MPD_STATE_PLAY:
            setState("play");
            break;
        case MPD_STATE_PAUSE:
            setState("pause");
            break;
        case MPD_STATE_STOP:
            setState("stop");
            break;
        default:
            setState("unknown");
            break;
        }

        // Playback tracking logic
        if (m_state == "play") {
            if (m_playTimer.isValid()) {
                m_currentSongPlayTime += m_playTimer.restart();
            } else {
                m_playTimer.start();
            }
        } else {
            m_playTimer.invalidate();
        }

        bool repeat = mpd_status_get_repeat(status);
        bool random = mpd_status_get_random(status);
        bool single = mpd_status_get_single(status);
        bool consume = mpd_status_get_consume(status);

        if (m_repeat != repeat) {
            m_repeat = repeat;
            emit repeatChanged();
        }
        if (m_random != random) {
            m_random = random;
            emit randomChanged();
        }
        if (m_single != single) {
            m_single = single;
            emit singleChanged();
        }
        if (m_consume != consume) {
            m_consume = consume;
            emit consumeChanged();
        }

        int volume = mpd_status_get_volume(status);
        if (m_volume != volume) {
            m_volume = volume;
            emit volumeChanged();
        }

        qint64 elapsed = mpd_status_get_elapsed_time(status);
        qint64 total = mpd_status_get_total_time(status);

        if (m_elapsed != elapsed) {
            m_elapsed = elapsed;
            emit elapsedChanged();
        }
        if (m_duration != total) {
            m_duration = total;
            emit durationChanged();
        }

        int song_id = mpd_status_get_song_id(status);
        mpd_status_free(status);

        struct mpd_song *song = mpd_run_current_song(m_connection);
        if (song) {
            const char *artist_tag = mpd_song_get_tag(song, MPD_TAG_ARTIST, 0);
            const char *title_tag = mpd_song_get_tag(song, MPD_TAG_TITLE, 0);
            const char *album_tag = mpd_song_get_tag(song, MPD_TAG_ALBUM, 0);
            const char *uri_tag = mpd_song_get_uri(song);
            const char *mbid_tag = mpd_song_get_tag(song, MPD_TAG_MUSICBRAINZ_ALBUMID, 0);

            setArtist(artist_tag ? QString::fromUtf8(artist_tag) : tr("Unknown Artist"));
            setTitle(title_tag ? QString::fromUtf8(title_tag) : tr("Unknown Title"));
            setAlbum(album_tag ? QString::fromUtf8(album_tag) : tr("Unknown Album"));
            if (uri_tag)
                m_currentUri = QString::fromUtf8(uri_tag);
            m_currentMbid = mbid_tag ? QString::fromUtf8(mbid_tag) : "";

            if (m_albumModel) {
                const auto albums = m_albumModel->albums();
                int newIndex = -1;
                for (int i = 0; i < albums.count(); ++i) {
                    if (albums[i].name == m_album) {
                        newIndex = i;
                        break;
                    }
                }
                if (newIndex != -1 && m_currentAlbumIndex != newIndex) {
                    m_currentAlbumIndex = newIndex;
                    emit currentAlbumIndexChanged();
                }
            }

            if (song_id != m_currentSongId) {
                // Log previous song if played for more than 5 seconds
                if (m_currentSongId != -1 && m_currentSongPlayTime > PLAY_LOG_THRESHOLD_MS) {
                    QString lastUri = property("lastUri").toString();
                    m_stats->logPlay(m_lastArtist, m_lastTitle, m_lastAlbum, lastUri, m_currentSongPlayTime);
                }
                m_currentSongPlayTime = 0;
                m_lastArtist = m_artist;
                m_lastTitle = m_title;
                m_lastAlbum = m_album;
                setProperty("lastUri", m_currentUri);
                m_stats->submitPlayingNow(m_artist, m_title, m_album, m_duration * MS_PER_SECOND);

                m_currentSongId = song_id;
                if (m_queueModel) {
                    m_queueModel->setCurrentSongId(m_currentSongId);
                }
                fetchAlbumArt(m_album);
                emit currentSongChanged();
            }

            mpd_song_free(song);
        }
    } else if (mpd_connection_get_error(m_connection) != MPD_ERROR_SUCCESS) {
        qWarning() << "MPD status error:" << mpd_connection_get_error_message(m_connection);
        if (m_notifier) {
            delete m_notifier;
            m_notifier = nullptr;
        }
        mpd_connection_free(m_connection);
        m_connection = nullptr;
        m_isIdle = false;
    }

    if (wasIdle) {
        sendIdle();
    }
}

void MpdClient::fetchAlbumArt(const QString &album)
{
    if (album.isEmpty() || album == "Unknown Album")
        return;

    // Reset album art to empty while fetching from APIs/Cache
    m_albumArt.clear();
    emit albumArtChanged();

    QString cachePath = getCachePath(m_artist, album, m_currentMbid);

    if (QFile::exists(cachePath)) {
        m_albumArt = "file://" + cachePath;
        emit albumArtChanged();
        return;
    }

    QByteArray mpdData = getMpdPicture(m_currentUri);
    if (!mpdData.isEmpty()) {
        QFile file(cachePath);
        if (file.open(QIODevice::WriteOnly)) {
            file.write(mpdData);
            file.close();
            m_albumArt = "file://" + cachePath;
            emit albumArtChanged();
            return;
        }
    }

    fetchAlbumArtFromAPIs({.artist=m_artist, .album=album, .mbid=m_currentMbid, .cachePath=cachePath, .isMainArt=true, .modelIndex=-1});
}

auto MpdClient::artist() const -> QString { return m_artist; }
auto MpdClient::title() const -> QString { return m_title; }
auto MpdClient::album() const -> QString { return m_album; }
auto MpdClient::albumArt() const -> QString { return m_albumArt; }
auto MpdClient::duration() const -> qint64 { return m_duration; }
auto MpdClient::currentAlbumIndex() const -> int { return m_currentAlbumIndex; }
auto MpdClient::browserModel() const -> BrowserModel * { return m_browserModel; }
auto MpdClient::currentPath() const -> QString { return m_currentPath; }
auto MpdClient::consume() const -> bool { return m_consume; }
auto MpdClient::sortMode() const -> SortMode { return m_sortMode; }
auto MpdClient::uri() const -> QString { return m_currentUri; }
auto MpdClient::weeklyStats() const -> QVariantMap { return m_stats->getWeeklyStats(); }
auto MpdClient::monthlyStats() const -> QVariantMap { return m_stats->getMonthlyStats(); }
auto MpdClient::yearlyStats() const -> QVariantMap { return m_stats->getYearlyStats(); }
auto MpdClient::allTimeStats() const -> QVariantMap { return m_stats->getAllTimeStats(); }
auto MpdClient::listenBrainzToken() const -> QString {
    QSettings settings("Quester", "Quester");
    return settings.value("listenBrainzToken").toString();
}
auto MpdClient::listenBrainzUsername() const -> QString {
    QSettings settings("Quester", "Quester");
    return settings.value("listenBrainzUsername").toString();
}

void MpdClient::setListenBrainzToken(const QString &token) {
    QSettings settings("Quester", "Quester");
    if (settings.value("listenBrainzToken").toString() != token) {
        settings.setValue("listenBrainzToken", token);
        m_stats->setListenBrainzCredentials(token, listenBrainzUsername());
        emit listenBrainzTokenChanged();
    }
}

void MpdClient::setListenBrainzUsername(const QString &username) {
    QSettings settings("Quester", "Quester");
    if (settings.value("listenBrainzUsername").toString() != username) {
        settings.setValue("listenBrainzUsername", username);
        m_stats->setListenBrainzCredentials(listenBrainzToken(), username);
        emit listenBrainzUsernameChanged();
    }
}

auto MpdClient::lastfmCredentialsValid() const -> bool {
    return m_stats->lastfmCredentialsValid();
}

void MpdClient::setLastfmCredentials(const QString &apiKey, const QString &secret, const QString &sessionKey) {
    m_stats->setLastfmCredentials(apiKey, secret, sessionKey);
    emit lastfmCredentialsValidChanged();
}

void MpdClient::authenticateLastfm(const QString &lastfmUser, const QString &lastfmPassword) {
    // Last.fm authentication requires getting a session key via their auth web service
    // For simplicity, users should get an API key and session key from last.fm/api
    // This method can be extended for full auth flow if needed
    qWarning() << "[Last.fm] Manual authentication required. Please get API credentials from last.fm/api";
}

void MpdClient::setArtist(const QString &artist) {
    if (m_artist != artist) { m_artist = artist; emit artistChanged(); }
}
void MpdClient::setTitle(const QString &title) {
    if (m_title != title) { m_title = title; emit titleChanged(); }
}
void MpdClient::setAlbum(const QString &album) {
    if (m_album != album) { m_album = album; emit albumChanged(); }
}
void MpdClient::setState(const QString &state) {
    if (m_state != state) { m_state = state; emit stateChanged(); }
}
void MpdClient::setRepeat(bool on) {
    if (m_connection) { leaveIdle(); mpd_run_repeat(m_connection, on); sendIdle(); }
}
void MpdClient::setRandom(bool on) {
    if (m_connection) { leaveIdle(); mpd_run_random(m_connection, on); sendIdle(); }
}
void MpdClient::setSingle(bool on) {
    if (m_connection) { leaveIdle(); mpd_run_single(m_connection, on); sendIdle(); }
}
void MpdClient::setConsume(bool on) {
    if (m_connection) { leaveIdle(); mpd_run_consume(m_connection, on); sendIdle(); }
}
void MpdClient::setVolume(int volume) {
    if (m_connection) { leaveIdle(); mpd_run_set_volume(m_connection, volume); sendIdle(); }
}
void MpdClient::setSortMode(SortMode mode) {
    if (m_sortMode == mode) return;
    m_sortMode = mode;
    QSettings settings("Quester", "Quester");
    settings.setValue("sortMode", static_cast<int>(m_sortMode));
    emit sortModeChanged();
    if (m_albumModel) {
        QList<AlbumItem> albums = m_albumModel->m_albums;
        sortAlbums(albums);
        m_albumModel->setAlbums(albums);
    }
}
void MpdClient::setAudioSource(const QString &source) {
    if (m_audioSource == source) return;
    m_audioSource = source;
    QSettings settings("Quester", "Quester");
    settings.setValue("audioSource", m_audioSource);
    emit audioSourceChanged();
}

// KISS: The MpdClient acts as the main controller. While separating window management
// might be architecturally "purer", keeping it here simplifies the QML interface
// and reduces the number of context properties / singletons required.
void MpdClient::setWindow(QQuickWindow *window) { m_window = window; }

void MpdClient::play() {
    if (m_connection) { leaveIdle(); mpd_run_play(m_connection); sendIdle(); }
}

void MpdClient::seek(double time)
{
    if (m_connection) {
        leaveIdle();
        mpd_run_seek_current(m_connection, static_cast<float>(time), false);
        sendIdle();
        updateStatus();
    }
}

void MpdClient::seekTo(double time)
{
    if (m_connection) {
        leaveIdle();
        mpd_run_seek_current(m_connection, static_cast<float>(time), true);
        sendIdle();
        updateStatus();
    }
}

void MpdClient::refreshLibrary()
{
    QThreadPool::globalInstance()->start([this]() -> void {
        // First, load from cache and update the UI
        QList<AlbumItem> cachedAlbums = loadLibraryFromCacheInternal();
        if (!cachedAlbums.isEmpty()) {
            QMetaObject::invokeMethod(this, [this, cachedAlbums]() -> void {
                handleLibraryUpdate(cachedAlbums);
            }, Qt::QueuedConnection);
        }

        // Now, refresh from MPD in the background
        struct mpd_connection *conn = mpd_connection_new("localhost", MPD_PORT, MPD_TIMEOUT_MS);
        if (mpd_connection_get_error(conn) != MPD_ERROR_SUCCESS) {
            qWarning() << "Failed to connect to MPD in background thread:" << mpd_connection_get_error_message(conn);
            mpd_connection_free(conn);
            return;
        }

        QList<AlbumItem> albums;
        QSet<QString> addedMbids;
        QSet<QString> addedArtistAlbums;

        if (mpd_send_command(conn, "list", "album", "group", "artist", "group", "artist_sort_name", "group", "date", "group", "musicbrainz_albumid", nullptr)) { // NOLINT(cppcoreguidelines-pro-type-vararg)
            struct mpd_pair *pair = nullptr;
            QString currentArtist = tr("Unknown Artist");
            QString currentArtistSortName;
            QString currentMbid;
            int currentYear = 0;

            while ((pair = mpd_recv_pair(conn)) != nullptr) {
                QString tagName = QString::fromUtf8(pair->name);
                QString tagValue = QString::fromUtf8(pair->value);

                if (tagName == "Artist") {
                    currentArtist = tagValue;
                } else if (tagName == "ArtistSortName" || tagName == "ARTIST_SORT_NAME") {
                    currentArtistSortName = tagValue;
                } else if (tagName == "Date") {
                    currentYear = tagValue.left(4).toInt();
                } else if (tagName == "MusicBrainzAlbumId" || tagName == "MUSICBRAINZ_ALBUMID") {
                    currentMbid = tagValue;
                } else if (tagName == "Album") {
                    QString albumName = tagValue;
                    if (!albumName.isEmpty()) {
                        bool shouldAdd = false;
                        QString groupKey;

                        if (!currentMbid.isEmpty()) {
                            groupKey = currentMbid;
                            if (!addedMbids.contains(groupKey)) {
                                shouldAdd = true;
                                addedMbids.insert(groupKey);
                            }
                        } else {
                            groupKey = currentArtist + "|" + albumName;
                            if (!addedArtistAlbums.contains(groupKey)) {
                                shouldAdd = true;
                                addedArtistAlbums.insert(groupKey);
                            }
                        }

                        if (shouldAdd) {
                            QString cachePath = getCachePath(currentArtist, albumName, currentMbid);
                            QString art = QFile::exists(cachePath) ? "file://" + cachePath : "";
                            // Use artist sort name if available, otherwise fall back to artist
                            QString sortName = currentArtistSortName.isEmpty() ? currentArtist : currentArtistSortName;
                            albums.append(AlbumItem{.artist=currentArtist, .artistSortName=sortName, .name=albumName, .artUrl=art, .mbid=currentMbid, .uri="", .artLoading=false, .year=currentYear});
                        }
                    }
                }
                mpd_return_pair(conn, pair);
            }

            if (mpd_connection_get_error(conn) != MPD_ERROR_SUCCESS) {
                qWarning() << "Error reading library response:" << mpd_connection_get_error_message(conn);
                mpd_connection_clear_error(conn);
            }

            mpd_response_finish(conn);
        } else {
            qWarning() << "Failed to send list command:" << mpd_connection_get_error_message(conn);
            mpd_connection_clear_error(conn);
        }

        mpd_connection_free(conn);

        if (!albums.isEmpty()) {
            saveLibraryToCache(albums);
            QMetaObject::invokeMethod(this, [this, albums]() -> void {
                handleLibraryUpdate(albums);
            }, Qt::QueuedConnection);
        }
    });
}

void MpdClient::handleAlbumTracksLoaded(const QList<TrackItem> &tracks)
{
    m_trackModel->setTracks(tracks);
}

void MpdClient::loadAlbumTracks(int index)
{
    if (index < 0 || index >= m_albumModel->albums().count())
        return;

    const AlbumItem album = m_albumModel->albums().at(index);
    QString albumName = album.name;
    QString artistName = album.artist;
    QString mbid = album.mbid;

    QThreadPool::globalInstance()->start([this, artistName, albumName, mbid]() -> void {
        struct mpd_connection *conn = mpd_connection_new("localhost", MPD_PORT, MPD_TIMEOUT_MS);
        if (mpd_connection_get_error(conn) != MPD_ERROR_SUCCESS) {
            qWarning() << "Failed to connect to MPD in background thread:" << mpd_connection_get_error_message(conn);
            mpd_connection_free(conn);
            return;
        }

        QList<SortableSong> sortedTracks = getSongsForAlbum(conn, artistName, albumName, mbid);
        QList<TrackItem> tracks;

        for (const auto &t : sortedTracks) {
            tracks.append({.title=t.title, .duration=t.duration, .uri=t.uri});
        }
        
        mpd_connection_free(conn);

        emit albumTracksLoaded(tracks);
    });
}

void MpdClient::refreshQueue()
{
    if (!m_connection)
        return;

    QThreadPool::globalInstance()->start([this]() -> void {
        struct mpd_connection *conn = mpd_connection_new("localhost", MPD_PORT, MPD_TIMEOUT_MS);
        if (mpd_connection_get_error(conn) != MPD_ERROR_SUCCESS) {
            qWarning() << "Failed to connect to MPD in background thread:" << mpd_connection_get_error_message(conn);
            mpd_connection_free(conn);
            return;
        }

        QList<QueueItem> queue;
        if (mpd_send_list_queue_meta(conn)) {
            struct mpd_entity *entity = nullptr;
            while ((entity = mpd_recv_entity(conn)) != nullptr) {
                if (mpd_entity_get_type(entity) == MPD_ENTITY_TYPE_SONG) {
                    const struct mpd_song *song = mpd_entity_get_song(entity);
                    int id = static_cast<int>(mpd_song_get_id(song));
                    const char *uri = mpd_song_get_uri(song);
                    const char *title = mpd_song_get_tag(song, MPD_TAG_TITLE, 0);
                    const char *artist = mpd_song_get_tag(song, MPD_TAG_ARTIST, 0);
                    const char *album = mpd_song_get_tag(song, MPD_TAG_ALBUM, 0);
                    unsigned duration = mpd_song_get_duration(song);

                    queue.append({
                        .id=id,
                        .title=title ? QString::fromUtf8(title) : QString::fromUtf8(uri).section('/', -1),
                        .artist=artist ? QString::fromUtf8(artist) : tr("Unknown Artist"),
                        .album=album ? QString::fromUtf8(album) : tr("Unknown Album"),
                        .duration=QString("%1:%2").arg(duration / SECONDS_PER_MINUTE).arg(duration % SECONDS_PER_MINUTE, 2, DECIMAL_BASE, QChar('0')),
                        .uri=QString::fromUtf8(uri),
                        .durationSecs=duration
                    });
                }
                mpd_entity_free(entity);
            }
            mpd_response_finish(conn);
        } else {
            mpd_connection_clear_error(conn);
        }

        mpd_connection_free(conn);

        QMetaObject::invokeMethod(this, [this, queue]() -> void {
            m_queueModel->setQueue(queue);
            m_queueModel->setCurrentSongId(m_currentSongId);
        }, Qt::QueuedConnection);
    });
}

void MpdClient::playQueueId(int id)
{
    if (!m_connection) return;
    leaveIdle();
    mpd_run_play_id(m_connection, id);
    sendIdle();
}

void MpdClient::playTrack(const QString &uri)
{
    if (!m_connection || uri.isEmpty())
        return;

    m_timer->stop();
    leaveIdle();
    int id = mpd_run_add_id(m_connection, uri.toUtf8().constData());
    if (id != -1) {
        mpd_run_play_id(m_connection, id);
    }
    m_timer->start(TIMER_INTERVAL);
    sendIdle();
}

void MpdClient::playAlbum(const QString &artistName, const QString &albumName, const QString &mbid)
{
    if (!m_connection || albumName.isEmpty())
        return;

    QThreadPool::globalInstance()->start([this, artistName, albumName, mbid]() -> void {
        struct mpd_connection *conn = mpd_connection_new("localhost", MPD_PORT, MPD_TIMEOUT_MS);
        if (mpd_connection_get_error(conn) != MPD_ERROR_SUCCESS) {
            qWarning() << "Failed to connect to MPD in background thread:" << mpd_connection_get_error_message(conn);
            mpd_connection_free(conn);
            return;
        }

        if (!mpd_run_clear(conn)) {
            mpd_connection_clear_error(conn);
            mpd_connection_free(conn);
            return;
        }

        QList<SortableSong> songList = getSongsForAlbum(conn, artistName, albumName, mbid);

        if (!songList.isEmpty()) {
            mpd_command_list_begin(conn, false);
            for (const auto &s : songList) {
                mpd_send_add(conn, s.uri.toUtf8().constData());
            }
            mpd_command_list_end(conn);
            if (!mpd_response_finish(conn)) {
                mpd_connection_clear_error(conn);
            }
        }

        if (!mpd_run_play(conn)) {
            qWarning() << "Failed to start playing album:" << mpd_connection_get_error_message(conn);
            mpd_connection_clear_error(conn);
        }

        mpd_connection_free(conn);

        QMetaObject::invokeMethod(this, [this]() -> void {
            updateStatus();
        }, Qt::QueuedConnection);
    });
}

void MpdClient::addAlbum(const QString &artistName, const QString &albumName, const QString &mbid)
{
    if (!m_connection || albumName.isEmpty())
        return;

    QThreadPool::globalInstance()->start([this, artistName, albumName, mbid]() -> void {
        struct mpd_connection *conn = mpd_connection_new("localhost", MPD_PORT, MPD_TIMEOUT_MS);
        if (mpd_connection_get_error(conn) != MPD_ERROR_SUCCESS) {
            qWarning() << "Failed to connect to MPD in background thread:" << mpd_connection_get_error_message(conn);
            mpd_connection_free(conn);
            return;
        }

        QList<SortableSong> songList = getSongsForAlbum(conn, artistName, albumName, mbid);

        if (!songList.isEmpty()) {
            mpd_command_list_begin(conn, false);
            for (const auto &s : songList) {
                mpd_send_add(conn, s.uri.toUtf8().constData());
            }
            mpd_command_list_end(conn);
            if (!mpd_response_finish(conn)) {
                mpd_connection_clear_error(conn);
            }
        }

        mpd_connection_free(conn);
    });
}

void MpdClient::addTrack(const QString &uri)
{
    if (!m_connection || uri.isEmpty())
        return;

    m_timer->stop();
    leaveIdle();
    mpd_run_add(m_connection, uri.toUtf8().constData());
    sendIdle();
    m_timer->start(TIMER_INTERVAL);
}

void MpdClient::addPath(const QString &path)
{
    addTrack(path);
}

auto MpdClient::getSongsForAlbum(struct mpd_connection *conn, const QString &artistName, const QString &albumName, const QString &mbid) -> QList<SortableSong>
{
    QList<SortableSong> songs;
    if (!conn) return songs;

    auto fetchSongs = [&]() -> bool {
        struct mpd_song *song = nullptr;
        bool any = false;
        while ((song = mpd_recv_song(conn)) != nullptr) {
            any = true;
            const char *uri = mpd_song_get_uri(song);
            const char *title = mpd_song_get_tag(song, MPD_TAG_TITLE, 0);
            const char *trackStr = mpd_song_get_tag(song, MPD_TAG_TRACK, 0);
            const char *discStr = mpd_song_get_tag(song, MPD_TAG_DISC, 0);
            unsigned duration = mpd_song_get_duration(song);

            SortableSong s;
            s.uri = uri ? QString::fromUtf8(uri) : "";
            s.title = title ? QString::fromUtf8(title) : tr("Unknown Title");
            s.duration = QString("%1:%2").arg(duration / SECONDS_PER_MINUTE).arg(duration % SECONDS_PER_MINUTE, 2, DECIMAL_BASE, QChar('0'));
            s.track = trackStr ? std::atoi(trackStr) : 0;
            s.disc = discStr ? std::atoi(discStr) : 0;
            
            songs.append(s);
            mpd_song_free(song);
        }
        if (!mpd_response_finish(conn)) {
            mpd_connection_clear_error(conn);
        }
        return any;
    };

    auto searchByArtist = [&](mpd_tag_type artistTag) -> bool {
        if (!mpd_search_db_songs(conn, true)) return false;
        
        if (!artistName.isEmpty() && artistName != tr("Unknown Artist")) {
            mpd_search_add_tag_constraint(conn, MPD_OPERATOR_DEFAULT, artistTag, artistName.toUtf8().constData());
        }
        mpd_search_add_tag_constraint(conn, MPD_OPERATOR_DEFAULT, MPD_TAG_ALBUM, albumName.toUtf8().constData());
        
        if (!mpd_search_commit(conn)) {
            mpd_connection_clear_error(conn);
            return false;
        }
        return fetchSongs();
    };

    if (!mbid.isEmpty()) {
        if (!mpd_search_db_songs(conn, true)) {
            mpd_connection_clear_error(conn);
        } else {
            mpd_search_add_tag_constraint(conn, MPD_OPERATOR_DEFAULT, MPD_TAG_MUSICBRAINZ_ALBUMID, mbid.toUtf8().constData());
            
            if (!mpd_search_commit(conn)) {
                mpd_connection_clear_error(conn);
            } else {
                if (fetchSongs() && !songs.isEmpty()) {
                    QCollator collator;
                    collator.setNumericMode(true);
                    std::ranges::sort(songs, [&](const SortableSong &a, const SortableSong &b) -> bool {
                        if (a.disc != b.disc) return a.disc < b.disc;
                        if (a.track != b.track) return a.track < b.track;
                        return collator.compare(a.uri, b.uri) < 0;
                    });
                    return songs;
                }
            }
        }
        songs.clear();
    }

    if (!searchByArtist(MPD_TAG_ALBUM_ARTIST)) {
        songs.clear();
        searchByArtist(MPD_TAG_ARTIST);
    }

    QCollator collator;
    collator.setNumericMode(true);
    std::ranges::sort(songs, [&](const SortableSong &a, const SortableSong &b) -> bool {
        if (a.disc != b.disc) return a.disc < b.disc;
        if (a.track != b.track) return a.track < b.track;
        return collator.compare(a.uri, b.uri) < 0;
    });

    return songs;
}

void MpdClient::browsePath(const QString &path)
{
    QThreadPool::globalInstance()->start([this, path]() -> void {
        struct mpd_connection *conn = mpd_connection_new("localhost", MPD_PORT, MPD_TIMEOUT_MS);
        if (mpd_connection_get_error(conn) != MPD_ERROR_SUCCESS) {
            qWarning() << "Failed to connect to MPD in background thread:" << mpd_connection_get_error_message(conn);
            mpd_connection_free(conn);
            return;
        }

        QList<BrowserItem> items;

        if (!path.isEmpty()) {
            QString parent = "";
            int idx = static_cast<int>(path.lastIndexOf('/'));
            if (idx >= 0)
                parent = path.left(idx);
            items.append({.name="..", .path=parent, .isDir=true});
        }

        if (mpd_send_list_meta(conn, path.toUtf8().constData())) {
            struct mpd_entity *entity = nullptr;
            while ((entity = mpd_recv_entity(conn)) != nullptr) {
                if (mpd_entity_get_type(entity) == MPD_ENTITY_TYPE_DIRECTORY) {
                    const struct mpd_directory *dir = mpd_entity_get_directory(entity);
                    items.append({.name=QString::fromUtf8(mpd_directory_get_path(dir)).section('/', -1),
                                  .path=QString::fromUtf8(mpd_directory_get_path(dir)),
                                  .isDir=true});
                } else if (mpd_entity_get_type(entity) == MPD_ENTITY_TYPE_SONG) {
                    const struct mpd_song *song = mpd_entity_get_song(entity);
                    const char *title = mpd_song_get_tag(song, MPD_TAG_TITLE, 0);
                    QString name = title ? QString::fromUtf8(title)
                                         : QString::fromUtf8(mpd_song_get_uri(song)).section('/', -1);
                    items.append({.name=name, .path=QString::fromUtf8(mpd_song_get_uri(song)), .isDir=false});
                }
                mpd_entity_free(entity);
            }
            mpd_response_finish(conn);
        }

        QCollator collator;
        collator.setNumericMode(true);

        std::ranges::sort(items, [&](const BrowserItem &a, const BrowserItem &b) -> bool {
            if (a.name == "..") return true;
            if (b.name == "..") return false;
            if (a.isDir != b.isDir) return a.isDir;
            return collator.compare(a.name, b.name) < 0;
        });

        mpd_connection_free(conn);

        QMetaObject::invokeMethod(this, [this, items, path]() -> void {
            m_browserModel->setItems(items);
            m_currentPath = path;
            emit currentPathChanged();
        }, Qt::QueuedConnection);
    });
}

void MpdClient::quitApplication()
{
    QCoreApplication::quit();
}

void MpdClient::toggleFullscreen()
{
    if (m_window) {
        if (m_window->visibility() == QWindow::FullScreen) {
            m_window->setVisibility(QWindow::Windowed);
        } else {
            m_window->setVisibility(QWindow::FullScreen);
        }
    }
}

void MpdClient::toggleWindow()
{
    if (m_window) {
        if (m_window->isVisible() && m_window->isActive()) {
            m_window->hide();
        } else {
            m_window->show();
            m_window->raise();
            m_window->requestActivate();
        }
    }
}

void MpdClient::fetchCoverForModel(int index, const QString &albumName)
{
    // Get safe copies of album data using thread-safe accessor
    QList<AlbumItem> albums = m_albumModel->albums();
    if (index < 0 || index >= albums.count()) return;
    
    QString mbid = albums[index].mbid;
    QString artist = albums[index].artist;
    QString uri = albums[index].uri;

    if (uri.isEmpty() && m_connection) {
        if (mpd_search_db_songs(m_connection, true)) {
            if (!mbid.isEmpty()) {
                mpd_search_add_tag_constraint(m_connection, MPD_OPERATOR_DEFAULT, MPD_TAG_MUSICBRAINZ_ALBUMID, mbid.toUtf8().constData());
            } else {
                mpd_search_add_tag_constraint(m_connection, MPD_OPERATOR_DEFAULT, MPD_TAG_ALBUM, albumName.toUtf8().constData());
            }
            if (mpd_search_commit(m_connection)) {
                struct mpd_song *song = mpd_recv_song(m_connection);
                if (song) {
                    const char *u = mpd_song_get_uri(song);
                    if (u) {
                        uri = QString::fromUtf8(u);
                        // Note: We're not updating the model directly here since we don't have thread-safe access
                    }
                    // If mbid not already stored, try to fetch it from the song
                    if (mbid.isEmpty()) {
                        const char *m = mpd_song_get_tag(song, MPD_TAG_MUSICBRAINZ_ALBUMID, 0);
                        if (m) {
                            mbid = QString::fromUtf8(m);
                        }
                    }
                    mpd_song_free(song);
                }
                mpd_response_finish(m_connection);
            } else {
                mpd_connection_clear_error(m_connection);
            }
        }
    }

    if (!uri.isEmpty()) {
        QByteArray mpdData = getMpdPicture(uri);
        if (!mpdData.isEmpty()) {
            QString cachePath = getCachePath(artist, albumName, mbid);
            QFile file(cachePath);
            if (file.open(QIODevice::WriteOnly)) {
                file.write(mpdData);
                file.close();
                m_albumModel->updateArt(index, "file://" + cachePath);
                return;
            }
        }
    }

    fetchAlbumArtFromAPIs({.artist=artist, .album=albumName, .mbid=mbid, .cachePath=getCachePath(artist, albumName, mbid), .isMainArt=false, .modelIndex=index});
}

void MpdClient::fetchAlbumArtFromAPIs(const FetchParams &params)
{
    // KISS: Sequential fallback logic implemented via nested callbacks.
    // While coroutines would be cleaner, avoiding extra dependencies or complex state machines
    // keeps the implementation portable and standard-compliant with Qt 6.
    auto onArtFound = [this, params](const QByteArray &data) -> void {
        QFile file(params.cachePath);
        if (file.open(QIODevice::WriteOnly)) {
            file.write(data);
            file.close();
            QString artUrl = "file://" + params.cachePath;
            if (params.isMainArt) {
                m_albumArt = artUrl;
                emit albumArtChanged();
            } else {
                m_albumModel->updateArt(params.modelIndex, artUrl);
            }
        } else if (params.isMainArt) {
             m_albumArt = "data:image/jpeg;base64," + data.toBase64();
             emit albumArtChanged();
        }
    };

    // Create shared pointers to capture by value instead of reference
    auto tryMusicHoarders = std::make_shared<std::function<void()>>();
    *tryMusicHoarders = [this, params, onArtFound, tryMusicHoarders]() -> void {
        if (params.artist == "Unknown Artist" || params.album == "Unknown Album") return;
        
        // Try covers.musichoarders.xyz
        QUrl url(QString("https://covers.musichoarders.xyz/cover/%1/%2")
            .arg(params.artist).arg(params.album));
        
        QNetworkRequest request(url);
        request.setRawHeader("User-Agent", "Quester/1.0");
        request.setAttribute(QNetworkRequest::Http2AllowedAttribute, false);
        
        QNetworkReply *reply = m_networkManager->get(request);
        AlbumModel::connect(reply, &QNetworkReply::finished, this, [reply, onArtFound]() -> void {
            reply->deleteLater();
            if (reply->error() == QNetworkReply::NoError) {
                onArtFound(reply->readAll());
            }
        });
    };

    auto tryAudioDb = [this, params, onArtFound, tryMusicHoarders]() -> void {
        if (params.artist == "Unknown Artist" || params.album == "Unknown Album") return;
        
        QUrl url("https://www.theaudiodb.com/api/v1/json/123/searchalbum.php");
        QUrlQuery query;
        query.addQueryItem("s", params.artist);
        query.addQueryItem("a", params.album);
        url.setQuery(query);
        
        QNetworkRequest request(url);
        request.setRawHeader("User-Agent", "Quester/1.0");
        request.setAttribute(QNetworkRequest::Http2AllowedAttribute, false);
        
        QNetworkReply *reply = m_networkManager->get(request);
        AlbumModel::connect(reply, &QNetworkReply::finished, this, [this, reply, onArtFound, tryMusicHoarders]() -> void {
            reply->deleteLater();
            if (reply->error() != QNetworkReply::NoError) {
                if (tryMusicHoarders && *tryMusicHoarders) {
                    (*tryMusicHoarders)();
                }
                return;
            }
            
            QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
            QJsonArray albumArray = doc.object()["album"].toArray();
            if (albumArray.isEmpty()) {
                if (tryMusicHoarders && *tryMusicHoarders) {
                    (*tryMusicHoarders)();
                }
                return;
            }
            
            QString imageUrl = albumArray.first().toObject()["strAlbumThumb"].toString();
            if (imageUrl.isEmpty()) {
                if (tryMusicHoarders && *tryMusicHoarders) {
                    (*tryMusicHoarders)();
                }
                return;
            }
            
            QNetworkRequest imgReq((QUrl(imageUrl)));
            imgReq.setRawHeader("User-Agent", "Quester/1.0");
            imgReq.setAttribute(QNetworkRequest::Http2AllowedAttribute, false);
            QNetworkReply *imgReply = m_networkManager->get(imgReq);
            AlbumModel::connect(imgReply, &QNetworkReply::finished, this, [imgReply, onArtFound, tryMusicHoarders]() -> void {
                imgReply->deleteLater();
                if (imgReply->error() == QNetworkReply::NoError) {
                    onArtFound(imgReply->readAll());
                } else {
                    if (tryMusicHoarders && *tryMusicHoarders) {
                        (*tryMusicHoarders)();
                    }
                }
            });
        });
    };

    if (!params.mbid.isEmpty()) {
        QUrl url("https://coverartarchive.org/release/" + params.mbid + "/front");
        QNetworkRequest request(url);
        request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
        request.setAttribute(QNetworkRequest::Http2AllowedAttribute, false);
        
        QNetworkReply *reply = m_networkManager->get(request);
        AlbumModel::connect(reply, &QNetworkReply::finished, this, [this, reply, tryAudioDb, onArtFound]() -> void {
            reply->deleteLater();
            if (reply->error() == QNetworkReply::NoError) {
                onArtFound(reply->readAll());
            } else {
                tryAudioDb();
            }
        });
    } else {
        tryAudioDb();
    }
}

auto MpdClient::getCachePath(const QString &artist, const QString &album, const QString &mbid) -> QString
{
    QString cacheDir = QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + "/covers/";
    QDir dir(cacheDir);
    if (!dir.exists()) {
        dir.mkpath(".");
    }
    if (!mbid.isEmpty()) {
        return cacheDir + mbid + ".jpg";
    }
    QByteArray hashName = QCryptographicHash::hash((artist + album).toUtf8(), QCryptographicHash::Md5).toHex();
    return cacheDir + hashName + ".jpg";
}

void MpdClient::sortAlbums(QList<AlbumItem> &albums)
{
    QCollator collator;
    collator.setNumericMode(true);
    collator.setCaseSensitivity(Qt::CaseInsensitive);

    if (m_sortMode == SortMode::Artist) {
        std::ranges::sort(albums, [&](const AlbumItem &a, const AlbumItem &b) -> bool {
            // Use artistSortName for proper sorting (e.g., "Beatles, The" instead of "The Beatles")
            QString aSortName = a.artistSortName.isEmpty() ? a.artist : a.artistSortName;
            QString bSortName = b.artistSortName.isEmpty() ? b.artist : b.artistSortName;
            int res = collator.compare(aSortName, bSortName);
            if (res != 0) return res < 0;
            return collator.compare(a.name, b.name) < 0;
        });
    } else if (m_sortMode == SortMode::ArtistYear) {
        std::ranges::sort(albums, [&](const AlbumItem &a, const AlbumItem &b) -> bool {
            // Use artistSortName for proper sorting
            QString aSortName = a.artistSortName.isEmpty() ? a.artist : a.artistSortName;
            QString bSortName = b.artistSortName.isEmpty() ? b.artist : b.artistSortName;
            int res = collator.compare(aSortName, bSortName);
            if (res != 0) return res < 0;
            if (a.year != b.year) return a.year < b.year;
            return collator.compare(a.name, b.name) < 0;
        });
    } else {
        std::ranges::sort(albums, [&](const AlbumItem &a, const AlbumItem &b) -> bool {
            int res = collator.compare(a.name, b.name);
            if (res != 0) return res < 0;
            return collator.compare(a.artist, b.artist) < 0;
        });
    }
}

auto MpdClient::getMpdPicture(const QString &uri) -> QByteArray
{
    if (!m_connection || uri.isEmpty())
        return {};

    const std::array<const char*, 2> cmds = {"readpicture", "albumart"};

    for (const char *cmd : cmds) {
        QByteArray buffer;
        long long offset = 0;
        long long totalSize = 0;

        while (true) {
            if (!mpd_send_command(m_connection, cmd, uri.toUtf8().constData(), // NOLINT(cppcoreguidelines-pro-type-vararg)
                    QByteArray::number(offset).constData(), nullptr)) {
                mpd_connection_clear_error(m_connection);
                break;
            }

            struct mpd_pair *pair = mpd_recv_pair(m_connection);
            long long chunkSize = -1;

            while (pair != nullptr) {
                QString name = QString::fromUtf8(pair->name);
                QString value = QString::fromUtf8(pair->value);

                if (name == "size") {
                    totalSize = value.toLongLong();
                } else if (name == "binary") {
                    chunkSize = value.toLongLong();
                    mpd_return_pair(m_connection, pair);
                    break;
                }

                mpd_return_pair(m_connection, pair);
                pair = mpd_recv_pair(m_connection);
            }

            if (chunkSize > 0) {
                QByteArray chunk(chunkSize, 0);
                if (mpd_recv_binary(m_connection, chunk.data(), chunkSize)) {
                    buffer.append(chunk);
                    offset += chunkSize;
                } else {
                    mpd_connection_clear_error(m_connection);
                    buffer.clear();
                    break;
                }
            }

            if (!mpd_response_finish(m_connection)) {
                mpd_connection_clear_error(m_connection);
                buffer.clear();
                break;
            }

            if (chunkSize <= 0 || offset >= totalSize)
                break;
        }

        if (!buffer.isEmpty())
            return buffer;
    }

    return {};
}

void MpdClient::saveLibraryToCache(const QList<AlbumItem> &albums)
{
    QString cacheDir = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
    QDir dir(cacheDir);
    if (!dir.exists()) {
        dir.mkpath(".");
    }
    QString cachePath = cacheDir + "/library.cache";
    QFile file(cachePath);
    if (!file.open(QIODevice::WriteOnly)) {
        qWarning() << "Could not open library cache for writing:" << file.errorString();
        return;
    }

    QJsonArray jsonArray;
    for (const AlbumItem &album : albums) {
        QJsonObject albumObject;
        albumObject["musicbrainz_albumid"] = album.mbid;
        albumObject["artist"] = album.artist;
        albumObject["name"] = album.name;
        albumObject["artUrl"] = album.artUrl;
        albumObject["uri"] = album.uri;
        albumObject["year"] = album.year;
        jsonArray.append(albumObject);
    }

    QJsonDocument doc(jsonArray);
    file.write(doc.toJson());
    file.close();
}

auto MpdClient::loadLibraryFromCacheInternal() -> QList<AlbumItem>
{
    QString cachePath = QStandardPaths::writableLocation(QStandardPaths::CacheLocation)
                        + "/library.cache";
    QFile file(cachePath);
    if (!file.exists() || !file.open(QIODevice::ReadOnly)) {
        return {};
    }

    QByteArray data = file.readAll();
    file.close();

    QJsonDocument doc = QJsonDocument::fromJson(data);
    if (!doc.isArray()) {
        qWarning() << "Library cache is corrupted or not a JSON array.";
        return {};
    }

    QJsonArray jsonArray = doc.array();
    QList<AlbumItem> albums;
    albums.reserve(jsonArray.size());

    for (const auto &value : jsonArray) {
        if (!value.isObject())
            continue;
        QJsonObject obj = value.toObject();
        albums.append(
            {.artist=obj.value("artist").toString(),
             .name=obj.value("name").toString(),
             .artUrl=obj.value("artUrl").toString(),
             .mbid=obj.value("musicbrainz_albumid").toString(),
             .uri=obj.value("uri").toString(),
             .artLoading=false,
             .year=obj.value("year").toInt()});
    }
    return albums;
}

auto MpdClient::albumModel() const -> AlbumModel * { return m_albumModel; }
auto MpdClient::trackModel() const -> TrackModel * { return m_trackModel; }
auto MpdClient::playlistModel() const -> PlaylistModel * { return m_playlistModel; }
auto MpdClient::playlistTrackModel() const -> PlaylistTrackModel * { return m_playlistTrackModel; }

void MpdClient::fetchJspfPlaylist(const QString &playlistIdentifier)
{
    if (playlistIdentifier.isEmpty())
        return;

    // Extract playlist ID from URL if needed
    QString playlistId = playlistIdentifier;
    if (playlistIdentifier.contains("/")) {
        // Extract UUID from URL like https://listenbrainz.org/playlist/uuid
        playlistId = playlistIdentifier.section('/', -1);
    }

    // Fetch the JSPF playlist from ListenBrainz API
    QUrl url(QString("https://api.listenbrainz.org/1/playlist/%1").arg(playlistId));
    QNetworkRequest request(url);
    request.setAttribute(QNetworkRequest::Http2AllowedAttribute, false);
    if (!m_lbToken.isEmpty()) {
        request.setRawHeader("Authorization", QString("Token %1").arg(m_lbToken).toUtf8());
    }

    QNetworkReply *reply = m_networkManager->get(request);
    QObject::connect(reply, &QNetworkReply::finished, this, [this, reply, playlistId]() -> void {
        reply->deleteLater();
        
        if (reply->error() != QNetworkReply::NoError) {
            qWarning() << "Failed to fetch JSPF playlist:" << reply->errorString();
            return;
        }

        QByteArray data = reply->readAll();
        // Use fromUtf8 for proper UTF-8 handling of international characters
        qDebug().noquote() << "JSPF Playlist response:" << QString::fromUtf8(data);
        
        QJsonParseError error;
        QJsonDocument doc = QJsonDocument::fromJson(data, &error);
        if (error.error != QJsonParseError::NoError) {
            qWarning() << "Failed to parse JSPF playlist:" << error.errorString();
            return;
        }

        QJsonObject root = doc.object();
        QJsonObject playlist = root["playlist"].toObject();
        
        // Extract playlist info
        QString title = playlist["title"].toString();
        QString creator = playlist["creator"].toString();
        QString identifier = playlist["identifier"].toString();
        QString date = playlist["date"].toString();
        
        // Store full playlist data
        QVariantMap playlistData;
        playlistData["title"] = title;
        playlistData["creator"] = creator;
        playlistData["identifier"] = identifier;
        playlistData["date"] = date;
        playlistData["annotation"] = playlist["annotation"].toString();
        // Use fromUtf8 for proper UTF-8 handling of international characters
        playlistData["original_data"] = QString::fromUtf8(data);
        
        // Parse tracks
        QJsonArray trackArray = playlist["track"].toArray();
        QList<PlaylistTrackItem> tracks;
        
        for (const auto &trackValue : trackArray) {
            QJsonObject trackObj = trackValue.toObject();
            QJsonObject track = trackObj["track"].toObject();
            
            // Get track metadata from extension
            QJsonObject extension = track["extension"].toObject();
            QJsonObject lbData = extension["https://musicbrainz.org/doc/jspf#track"].toObject();
            
            QString trackTitle = track["title"].toString();
            QString trackCreator;
            QString trackAlbum;
            QString trackDuration;
            QString trackIdentifier = track["identifier"].toString();
            
            // Extract track info
            QJsonArray artistArray = track["artist"].toArray();
            if (!artistArray.isEmpty()) {
                trackCreator = artistArray.first().toObject()["name"].toString();
            }
            
            QJsonArray releaseArray = track["release"].toArray();
            if (!releaseArray.isEmpty()) {
                trackAlbum = releaseArray.first().toObject()["title"].toString();
            }
            
            int durationMs = track["duration"].toInt();
            if (durationMs > 0) {
                int minutes = durationMs / MS_PER_MINUTE;
                int seconds = (durationMs % MS_PER_MINUTE) / MS_PER_SECOND;
                trackDuration = QString("%1:%2").arg(minutes).arg(seconds, 2, DECIMAL_BASE, QChar('0'));
            }
            
            tracks.append({.title=trackTitle, .creator=trackCreator, .album=trackAlbum, .duration=trackDuration, .identifier=trackIdentifier});
        }
        
        // Update playlist track model
        m_playlistTrackModel->setTracks(tracks);
        
        // Update playlist data
        playlistData["trackCount"] = tracks.count();
        m_cachedJspfPlaylists[identifier] = playlistData;
        
        qDebug() << "Loaded JSPF playlist:" << title << "by" << creator << "with" << tracks.count() << "tracks";
    });
}

void MpdClient::saveJspfPlaylistToCache(const QString &identifier)
{
    if (!m_cachedJspfPlaylists.contains(identifier)) {
        qWarning() << "Playlist not found in cache:" << identifier;
        return;
    }
    
    QVariantMap playlistData = m_cachedJspfPlaylists[identifier];
    QString originalData = playlistData["original_data"].toString();
    
    if (originalData.isEmpty()) {
        qWarning() << "No JSPF data to save for playlist:" << identifier;
        return;
    }
    
    // Save to cache directory
    QString cacheDir = QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + "/playlists/";
    QDir dir(cacheDir);
    if (!dir.exists()) {
        dir.mkpath(".");
    }
    
    // Extract filename from identifier
    QString fileName = identifier.section('/', -1);
    if (fileName.isEmpty()) {
        fileName = "playlist_" + QDateTime::currentDateTime().toString("yyyyMMdd_hhmmss") + ".jspf";
    }
    
    QString cachePath = cacheDir + fileName;
    QFile file(cachePath);
    
    if (!file.open(QIODevice::WriteOnly)) {
        qWarning() << "Failed to open cache file for writing:" << file.errorString();
        return;
    }
    
    file.write(originalData.toUtf8());
    file.close();
    
    qInfo() << "Saved JSPF playlist to cache:" << cachePath;
    
    // Also load it into MPD if it contains MPD-compatible URIs
    // For now, just notify the user
    QVariantMap data = m_cachedJspfPlaylists[identifier];
    emit playlistSaved(data["title"].toString(), cachePath);
}

void MpdClient::pause()
{
    if (m_connection) {
        leaveIdle();
        mpd_run_pause(m_connection, true);
        sendIdle();
    }
}

void MpdClient::togglePlayPause()
{
    if (m_state == "play") {
        pause();
    } else {
        play();
    }
}

void MpdClient::stop()
{
    if (m_connection) {
        leaveIdle();
        mpd_run_stop(m_connection);
        sendIdle();
    }
}

void MpdClient::next()
{
    if (m_connection) {
        leaveIdle();
        mpd_run_next(m_connection);
        sendIdle();
    }
}

void MpdClient::previous()
{
    if (m_connection) {
        leaveIdle();
        mpd_run_previous(m_connection);
        sendIdle();
    }
}

void MpdClient::refreshPlaylists()
{
    if (!m_connection)
        return;

    bool wasIdle = m_isIdle;
    if (wasIdle) {
        leaveIdle();
    }

    if (mpd_send_list_playlists(m_connection)) {
        QStringList newPlaylists;
        struct mpd_playlist *pl = nullptr;
        while ((pl = mpd_recv_playlist(m_connection)) != nullptr) {
            newPlaylists.append(QString::fromUtf8(mpd_playlist_get_path(pl)));
            mpd_playlist_free(pl);
        }
        mpd_response_finish(m_connection);

        if (m_playlists != newPlaylists) {
            m_playlists = newPlaylists;
            emit playlistsChanged();
        }
    } else {
        mpd_connection_clear_error(m_connection);
    }

    if (wasIdle) {
        sendIdle();
    }
}

void MpdClient::loadPlaylist(const QString &name)
{
    if (!m_connection || name.isEmpty())
        return;
    leaveIdle();
    mpd_run_load(m_connection, name.toUtf8().constData());
    sendIdle();
}

void MpdClient::savePlaylist(const QString &name)
{
    if (!m_connection || name.isEmpty())
        return;
    leaveIdle();
    mpd_run_save(m_connection, name.toUtf8().constData());
    sendIdle();
}

void MpdClient::removePlaylist(const QString &name)
{
    if (!m_connection || name.isEmpty())
        return;
    leaveIdle();
    mpd_run_rm(m_connection, name.toUtf8().constData());
    sendIdle();
}

void MpdClient::clearQueue()
{
    if (!m_connection)
        return;
    leaveIdle();
    mpd_run_clear(m_connection);
    sendIdle();
}

void MpdClient::removeId(int id)
{
    if (!m_connection)
        return;
    leaveIdle();
    mpd_run_delete_id(m_connection, id);
    sendIdle();
}

void MpdClient::openFileLocation(const QString &path)
{
    if (path.isEmpty()) return;
    QFileInfo info(path);
    QDesktopServices::openUrl(QUrl::fromLocalFile(info.absolutePath()));
}

void MpdClient::handleLibraryUpdate(const QList<AlbumItem> &albums)
{
    QList<AlbumItem> sortedAlbums = albums;
    sortAlbums(sortedAlbums);
    m_albumModel->setAlbums(sortedAlbums);

    // OPTIMIZATION: Only fetch covers for the first N albums on initial load
    // This significantly improves startup time by avoiding hundreds of concurrent API calls
    // Remaining covers will be fetched on-demand when users browse to them
    int fetchCount = 0;
    for (int i = 0; i < sortedAlbums.count(); ++i) {
        if (sortedAlbums[i].artUrl.isEmpty()) {
            fetchCoverForModel(i, sortedAlbums[i].name);
            fetchCount++;
            if (fetchCount >= INITIAL_COVER_FETCH_LIMIT) {
                qInfo() << "Startup optimization: Limited initial cover fetch to" << fetchCount << "albums";
                break;
            }
        }
    }
}

void MpdClient::cleanup()
{
    if (m_state == "play") {
        pause();
    }
    m_timer->stop();
    if (m_notifier) {
        m_notifier->setEnabled(false);
    }
}

void MpdClient::setupSystemTray()
{
    if (!QSystemTrayIcon::isSystemTrayAvailable()) {
        return;
    }

    m_trayIcon = new QSystemTrayIcon(this); // NOLINT(cppcoreguidelines-owning-memory)
    m_trayMenu = new QMenu(); // NOLINT(cppcoreguidelines-owning-memory)
    
    // Create actions
    m_showAction = new QAction(tr("Show/Hide"), this); // NOLINT(cppcoreguidelines-owning-memory)
    m_playPauseAction = new QAction(tr("Play/Pause"), this); // NOLINT(cppcoreguidelines-owning-memory)
    m_nextAction = new QAction(tr("Next"), this); // NOLINT(cppcoreguidelines-owning-memory)
    m_prevAction = new QAction(tr("Previous"), this); // NOLINT(cppcoreguidelines-owning-memory)
    m_quitAction = new QAction(tr("Quit"), this); // NOLINT(cppcoreguidelines-owning-memory)

    // Connect actions
    QObject::connect(m_showAction, &QAction::triggered, this, &MpdClient::toggleWindow);
    QObject::connect(m_playPauseAction, &QAction::triggered, this, &MpdClient::togglePlayPause);
    QObject::connect(m_nextAction, &QAction::triggered, this, &MpdClient::next);
    QObject::connect(m_prevAction, &QAction::triggered, this, &MpdClient::previous);
    QObject::connect(m_quitAction, &QAction::triggered, this, &MpdClient::quitApplication);

    // Add actions to menu
    m_trayMenu->addAction(m_showAction);
    m_trayMenu->addSeparator();
    m_trayMenu->addAction(m_prevAction);
    m_trayMenu->addAction(m_playPauseAction);
    m_trayMenu->addAction(m_nextAction);
    m_trayMenu->addSeparator();
    m_trayMenu->addAction(m_quitAction);

    m_trayIcon->setContextMenu(m_trayMenu);
    
    // macOS specific tray icon setup
#ifdef Q_OS_MACOS
    // On macOS, use the 512x512 PNG for better quality in the menu bar
    m_trayIcon->setIcon(QIcon(":/Quester512.png"));
#else
    // On other platforms, use the SVG icon with theme fallback
    m_trayIcon->setIcon(QIcon::fromTheme("quester", QIcon(":/Quester.svg")));
#endif
    
    m_trayIcon->show();

    // Connect to state changes to update tray icon
    QObject::connect(this, &MpdClient::stateChanged, this, &MpdClient::updateTrayIcon);
    QObject::connect(this, &MpdClient::artistChanged, this, &MpdClient::updateTrayTooltip);
    QObject::connect(this, &MpdClient::titleChanged, this, &MpdClient::updateTrayTooltip);
    QObject::connect(this, &MpdClient::albumChanged, this, &MpdClient::updateTrayTooltip);

    // Handle tray activation (click)
    QObject::connect(m_trayIcon, &QSystemTrayIcon::activated, this, [this](QSystemTrayIcon::ActivationReason reason) -> void {
        if (reason == QSystemTrayIcon::Trigger || reason == QSystemTrayIcon::DoubleClick) {
            toggleWindow();
        }
    });

    updateTrayIcon();
    updateTrayTooltip();
}

void MpdClient::updateTrayIcon()
{
    if (!m_trayIcon) return;

#ifdef Q_OS_MACOS
    // On macOS, we keep the application icon constant in the menu bar
    // instead of changing it to play/pause indicators for better user experience
    m_trayIcon->setIcon(QIcon(":/Quester512.png"));
#else
    // On other platforms, show play/pause indicators
    QIcon icon;
    if (m_state == "play") {
        icon = QIcon::fromTheme("media-playback-start", QIcon(":/Quester.svg"));
    } else if (m_state == "pause") {
        icon = QIcon::fromTheme("media-playback-pause", QIcon(":/Quester.svg"));
    } else {
        icon = QIcon::fromTheme("quester", QIcon(":/Quester.svg"));
    }
    
    m_trayIcon->setIcon(icon);
#endif
}

void MpdClient::updateTrayTooltip()
{
    if (!m_trayIcon) return;

    QString tooltip = "Quester";
    if (!m_artist.isEmpty() && !m_title.isEmpty()) {
        tooltip += QString("\n%1 - %2").arg(m_artist, m_title);
        if (!m_album.isEmpty()) {
            tooltip += QString("\n%1").arg(m_album);
        }
    }
    
    m_trayIcon->setToolTip(tooltip);
}

auto MpdClient::mpdMusicDirectory() -> QString
{
    if (!m_connection) return {};
    
    QString musicDir;
    
    // Get MPD's configured music directory from settings or try MPD command
    QSettings settings("Quester", "Quester");
    musicDir = settings.value("mpdMusicDirectory").toString();
    
    // If not configured, try to get from MPD
    if (musicDir.isEmpty()) {
        // Fallback to common locations
        QByteArray envPath = qgetenv("HOME");
        if (!envPath.isEmpty()) {
            // Use fromUtf8 for proper handling of paths with international characters
            musicDir = QString::fromUtf8(envPath) + "/Music";
        }
        
        QString musicDir2 = QStandardPaths::writableLocation(QStandardPaths::MusicLocation);
        if (!musicDir2.isEmpty() && QDir(musicDir2).exists()) {
            musicDir = musicDir2;
        }
    }
    
    return musicDir;
}

// --- Deduplicator Implementation ---

struct DuplicateGroup {
    QString key;                    // Unique identifier for the duplicate group
    QString artist;
    QString title;
    QString album;
    unsigned duration = 0;
    QString recordingId;            // MusicBrainz Recording ID if available
    QList<QString> uris;           // All duplicate file URIs
};

void MpdClient::findDuplicates()
{
    QThreadPool::globalInstance()->start([this]() -> void {
        struct mpd_connection *conn = mpd_connection_new("localhost", MPD_PORT, MPD_TIMEOUT_MS);
        if (mpd_connection_get_error(conn) != MPD_ERROR_SUCCESS) {
            qWarning() << "Failed to connect to MPD in background thread:" << mpd_connection_get_error_message(conn);
            mpd_connection_free(conn);
            QMetaObject::invokeMethod(this, [this]() -> void {
                emit duplicatesFound(QVariantList());
            }, Qt::QueuedConnection);
            return;
        }

        // Regex pattern for common audio file extensions
        static const QRegularExpression audioFilePattern(
            R"(/\.(?:mp3|wav|aiff|aif|flac|aac|wma|ogg|m4a)$)",
            QRegularExpression::CaseInsensitiveOption
        );

        QList<DuplicateGroup> groups;
        QMap<QString, DuplicateGroup*> groupMap;  // key -> group pointer

        // Fetch all songs from MPD database
        if (mpd_send_list_meta(conn, "")) {
            struct mpd_entity *entity = nullptr;
            while ((entity = mpd_recv_entity(conn)) != nullptr) {
                if (mpd_entity_get_type(entity) == MPD_ENTITY_TYPE_SONG) {
                    const struct mpd_song *song = mpd_entity_get_song(entity);
                    
                    const char *uri = mpd_song_get_uri(song);
                    const char *artist = mpd_song_get_tag(song, MPD_TAG_ARTIST, 0);
                    const char *title = mpd_song_get_tag(song, MPD_TAG_TITLE, 0);
                    const char *album = mpd_song_get_tag(song, MPD_TAG_ALBUM, 0);
                    const char *recordingId = mpd_song_get_tag(song, MPD_TAG_MUSICBRAINZ_TRACKID, 0);
                    unsigned duration = mpd_song_get_duration(song);

                    if (uri && title && artist) {
                        QString qUri = QString::fromUtf8(uri);
                        
                        // Only consider actual audio files using the regex
                        if (!audioFilePattern.match(qUri).hasMatch()) {
                            mpd_entity_free(entity);
                            continue;
                        }

                        QString qArtist = QString::fromUtf8(artist);
                        QString qTitle = QString::fromUtf8(title);
                        QString qAlbum = album ? QString::fromUtf8(album) : "";
                        QString qRecordingId = recordingId ? QString::fromUtf8(recordingId) : "";

                        // Build a unique key for deduplication
                        // Only consider songs with MusicBrainz Recording ID to avoid false positives
                        if (!qRecordingId.isEmpty()) {
                            QString key = "mbid:" + qRecordingId;

                            if (groupMap.contains(key)) {
                                // Add to existing group
                                groupMap[key]->uris.append(qUri);
                            } else {
                                // Create new group
                                DuplicateGroup group;
                                group.key = key;
                                group.artist = qArtist;
                                group.title = qTitle;
                                group.album = qAlbum;
                                group.duration = duration;
                                group.recordingId = qRecordingId;
                                group.uris.append(qUri);
                                groups.append(group);
                                groupMap[key] = &groups.last();
                            }
                        }
                    }
                }
                mpd_entity_free(entity);
            }
            mpd_response_finish(conn);
        } else {
            mpd_connection_clear_error(conn);
        }

        mpd_connection_free(conn);

        // Filter to only groups with actual duplicates (more than 1 URI)
        QVariantList duplicateList;
        for (const auto &group : groups) {
            if (group.uris.size() > 1) {
                QVariantMap groupMap;
                groupMap["artist"] = group.artist;
                groupMap["title"] = group.title;
                groupMap["album"] = group.album;
                groupMap["duration"] = QString("%1:%2")
                    .arg(group.duration / SECONDS_PER_MINUTE)
                    .arg(group.duration % SECONDS_PER_MINUTE, 2, DECIMAL_BASE, QChar('0'));
                groupMap["count"] = group.uris.size();
                groupMap["uris"] = QVariant::fromValue(group.uris);
                groupMap["recordingId"] = group.recordingId;
                duplicateList.append(groupMap);
            }
        }

        qDebug() << "Deduplicator: Found" << duplicateList.size() << "duplicate groups";
        QMetaObject::invokeMethod(this, [this, duplicateList]() -> void {
            emit duplicatesFound(duplicateList);
        }, Qt::QueuedConnection);
    });
}

void MpdClient::deleteSelectedDuplicates(const QVariantList &selectedUris)
{
    if (selectedUris.isEmpty()) {
        emit duplicatesDeleted(0);
        return;
    }

    QString musicDir = mpdMusicDirectory();
    int deletedCount = 0;

    for (const QVariant &uriVar : selectedUris) {
        QString uri = uriVar.toString();
        if (uri.isEmpty()) continue;

        // Determine the actual file path
        QString filePath;
        
        if (uri.startsWith("file://")) {
            filePath = uri.remove(0, FILE_SCHEME.length());
        } else if (uri.startsWith("/")) {
            // If it's an absolute path, use it directly
            filePath = uri;
        } else {
            // Relative path - prepend MPD music directory
            if (!musicDir.isEmpty()) {
                // Handle both trailing slash and no trailing slash
                if (musicDir.endsWith('/')) {
                    filePath = musicDir + uri;
                } else {
                    filePath = musicDir + "/" + uri;
                }
            } else {
                filePath = uri;
            }
        }

        // Delete the file from disk manually
        QFile file(filePath);
        if (file.exists()) {
            if (file.remove()) {
                deletedCount++;
                qDebug() << "Deleted:" << filePath;
            } else {
                qWarning() << "Failed to delete file:" << filePath << file.errorString();
            }
        } else {
            qWarning() << "File does not exist:" << filePath;
        }
    }

    // Tell MPD to update its database to reflect the changes
    if (m_connection) {
        leaveIdle();
        mpd_run_update(m_connection, "");
        sendIdle();
    }

    // Refresh our local library
    refreshLibrary();

    emit duplicatesDeleted(deletedCount);
}

void MpdClient::playAlbumInternal(const QList<SortableSong> &songs)
{
    if (!m_connection) return;

    m_timer->stop();
    leaveIdle();

    if (!mpd_run_clear(m_connection)) {
        mpd_connection_clear_error(m_connection);
        m_timer->start(TIMER_INTERVAL);
        sendIdle();
        return;
    }

    if (!songs.isEmpty()) {
        mpd_command_list_begin(m_connection, false);
        for (const auto &s : songs) {
            mpd_send_add(m_connection, s.uri.toUtf8().constData());
        }
        mpd_command_list_end(m_connection);
        if (!mpd_response_finish(m_connection)) {
            mpd_connection_clear_error(m_connection);
        }
    }

    if (!mpd_run_play(m_connection)) {
        qWarning() << "Failed to start playing album:" << mpd_connection_get_error_message(m_connection);
        mpd_connection_clear_error(m_connection);
    }

    sendIdle();
    m_timer->start(TIMER_INTERVAL);
    updateStatus();
}

void MpdClient::addAlbumInternal(const QList<SortableSong> &songs)
{
    if (!m_connection) return;

    m_timer->stop();
    leaveIdle();

    if (!songs.isEmpty()) {
        mpd_command_list_begin(m_connection, false);
        for (const auto &s : songs) {
            mpd_send_add(m_connection, s.uri.toUtf8().constData());
        }
        mpd_command_list_end(m_connection);
        if (!mpd_response_finish(m_connection)) {
            mpd_connection_clear_error(m_connection);
        }
    }

    sendIdle();
    m_timer->start(TIMER_INTERVAL);
}

void MpdClient::handleQueueUpdate(const QList<QueueItem> &queue)
{
    m_queueModel->setQueue(queue);
    m_queueModel->setCurrentSongId(m_currentSongId);
}

void MpdClient::handleBrowseUpdate(const QList<BrowserItem> &items)
{
    m_browserModel->setItems(items);
}

void MpdClient::loadMostPlayedPlaylist()
{
    // Implementation to load most played tracks playlist
    // This would typically use statistics from StatisticsManager
    qWarning() << "loadMostPlayedPlaylist not implemented";
}

void MpdClient::refreshLibraryAfterDelete()
{
    // No longer needed - functionality moved to deleteSelectedDuplicates
    // Kept for source compatibility
}

void MpdClient::fetchArtistImage(const QString &artistName, QJSValue callback)
{
    if (artistName.isEmpty() || artistName == "Unknown Artist" || !callback.isCallable())
        return;

    QString cachePath = getArtistImageCachePath(artistName);

    // Check if we have a cached image
    if (QFile::exists(cachePath)) {
        QString artUrl = "file://" + cachePath;
        QJSValueList args;
        args << artUrl;
        callback.call(args);
        return;
    }

    // Try TheAudioDB for artist image
    auto tryAudioDb = [this, artistName, cachePath, callback]() -> void {
        QUrl url("https://www.theaudiodb.com/api/v1/json/1/search.php");
        QUrlQuery query;
        query.addQueryItem("s", artistName);
        url.setQuery(query);

        QNetworkRequest request(url);
        request.setRawHeader("User-Agent", "Quester/1.0");
        request.setAttribute(QNetworkRequest::Http2AllowedAttribute, false);

        QNetworkReply *reply = m_networkManager->get(request);
        QObject::connect(reply, &QNetworkReply::finished, this, [this, reply, artistName, cachePath, callback]() -> void {
            reply->deleteLater();
            
            if (reply->error() != QNetworkReply::NoError) {
                qWarning() << "TheAudioDB artist image request failed:" << reply->errorString();
                callback.call();
                return;
            }

            QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
            QJsonArray artistArray = doc.object()["artists"].toArray();
            
            if (artistArray.isEmpty()) {
                qWarning() << "TheAudioDB artist not found:" << artistName;
                callback.call();
                return;
            }

            QJsonObject artist = artistArray.first().toObject();
            QString strArtistThumb = artist["strArtistThumb"].toString();
            QString strArtistFanart = artist["strArtistFanart"].toString();
            
            QString imageUrl = strArtistThumb.isEmpty() ? strArtistFanart : strArtistThumb;
            
            if (imageUrl.isEmpty()) {
                qWarning() << "TheAudioDB artist image not available:" << artistName;
                callback.call();
                return;
            }

            // Fetch the image
            QNetworkRequest imgReq((QUrl(imageUrl)));
            imgReq.setRawHeader("User-Agent", "Quester/1.0");
            imgReq.setAttribute(QNetworkRequest::Http2AllowedAttribute, false);
            QNetworkReply *imgReply = m_networkManager->get(imgReq);
            QObject::connect(imgReply, &QNetworkReply::finished, this, [imgReply, cachePath, callback]() -> void {
                imgReply->deleteLater();
                
                if (imgReply->error() == QNetworkReply::NoError) {
                    QByteArray imageData = imgReply->readAll();
                    QFile file(cachePath);
                    if (file.open(QIODevice::WriteOnly)) {
                        file.write(imageData);
                        file.close();
                        QString artUrl = "file://" + cachePath;
                        QJSValueList args;
                        args << artUrl;
                        callback.call(args);
                    } else {
                        qWarning() << "Failed to save artist image to cache:" << cachePath;
                        callback.call();
                    }
                } else {
                    qWarning() << "Artist image download failed:" << imgReply->errorString();
                    callback.call();
                }
            });
        });
    };

    tryAudioDb();
}

QString MpdClient::getArtistImageCachePath(const QString &artistName)
{
    QString cacheDir = QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + "/artist_images/";
    QDir dir(cacheDir);
    if (!dir.exists()) {
        dir.mkpath(".");
    }
    QByteArray hashName = QCryptographicHash::hash(artistName.toUtf8(), QCryptographicHash::Md5).toHex();
    return cacheDir + hashName + ".jpg";
}