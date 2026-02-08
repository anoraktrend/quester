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

constexpr int TIMER_INTERVAL = 100;
constexpr int MPD_PORT = 6600;
constexpr int MPD_TIMEOUT_MS = 30000;
constexpr int SECONDS_PER_MINUTE = 60;
constexpr int DECIMAL_BASE = 10;
constexpr int HASH_SHIFT_LOW = 6;
constexpr int HASH_SHIFT_HIGH = 16;
constexpr int COLOR_MASK = 0xFFFFFF;
constexpr int HEX_COLOR_WIDTH = 6;
constexpr int HEX_BASE = 16;

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
    if (index < 0 || index >= m_albums.count())
        return;
    m_albums[index].artUrl = url;
    m_albums[index].artLoading = false;
    emit dataChanged(this->index(index), this->index(index), {static_cast<int>(AlbumRoles::ArtRole)});
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

MpdClient::MpdClient(QObject *parent)
    : QObject(parent)
    , m_connection(nullptr)
    , m_networkManager(new QNetworkAccessManager(this))
    , m_albumModel(new AlbumModel(this))
    , m_trackModel(new TrackModel(this))
    , m_browserModel(new BrowserModel(this))
    , m_queueModel(new QueueModel(this))
    , m_timer(new QTimer(this))
    , m_notifier(nullptr)
    , m_stats(new StatisticsManager(this))
{
    QSettings settings("Quester", "Quester");
    m_sortMode = static_cast<SortMode>(settings.value("sortMode", static_cast<int>(SortMode::Artist)).toInt());

    QObject::connect(m_timer, &QTimer::timeout, this, &MpdClient::updateStatus);
    m_timer->start(TIMER_INTERVAL);
    loadLibraryFromCache();

    connect();
}

MpdClient::~MpdClient()
{
    if (m_notifier)
        delete m_notifier;
    if (m_connection) {
        mpd_connection_free(m_connection);
    }
}

void MpdClient::connect()
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
                const auto &albums = m_albumModel->m_albums;
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
                if (m_currentSongId != -1 && m_currentSongPlayTime > 5000) {
                    m_stats->logPlay(m_lastArtist, m_lastTitle, m_lastAlbum, m_currentSongPlayTime);
                    emit weeklyStatsChanged();
                    emit monthlyStatsChanged();
                    emit yearlyStatsChanged();
                    emit allTimeStatsChanged();
                }
                m_currentSongPlayTime = 0;
                m_lastArtist = m_artist;
                m_lastTitle = m_title;
                m_lastAlbum = m_album;

                m_currentSongId = song_id;
                if (m_queueModel) {
                    m_queueModel->setCurrentSongId(m_currentSongId);
                }
                fetchAlbumArt(m_album);
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

    unsigned int hash = 0;
    for (QChar c : album) {
        hash = c.unicode() + (hash << HASH_SHIFT_LOW) + (hash << HASH_SHIFT_HIGH) - hash;
    }
    QString color = QString("#%1").arg(hash & COLOR_MASK, HEX_COLOR_WIDTH, HEX_BASE, QLatin1Char('0'));
    QString letter = album.left(1).toUpper();

    QString svg = QString(
                      "<svg width='200' height='200' xmlns='http://www.w3.org/2000/svg'>"
                      "<rect width='100%' height='100%' fill='%1'/>"
                      "<text x='50%' y='50%' font-size='80' fill='white' text-anchor='middle' "
                      "dy='.3em'>%2</text>"
                      "</svg>")
                      .arg(color, letter);

    m_albumArt = "data:image/svg+xml;base64," + svg.toUtf8().toBase64();
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

    fetchAlbumArtFromAPIs({m_artist, album, m_currentMbid, cachePath, true, -1});
}

