#include "quester.h"
#include <mpd/connection.h>
#include <mpd/pair.h>
#include <mpd/recv.h>
#include <mpd/response.h>
#include <mpd/search.h>
#include <mpd/song.h>
#include <mpd/status.h>
#include <mpd/tag.h>
#include <QByteArray>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>
#include <QStandardPaths>
#include <QUrlQuery>

// --- AlbumModel Implementation ---
int AlbumModel::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid())
        return 0;
    return m_albums.count();
}

QVariant AlbumModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= m_albums.count())
        return QVariant();
    const AlbumItem &item = m_albums[index.row()];
    if (role == NameRole)
        return item.name;
    if (role == ArtRole)
        return item.artUrl;
    if (role == ArtistRole)
        return item.artist; // Return artist for ArtistRole
    return QVariant();
}

QHash<int, QByteArray> AlbumModel::roleNames() const
{
    QHash<int, QByteArray> roles;
    roles[NameRole] = "name";
    roles[ArtRole] = "art";
    roles[ArtistRole] = "artist"; // Map ArtistRole to "artist"
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
    emit dataChanged(this->index(index), this->index(index), {ArtRole});
}

// --- TrackModel Implementation ---
int TrackModel::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid())
        return 0;
    return m_tracks.count();
}

QVariant TrackModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= m_tracks.count())
        return QVariant();
    const TrackItem &item = m_tracks[index.row()];
    if (role == TitleRole)
        return item.title;
    if (role == DurationRole)
        return item.duration;
    if (role == UriRole)
        return item.uri;
    return QVariant();
}

QHash<int, QByteArray> TrackModel::roleNames() const
{
    QHash<int, QByteArray> roles;
    roles[TitleRole] = "title";
    roles[DurationRole] = "duration";
    roles[UriRole] = "uri";
    return roles;
}

void TrackModel::setTracks(const QList<TrackItem> &tracks)
{
    beginResetModel();
    m_tracks = tracks;
    endResetModel();
}

MpdClient::MpdClient(QObject *parent)
    : QObject(parent)
    , m_connection(nullptr)
    , m_networkManager(new QNetworkAccessManager(this))
    , m_albumModel(new AlbumModel(this))
    , m_trackModel(new TrackModel(this))
    , m_isIdle(false)
    , m_notifier(nullptr)
{
    m_timer = new QTimer(this);
    QObject::connect(m_timer, &QTimer::timeout, this, &MpdClient::updateStatus);
    m_timer->start(1000);
    loadLibraryFromCache();

    connect();
}

MpdClient::~MpdClient()
{
    if (m_notifier)
        delete m_notifier;
    if (m_connection) {
        if (m_isIdle)
            mpd_run_noidle(m_connection);
        mpd_connection_free(m_connection);
    }
}

void MpdClient::connect()
{
    m_connection = mpd_connection_new("localhost", 6600, 30000);
    if (mpd_connection_get_error(m_connection) != MPD_ERROR_SUCCESS) {
        qWarning() << "Failed to connect to MPD:" << mpd_connection_get_error_message(m_connection);
        mpd_connection_free(m_connection);
        m_connection = nullptr;
    } else {
        qInfo() << "Successfully connected to MPD.";

        // Setup socket notifier for idle events
        int fd = mpd_connection_get_fd(m_connection);
        if (m_notifier)
            delete m_notifier;
        m_notifier = new QSocketNotifier(fd, QSocketNotifier::Read, this);
        QObject::connect(m_notifier, &QSocketNotifier::activated, this, &MpdClient::handleMpdEvent);

        updateStatus(); // Initial status fetch
        sendIdle();     // Enter idle loop
    }
}