auto MpdClient::artist() const -> QString { return m_artist; }
auto MpdClient::title() const -> QString { return m_title; }
auto MpdClient::album() const -> QString { return m_album; }
auto MpdClient::state() const -> QString { return m_state; }
auto MpdClient::albumArt() const -> QString { return m_albumArt; }
auto MpdClient::duration() const -> qint64 { return m_duration; }
auto MpdClient::elapsed() const -> qint64 { return m_elapsed; }
auto MpdClient::currentAlbumIndex() const -> int { return m_currentAlbumIndex; }
auto MpdClient::browserModel() const -> BrowserModel * { return m_browserModel; }
auto MpdClient::queueModel() const -> QueueModel * { return m_queueModel; }
auto MpdClient::currentPath() const -> QString { return m_currentPath; }
auto MpdClient::repeat() const -> bool { return m_repeat; }
auto MpdClient::random() const -> bool { return m_random; }
auto MpdClient::single() const -> bool { return m_single; }
auto MpdClient::consume() const -> bool { return m_consume; }
auto MpdClient::volume() const -> int { return m_volume; }
auto MpdClient::playlists() const -> QStringList { return m_playlists; }
auto MpdClient::sortMode() const -> SortMode { return m_sortMode; }
auto MpdClient::uri() const -> QString { return m_currentUri; }
auto MpdClient::weeklyStats() const -> QVariantMap { return m_stats->getWeeklyStats(); }
auto MpdClient::monthlyStats() const -> QVariantMap { return m_stats->getMonthlyStats(); }
auto MpdClient::yearlyStats() const -> QVariantMap { return m_stats->getYearlyStats(); }
auto MpdClient::allTimeStats() const -> QVariantMap { return m_stats->getAllTimeStats(); }

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

void MpdClient::setWindow(QQuickWindow *window) { m_window = window; }

void MpdClient::play() {
    if (m_connection) { leaveIdle(); mpd_run_play(m_connection); sendIdle(); }
}

void MpdClient::seek(qint64 time)
{
    if (m_connection) {
        leaveIdle();
        mpd_run_seek_current(m_connection, static_cast<float>(time), false);
        sendIdle();
        updateStatus();
    }
}

void MpdClient::seekTo(qint64 time)
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
    if (!m_connection)
        return;

    m_timer->stop();

    bool wasIdle = m_isIdle;
    if (wasIdle) {
        leaveIdle();
    }

    QList<AlbumItem> albums;
    QSet<QString> addedMbids;
    QSet<QString> addedArtistAlbums;

    if (mpd_send_command(m_connection, "list", "album", "group", "artist", "group", "date", "group", "musicbrainz_albumid", nullptr)) { // NOLINT(cppcoreguidelines-pro-type-vararg)
        struct mpd_pair *pair = nullptr;
        QString currentArtist = tr("Unknown Artist");
        QString currentMbid;
        int currentYear = 0;

        while ((pair = mpd_recv_pair(m_connection)) != nullptr) {
            QString tagName = QString::fromUtf8(pair->name);
            QString tagValue = QString::fromUtf8(pair->value);

            if (tagName == "Artist") {
                currentArtist = tagValue;
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
                        albums.append(AlbumItem{currentArtist, albumName, art, currentMbid, "", false, currentYear});
                    }
                }
            }
            mpd_return_pair(m_connection, pair);
        }

        if (mpd_connection_get_error(m_connection) != MPD_ERROR_SUCCESS) {
            qWarning() << "Error reading library response:" << mpd_connection_get_error_message(m_connection);
            mpd_connection_clear_error(m_connection);
        }

        mpd_response_finish(m_connection);
    } else {
        qWarning() << "Failed to send list command:" << mpd_connection_get_error_message(m_connection);
        mpd_connection_clear_error(m_connection);
    }

    sortAlbums(albums);
    m_albumModel->setAlbums(albums);
    saveLibraryToCache(albums);

    for (int i = 0; i < albums.count(); ++i) {
        if (albums[i].artUrl.isEmpty()) {
            fetchCoverForModel(i, albums[i].name);
        }
    }

    if (wasIdle) {
        sendIdle();
    }
    m_timer->start(TIMER_INTERVAL);
}

void MpdClient::loadAlbumTracks(int index)
{
    if (!m_connection || index < 0 || index >= m_albumModel->m_albums.count())
        return;

    m_timer->stop();

    QString albumName = m_albumModel->m_albums[index].name;
    QString artistName = m_albumModel->m_albums[index].artist;
    QString mbid = m_albumModel->m_albums[index].mbid;
    leaveIdle();

    QList<SortableSong> sortedTracks = getSongsForAlbum(artistName, albumName, mbid);
    QList<TrackItem> tracks;

    for (const auto &t : sortedTracks) {
        tracks.append({t.title, t.duration, t.uri});
    }

    m_trackModel->setTracks(tracks);
    m_timer->start(TIMER_INTERVAL);
    sendIdle();
}