void MpdClient::sendIdle()
{
    if (m_connection) {
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

    // Read the idle response
    enum mpd_idle events = mpd_recv_idle(m_connection, true);

    if (events & (MPD_IDLE_PLAYER | MPD_IDLE_MIXER)) {
        updateStatus();
    }

    // Re-enter idle mode
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
        // Update player state
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

        // Use mpd_run_current_song for simpler synchronous fetching
        struct mpd_song *song = mpd_run_current_song(m_connection);
        if (song) {
            const char *artist_tag = mpd_song_get_tag(song, MPD_TAG_ARTIST, 0);
            const char *title_tag = mpd_song_get_tag(song, MPD_TAG_TITLE, 0);
            const char *album_tag = mpd_song_get_tag(song, MPD_TAG_ALBUM, 0);
            const char *uri_tag = mpd_song_get_uri(song);

            setArtist(artist_tag ? QString::fromUtf8(artist_tag) : "Unknown Artist");
            setTitle(title_tag ? QString::fromUtf8(title_tag) : "Unknown Title");
            setAlbum(album_tag ? QString::fromUtf8(album_tag) : "Unknown Album");
            if (uri_tag)
                m_currentUri = QString::fromUtf8(uri_tag);

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
                m_currentSongId = song_id;
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

    // Generate a deterministic color based on the album name hash
    unsigned int hash = 0;
    for (QChar c : album) {
        hash = c.unicode() + (hash << 6) + (hash << 16) - hash;
    }
    QString color = QString("#%1").arg(hash & 0xFFFFFF, 6, 16, QLatin1Char('0'));
    QString letter = album.left(1).toUpper();

    // Create a simple SVG placeholder
    QString svg = QString(
                      "<svg width='200' height='200' xmlns='http://www.w3.org/2000/svg'>"
                      "<rect width='100%' height='100%' fill='%1'/>"
                      "<text x='50%' y='50%' font-size='80' fill='white' text-anchor='middle' "
                      "dy='.3em'>%2</text>"
                      "</svg>")
                      .arg(color, letter);

    m_albumArt = "data:image/svg+xml;base64," + svg.toUtf8().toBase64();
    emit albumArtChanged();

    // Check Cache
    QString cacheDir = QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + "/covers/";
    // Use artist and album for a more unique hash for caching
    QByteArray hashName
        = QCryptographicHash::hash((m_artist + album).toUtf8(), QCryptographicHash::Md5).toHex();
    QString cachePath = cacheDir + hashName + ".jpg";

    // If cache exists, use it and return
    if (QFile::exists(cachePath)) {
        m_albumArt = "file://" + cachePath;
        emit albumArtChanged();
        return;
    }

    // --- MPD Native Art Fetch ---
    QByteArray mpdData = getMpdPicture(m_currentUri);
    if (!mpdData.isEmpty()) {
        QDir dir = QFileInfo(cachePath).dir();
        if (!dir.exists())
            dir.mkpath(".");
        QFile file(cachePath);
        if (file.open(QIODevice::WriteOnly)) {
            file.write(mpdData);
            file.close();
            m_albumArt = "file://" + cachePath;
            emit albumArtChanged();
            return;
        }
    }

    // --- TheAudioDB API Search ---
    if (m_artist == "Unknown Artist" || album == "Unknown Album")
        return;

    QUrl url("https://www.theaudiodb.com/api/v1/json/123/searchalbum.php");
    QUrlQuery query;
    query.addQueryItem("s", m_artist);
    query.addQueryItem("a", album);
    url.setQuery(query);

    QNetworkRequest request(url);
    request.setRawHeader("User-Agent", "Quester/1.0");

    QNetworkReply *reply = m_networkManager->get(request);
    QObject::connect(reply, &QNetworkReply::finished, this, [this, reply, cachePath]() {
        reply->deleteLater();
        if (reply->error() == QNetworkReply::NoError) {
            QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
            QJsonArray albumArray = doc.object()["album"].toArray();

            if (!albumArray.isEmpty()) {
                QString imageUrl = albumArray.first().toObject()["strAlbumThumb"].toString();
                if (!imageUrl.isEmpty()) {
                    QNetworkRequest imgReq((QUrl(imageUrl)));
                    imgReq.setRawHeader(
                        "User-Agent",
                        "Quester/1.0"); // TheAudioDB doesn't strictly require, but good practice
                    QNetworkReply *imgReply = m_networkManager->get(imgReq);

                    QObject::connect(
                        imgReply, &QNetworkReply::finished, this, [this, imgReply, cachePath]() {
                            imgReply->deleteLater();
                            if (imgReply->error() == QNetworkReply::NoError) {
                                QByteArray data = imgReply->readAll();

                                // Save to cache
                                QDir dir = QFileInfo(cachePath).dir();
                                if (!dir.exists())
                                    dir.mkpath(".");
                                QFile file(cachePath);
                                if (file.open(QIODevice::WriteOnly)) {
                                    file.write(data);
                                    file.close();
                                    m_albumArt = "file://" + cachePath;
                                } else {
                                    // Fallback to data URI if cache write fails
                                    m_albumArt = "data:image/jpeg;base64," + data.toBase64();
                                }
                                emit albumArtChanged();
                            }
                        });
                }
            }
        }
    });
}

// --- Properties ---
QString MpdClient::artist() const
{
    return m_artist;
}
QString MpdClient::title() const
{
    return m_title;
}
QString MpdClient::album() const
{
    return m_album;
}
QString MpdClient::state() const
{
    return m_state;
}
QString MpdClient::albumArt() const
{
    return m_albumArt;
}
qint64 MpdClient::duration() const
{
    return m_duration;
}
qint64 MpdClient::elapsed() const
{
    return m_elapsed;
}
int MpdClient::currentAlbumIndex() const
{
    return m_currentAlbumIndex;
}

void MpdClient::setArtist(const QString &artist)
{
    if (m_artist != artist) {
        m_artist = artist;
        emit artistChanged();
    }
}
void MpdClient::setTitle(const QString &title)
{
    if (m_title != title) {
        m_title = title;
        emit titleChanged();
    }
}
void MpdClient::setAlbum(const QString &album)
{
    if (m_album != album) {
        m_album = album;
        emit albumChanged();
    }
}
void MpdClient::setState(const QString &state)
{
    if (m_state != state) {
        m_state = state;
        emit stateChanged();
    }
}

void MpdClient::setWindow(QQuickWindow *window)
{
    m_window = window;
}

// --- Playback Controls ---
void MpdClient::play()
{
    if (m_connection) {
        leaveIdle();
        mpd_run_play(m_connection);
        sendIdle();
    }
}

void MpdClient::refreshLibrary()
{
    if (!m_connection)
        return;

    m_timer->stop();

    leaveIdle();

    QList<AlbumItem> albums;
    QSet<QString> addedAlbums;

    // Optimize: Fetch Album and Artist in one go using "list album group artist"
    if (mpd_send_command(m_connection, "list", "album", "group", "artist", NULL)) {
        struct mpd_pair *pair;
        QString currentArtist = "Unknown Artist";

        while ((pair = mpd_recv_pair(m_connection)) != NULL) {
            QString tagName = QString::fromUtf8(pair->name);
            QString tagValue = QString::fromUtf8(pair->value);

            if (tagName == "Artist") {
                currentArtist = tagValue;
            } else if (tagName == "Album") {
                QString albumName = tagValue;
                if (!albumName.isEmpty() && !addedAlbums.contains(albumName)) {
                    // Check cache immediately for initial display
                    QString cacheDir = QStandardPaths::writableLocation(
                                           QStandardPaths::CacheLocation)
                                       + "/covers/";
                    QByteArray hashName = QCryptographicHash::hash(
                                              (currentArtist + albumName).toUtf8(),
                                              QCryptographicHash::Md5)
                                              .toHex();
                    QString cachePath = cacheDir + hashName + ".jpg";
                    QString art = QFile::exists(cachePath) ? "file://" + cachePath : "";

                    albums.append({currentArtist, albumName, art, "", false});
                    addedAlbums.insert(albumName);
                }
            }
            mpd_return_pair(m_connection, pair);
        }

        if (mpd_connection_get_error(m_connection) != MPD_ERROR_SUCCESS) {
            qWarning() << "Error reading library response:"
                       << mpd_connection_get_error_message(m_connection);
            mpd_connection_clear_error(m_connection);
        }

        mpd_response_finish(m_connection);
    } else {
        qWarning() << "Failed to send list command:"
                   << mpd_connection_get_error_message(m_connection);
        mpd_connection_clear_error(m_connection);
    }

    m_albumModel->setAlbums(albums);
    saveLibraryToCache(albums);

    // Trigger fetches for missing art
    for (int i = 0; i < albums.count(); ++i) {
        if (albums[i].artUrl.isEmpty()) {
            fetchCoverForModel(i, albums[i].name);
        }
    }

    sendIdle();
    m_timer->start(1000);
}

void MpdClient::loadAlbumTracks(int index)
{
    if (!m_connection || index < 0 || index >= m_albumModel->m_albums.count())
        return;

    m_timer->stop();

    QString albumName = m_albumModel->m_albums[index].name;
    leaveIdle();

    QList<TrackItem> tracks;
    if (mpd_search_db_songs(m_connection, true)) {
        mpd_search_add_tag_constraint(
            m_connection, MPD_OPERATOR_DEFAULT, MPD_TAG_ALBUM, albumName.toUtf8().constData());
        if (mpd_search_commit(m_connection)) {
            struct mpd_song *song;
            while ((song = mpd_recv_song(m_connection)) != NULL) {
                const char *title = mpd_song_get_tag(song, MPD_TAG_TITLE, 0);
                unsigned duration = mpd_song_get_duration(song);
                const char *uri = mpd_song_get_uri(song);

                QString titleStr = title ? QString::fromUtf8(title) : "Unknown Title";
                QString durStr
                    = QString("%1:%2").arg(duration / 60).arg(duration % 60, 2, 10, QChar('0'));
                QString uriStr = uri ? QString::fromUtf8(uri) : "";

                tracks.append({titleStr, durStr, uriStr});
                mpd_song_free(song);
            }
        }
    }
    m_trackModel->setTracks(tracks);
    m_timer->start(1000);
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
    sendIdle();
}

void MpdClient::playAlbum(const QString &artistName, const QString &albumName)
{
    if (!m_connection || albumName.isEmpty())
        return;

    m_timer->stop();
    // It's good practice to force an update after a manual action.
    // We'll do it after the timer is restarted.

    leaveIdle(); // Exit idle mode before sending commands

    // 1. Clear the current playlist.
    if (!mpd_run_clear(m_connection)) {
        qWarning() << "Failed to clear MPD playlist:"
                   << mpd_connection_get_error_message(m_connection);
        mpd_connection_clear_error(m_connection);
        sendIdle();
        return;
    }

    // 2. Construct a filter string and use mpd_run_find_add to add all matching songs.
    // This is an efficient, single command to MPD.
    // Create temporary copies to escape quotes, as the input strings are const.
    QString escapedArtist = artistName;
    escapedArtist.replace('\'', QLatin1String("\\'"));
    QString escapedAlbum = albumName;
    escapedAlbum.replace('\'', QLatin1String("\\'"));

    QString filter;
    // MPD filter syntax requires escaping single quotes.
    if (!artistName.isEmpty() && artistName != "Unknown Artist") {
        filter = QString("((artist == '%1') AND (album == '%2'))").arg(escapedArtist, escapedAlbum);
    } else {
        filter = QString("(album == '%1')").arg(escapedAlbum);
    }

    // The function mpd_run_find_add may not exist in all libmpdclient versions.
    // We send the "findadd" command manually for better compatibility.
    if (!mpd_send_command(m_connection, "findadd", filter.toUtf8().constData(), NULL)) {
        qWarning() << "Failed to send 'findadd' command:"
                   << mpd_connection_get_error_message(m_connection);
        mpd_connection_clear_error(m_connection);
        sendIdle();
        return;
    }

    // We must finish the response, even if we don't use the result.
    if (!mpd_response_finish(m_connection)) {
        qWarning() << "Failed to finish 'findadd' response:"
                   << mpd_connection_get_error_message(m_connection);
        mpd_connection_clear_error(m_connection);
        sendIdle();
        return;
    }

    // 3. Start playing from the beginning of the (now populated) playlist.
    if (!mpd_run_play(m_connection)) {
        qWarning() << "Failed to start playing album:"
                   << mpd_connection_get_error_message(m_connection);
        mpd_connection_clear_error(m_connection);
    }

    sendIdle(); // Re-enter idle mode
    m_timer->start(1000);
    updateStatus(); // Force an immediate status update
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

void MpdClient::fetchCoverForModel(int index, const QString &albumName)
{
    // Try to find URI if missing
    if (index >= 0 && index < m_albumModel->m_albums.count()) {
        if (m_albumModel->m_albums[index].uri.isEmpty()) {
            // Find a song in this album to get a representative URI
            if (mpd_search_db_songs(m_connection, true)) {
                mpd_search_add_tag_constraint(
                    m_connection,
                    MPD_OPERATOR_DEFAULT,
                    MPD_TAG_ALBUM,
                    albumName.toUtf8().constData());
                if (mpd_search_commit(m_connection)) {
                    struct mpd_song *song = mpd_recv_song(m_connection);
                    if (song) {
                        const char *u = mpd_song_get_uri(song);
                        if (u)
                            m_albumModel->m_albums[index].uri = QString::fromUtf8(u);
                        mpd_song_free(song);
                    }
                }
                mpd_response_finish(m_connection);
            }
        }

        // Try MPD fetch
        QString uri = m_albumModel->m_albums[index].uri;
        if (!uri.isEmpty()) {
            QByteArray mpdData = getMpdPicture(uri);
            if (!mpdData.isEmpty()) {
                QString cacheDir = QStandardPaths::writableLocation(QStandardPaths::CacheLocation)
                                   + "/covers/";
                QByteArray hashName
                    = QCryptographicHash::hash(
                          (m_albumModel->m_albums[index].artist + albumName).toUtf8(),
                          QCryptographicHash::Md5)
                          .toHex();
                QString cachePath = cacheDir + hashName + ".jpg";

                QDir dir = QFileInfo(cachePath).dir();
                if (!dir.exists())
                    dir.mkpath(".");
                QFile file(cachePath);
                if (file.open(QIODevice::WriteOnly)) {
                    file.write(mpdData);
                    file.close();
                    m_albumModel->updateArt(index, "file://" + cachePath);
                    return;
                }
            }
        }
    }

    QUrl url("https://www.theaudiodb.com/api/v1/json/123/searchalbum.php");
    QUrlQuery query;
    // Use artist from model for better accuracy
    if (index >= 0 && index < m_albumModel->m_albums.count()
        && m_albumModel->m_albums[index].artist != "Unknown Artist") {
        query.addQueryItem("s", m_albumModel->m_albums[index].artist);
    }
    query.addQueryItem("a", albumName);
    url.setQuery(query);

    QNetworkRequest request(url);
    request.setRawHeader("User-Agent", "Quester/1.0");

    QNetworkReply *reply = m_networkManager->get(request);
    QObject::connect(reply, &QNetworkReply::finished, this, [this, reply, index, albumName]() {
        reply->deleteLater();
        if (reply->error() == QNetworkReply::NoError) {
            QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
            QJsonArray albumArray = doc.object()["album"].toArray();
            if (!albumArray.isEmpty()) {
                QString imageUrl = albumArray.first().toObject()["strAlbumThumb"].toString();
                if (!imageUrl.isEmpty()) {
                    QNetworkRequest imgReq((QUrl(imageUrl)));
                    imgReq.setRawHeader("User-Agent", "Quester/1.0");
                    QNetworkReply *imgReply = m_networkManager->get(imgReq);

                    QObject::connect(
                        imgReply,
                        &QNetworkReply::finished,
                        this,
                        [this, imgReply, index, albumName]() {
                            imgReply->deleteLater();
                            if (imgReply->error() == QNetworkReply::NoError) {
                                QByteArray data = imgReply->readAll();
                                QString cacheDir = QStandardPaths::writableLocation(
                                                       QStandardPaths::CacheLocation)
                                                   + "/covers/";
                                QByteArray hashName;
                                if (index >= 0 && index < m_albumModel->m_albums.count()) {
                                    hashName = QCryptographicHash::hash(
                                                   (m_albumModel->m_albums[index].artist + albumName)
                                                       .toUtf8(),
                                                   QCryptographicHash::Md5)
                                                   .toHex();
                                } else {
                                    hashName = QCryptographicHash::hash(
                                                   albumName.toUtf8(), QCryptographicHash::Md5)
                                                   .toHex();
                                }
                                QString cachePath = cacheDir + hashName + ".jpg";

                                QDir dir = QFileInfo(cachePath).dir();
                                if (!dir.exists())
                                    dir.mkpath(".");

                                QFile file(cachePath);
                                if (file.open(QIODevice::WriteOnly)) {
                                    file.write(data);
                                    file.close();
                                    m_albumModel->updateArt(index, "file://" + cachePath);
                                }
                            }
                        });
                }
            }
        }
    });
}

QByteArray MpdClient::getMpdPicture(const QString &uri)
{
    if (!m_connection || uri.isEmpty())
        return QByteArray();

    // Try readpicture (embedded) then albumart (external file)
    const char *cmds[] = {"readpicture", "albumart"};

    for (const char *cmd : cmds) {
        QByteArray buffer;
        long long offset = 0;
        long long totalSize = 0;

        while (true) {
            if (!mpd_send_command(
                    m_connection,
                    cmd,
                    uri.toUtf8().constData(),
                    QByteArray::number(offset).constData(),
                    NULL)) {
                mpd_connection_clear_error(m_connection);
                break;
            }

            struct mpd_pair *pair = mpd_recv_pair(m_connection);
            long long chunkSize = -1;

            while (pair != NULL) {
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

    return QByteArray();
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
        albumObject["artist"] = album.artist;
        albumObject["name"] = album.name;
        albumObject["artUrl"] = album.artUrl;
        albumObject["uri"] = album.uri;
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
        return; // Not an error if cache doesn't exist yet
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

    for (const QJsonValue &value : jsonArray) {
        if (!value.isObject())
            continue;
        QJsonObject obj = value.toObject();
        albums.append(
            {obj.value("artist").toString(),
             obj.value("name").toString(),
             obj.value("artUrl").toString(),
             obj.value("uri").toString(),
             false});
    }

    if (!albums.isEmpty()) {
        m_albumModel->setAlbums(albums);
    }
}

AlbumModel *MpdClient::albumModel() const
{
    return m_albumModel;
}
TrackModel *MpdClient::trackModel() const
{
    return m_trackModel;
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