void MpdClient::refreshQueue()
{
    if (!m_connection)
        return;

    m_timer->stop();
    bool wasIdle = m_isIdle;
    if (wasIdle) {
        leaveIdle();
    }

    QList<QueueItem> queue;
    if (mpd_send_list_queue_meta(m_connection)) {
        struct mpd_entity *entity = nullptr;
        while ((entity = mpd_recv_entity(m_connection)) != nullptr) {
            if (mpd_entity_get_type(entity) == MPD_ENTITY_TYPE_SONG) {
                const struct mpd_song *song = mpd_entity_get_song(entity);
                int id = static_cast<int>(mpd_song_get_id(song));
                const char *uri = mpd_song_get_uri(song);
                const char *title = mpd_song_get_tag(song, MPD_TAG_TITLE, 0);
                const char *artist = mpd_song_get_tag(song, MPD_TAG_ARTIST, 0);
                const char *album = mpd_song_get_tag(song, MPD_TAG_ALBUM, 0);
                unsigned duration = mpd_song_get_duration(song);

                queue.append({
                    id,
                    title ? QString::fromUtf8(title) : QString::fromUtf8(uri).section('/', -1),
                    artist ? QString::fromUtf8(artist) : tr("Unknown Artist"),
                    album ? QString::fromUtf8(album) : tr("Unknown Album"),
                    QString("%1:%2").arg(duration / SECONDS_PER_MINUTE).arg(duration % SECONDS_PER_MINUTE, 2, DECIMAL_BASE, QChar('0')),
                    QString::fromUtf8(uri)
                });
            }
            mpd_entity_free(entity);
        }
        mpd_response_finish(m_connection);
    } else {
        mpd_connection_clear_error(m_connection);
    }

    m_queueModel->setQueue(queue);
    m_queueModel->setCurrentSongId(m_currentSongId);

    if (wasIdle) {
        sendIdle();
    }
    m_timer->start(TIMER_INTERVAL);
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

    m_timer->stop();
    leaveIdle();

    if (!mpd_run_clear(m_connection)) {
        mpd_connection_clear_error(m_connection);
        m_timer->start(TIMER_INTERVAL);
        sendIdle();
        return;
    }

    QList<SortableSong> songList = getSongsForAlbum(artistName, albumName, mbid);

    if (!songList.isEmpty()) {
        mpd_command_list_begin(m_connection, false);
        for (const auto &s : songList) {
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

void MpdClient::addAlbum(const QString &artistName, const QString &albumName, const QString &mbid)
{
    if (!m_connection || albumName.isEmpty())
        return;

    m_timer->stop();
    leaveIdle();

    QList<SortableSong> songList = getSongsForAlbum(artistName, albumName, mbid);

    if (!songList.isEmpty()) {
        mpd_command_list_begin(m_connection, false);
        for (const auto &s : songList) {
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

auto MpdClient::getSongsForAlbum(const QString &artistName, const QString &albumName, const QString &mbid) -> QList<MpdClient::SortableSong>
{
    QList<SortableSong> songs;
    if (!m_connection) return songs;

    auto fetchSongs = [&]() -> bool {
        struct mpd_song *song = nullptr;
        bool any = false;
        while ((song = mpd_recv_song(m_connection)) != nullptr) {
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
        if (!mpd_response_finish(m_connection)) {
            mpd_connection_clear_error(m_connection);
        }
        return any;
    };

    auto searchByArtist = [&](mpd_tag_type artistTag) -> bool {
        if (!mpd_search_db_songs(m_connection, true)) return false;
        
        if (!artistName.isEmpty() && artistName != tr("Unknown Artist")) {
            mpd_search_add_tag_constraint(m_connection, MPD_OPERATOR_DEFAULT, artistTag, artistName.toUtf8().constData());
        }
        mpd_search_add_tag_constraint(m_connection, MPD_OPERATOR_DEFAULT, MPD_TAG_ALBUM, albumName.toUtf8().constData());
        
        if (!mpd_search_commit(m_connection)) {
            mpd_connection_clear_error(m_connection);
            return false;
        }
        return fetchSongs();
    };

    if (!mbid.isEmpty()) {
        if (!mpd_search_db_songs(m_connection, true)) {
            mpd_connection_clear_error(m_connection);
        } else {
            mpd_search_add_tag_constraint(m_connection, MPD_OPERATOR_DEFAULT, MPD_TAG_MUSICBRAINZ_ALBUMID, mbid.toUtf8().constData());
            
            if (!mpd_search_commit(m_connection)) {
                mpd_connection_clear_error(m_connection);
            } else {
                if (fetchSongs() && !songs.isEmpty()) {
                    QCollator collator;
                    collator.setNumericMode(true);
                    std::sort(songs.begin(), songs.end(), [&](const SortableSong &a, const SortableSong &b) -> bool {
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
    std::sort(songs.begin(), songs.end(), [&](const SortableSong &a, const SortableSong &b) -> bool {
        if (a.disc != b.disc) return a.disc < b.disc;
        if (a.track != b.track) return a.track < b.track;
        return collator.compare(a.uri, b.uri) < 0;
    });

    return songs;
}

void MpdClient::browsePath(const QString &path)
{
    if (!m_connection)
        return;

    m_timer->stop();
    bool wasIdle = m_isIdle;
    if (wasIdle) {
        leaveIdle();
    }

    QList<BrowserItem> items;

    if (!path.isEmpty()) {
        QString parent = "";
        int idx = static_cast<int>(path.lastIndexOf('/'));
        if (idx >= 0)
            parent = path.left(idx);
        items.append({"..", parent, true});
    }

    if (mpd_send_list_meta(m_connection, path.toUtf8().constData())) {
        struct mpd_entity *entity = nullptr;
        while ((entity = mpd_recv_entity(m_connection)) != nullptr) {
            if (mpd_entity_get_type(entity) == MPD_ENTITY_TYPE_DIRECTORY) {
                const struct mpd_directory *dir = mpd_entity_get_directory(entity);
                items.append({QString::fromUtf8(mpd_directory_get_path(dir)).section('/', -1),
                              QString::fromUtf8(mpd_directory_get_path(dir)),
                              true});
            } else if (mpd_entity_get_type(entity) == MPD_ENTITY_TYPE_SONG) {
                const struct mpd_song *song = mpd_entity_get_song(entity);
                const char *title = mpd_song_get_tag(song, MPD_TAG_TITLE, 0);
                QString name = title ? QString::fromUtf8(title)
                                     : QString::fromUtf8(mpd_song_get_uri(song)).section('/', -1);
                items.append({name, QString::fromUtf8(mpd_song_get_uri(song)), false});
            }
            mpd_entity_free(entity);
        }
        mpd_response_finish(m_connection);
    }

    QCollator collator;
    collator.setNumericMode(true);

    std::sort(items.begin(), items.end(), [&](const BrowserItem &a, const BrowserItem &b) -> bool {
        if (a.name == "..") return true;
        if (b.name == "..") return false;
        if (a.isDir != b.isDir) return a.isDir;
        return collator.compare(a.name, b.name) < 0;
    });

    m_browserModel->setItems(items);
    m_currentPath = path;
    emit currentPathChanged();

    if (wasIdle) {
        sendIdle();
    }
    m_timer->start(TIMER_INTERVAL);
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
    if (index < 0 || index >= m_albumModel->m_albums.count()) return;
    
    QString mbid = m_albumModel->m_albums[index].mbid;
    QString artist = m_albumModel->m_albums[index].artist;

    if (m_albumModel->m_albums[index].uri.isEmpty()) {
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
                    if (u)
                        m_albumModel->m_albums[index].uri = QString::fromUtf8(u);
                    // If mbid not already stored, try to fetch it from the song
                    if (mbid.isEmpty()) {
                        const char *m = mpd_song_get_tag(song, MPD_TAG_MUSICBRAINZ_ALBUMID, 0);
                        if (m) {
                            mbid = QString::fromUtf8(m);
                            m_albumModel->m_albums[index].mbid = mbid;
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

    QString uri = m_albumModel->m_albums[index].uri;
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

    fetchAlbumArtFromAPIs({artist, albumName, mbid, getCachePath(artist, albumName, mbid), false, index});
}

void MpdClient::fetchAlbumArtFromAPIs(const FetchParams &params)
{
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

    auto tryAudioDb = [this, params, onArtFound]() -> void {
        if (params.artist == "Unknown Artist" || params.album == "Unknown Album") return;
        
        QUrl url("https://www.theaudiodb.com/api/v1/json/123/searchalbum.php");
        QUrlQuery query;
        query.addQueryItem("s", params.artist);
        query.addQueryItem("a", params.album);
        url.setQuery(query);
        
        QNetworkRequest request(url);
        request.setRawHeader("User-Agent", "Quester/1.0");
        
        QNetworkReply *reply = m_networkManager->get(request);
        AlbumModel::connect(reply, &QNetworkReply::finished, this, [this, reply, onArtFound]() -> void {
            reply->deleteLater();
            if (reply->error() != QNetworkReply::NoError) return;
            
            QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
            QJsonArray albumArray = doc.object()["album"].toArray();
            if (albumArray.isEmpty()) return;
            
            QString imageUrl = albumArray.first().toObject()["strAlbumThumb"].toString();
            if (imageUrl.isEmpty()) return;
            
            QNetworkRequest imgReq((QUrl(imageUrl)));
            imgReq.setRawHeader("User-Agent", "Quester/1.0");
            QNetworkReply *imgReply = m_networkManager->get(imgReq);
            AlbumModel::connect(imgReply, &QNetworkReply::finished, this, [imgReply, onArtFound]() -> void {
                imgReply->deleteLater();
                if (imgReply->error() == QNetworkReply::NoError) {
                    onArtFound(imgReply->readAll());
                }
            });
        });
    };

    if (!params.mbid.isEmpty()) {
        QUrl url("https://coverartarchive.org/release/" + params.mbid + "/front");
        QNetworkRequest request(url);
        request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
        
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
        std::sort(albums.begin(), albums.end(), [&](const AlbumItem &a, const AlbumItem &b) -> bool {
            int res = collator.compare(a.artist, b.artist);
            if (res != 0) return res < 0;
            return collator.compare(a.name, b.name) < 0;
        });
    } else if (m_sortMode == SortMode::ArtistYear) {
        std::sort(albums.begin(), albums.end(), [&](const AlbumItem &a, const AlbumItem &b) -> bool {
            int res = collator.compare(a.artist, b.artist);
            if (res != 0) return res < 0;
            if (a.year != b.year) return a.year < b.year;
            return collator.compare(a.name, b.name) < 0;
        });
    } else {
        std::sort(albums.begin(), albums.end(), [&](const AlbumItem &a, const AlbumItem &b) -> bool {
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

void MpdClient::loadLibraryFromCache()
{
    QString cachePath = QStandardPaths::writableLocation(QStandardPaths::CacheLocation)
                        + "/library.cache";
    QFile file(cachePath);
    if (!file.exists() || !file.open(QIODevice::ReadOnly)) {
        return;
    }

    QByteArray data = file.readAll();
    file.close();

    QJsonDocument doc = QJsonDocument::fromJson(data);
    if (!doc.isArray()) {
        qWarning() << "Library cache is corrupted or not a JSON array.";
        return;
    }

    QJsonArray jsonArray = doc.array();
    QList<AlbumItem> albums;
    albums.reserve(jsonArray.size());

    for (const auto &value : jsonArray) {
        if (!value.isObject())
            continue;
        QJsonObject obj = value.toObject();
        albums.append(
            {obj.value("artist").toString(),
             obj.value("name").toString(),
             obj.value("artUrl").toString(),
             obj.value("musicbrainz_albumid").toString(),
             obj.value("uri").toString(),
             false,
             obj.value("year").toInt()});
    }

    if (!albums.isEmpty()) {
        sortAlbums(albums);
        m_albumModel->setAlbums(albums);
    }
}

auto MpdClient::albumModel() const -> AlbumModel * { return m_albumModel; }
auto MpdClient::trackModel() const -> TrackModel * { return m_trackModel; }

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

    m_trayIcon = new QSystemTrayIcon(this);
    m_trayMenu = new QMenu();
    
    // Create actions
    m_showAction = new QAction(tr("Show/Hide"), this);
    m_playPauseAction = new QAction(tr("Play/Pause"), this);
    m_nextAction = new QAction(tr("Next"), this);
    m_prevAction = new QAction(tr("Previous"), this);
    m_quitAction = new QAction(tr("Quit"), this);

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
    m_trayIcon->setIcon(QIcon::fromTheme("quester", QIcon(":/Quester.svg")));
    m_trayIcon->show();

    // Connect to state changes to update tray icon
    QObject::connect(this, &MpdClient::stateChanged, this, &MpdClient::updateTrayIcon);
    QObject::connect(this, &MpdClient::artistChanged, this, &MpdClient::updateTrayTooltip);
    QObject::connect(this, &MpdClient::titleChanged, this, &MpdClient::updateTrayTooltip);
    QObject::connect(this, &MpdClient::albumChanged, this, &MpdClient::updateTrayTooltip);

    // Handle tray activation (click)
    QObject::connect(m_trayIcon, &QSystemTrayIcon::activated, this, [this](QSystemTrayIcon::ActivationReason reason) {
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

    QIcon icon;
    if (m_state == "play") {
        icon = QIcon::fromTheme("media-playback-start", QIcon(":/Quester.svg"));
    } else if (m_state == "pause") {
        icon = QIcon::fromTheme("media-playback-pause", QIcon(":/Quester.svg"));
    } else {
        icon = QIcon::fromTheme("quester", QIcon(":/Quester.svg"));
    }
    
    m_trayIcon->setIcon(icon);
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
