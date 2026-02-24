#include "dbus.h"
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusVariant>
#include <QDBusObjectPath>
#include <algorithm>
#include <utility>
#include <QQuickWindow>

const int PLAYLIST_PATH_PREFIX_LENGTH = 34;
const qint64 MICROSECONDS_PER_SECOND = 1000000;
const int VOLUME_SCALE = 100;

QDBusArgument & operator<<(QDBusArgument &argument, const MprisPlaylist &playlist) {
    argument.beginStructure();
    argument << playlist.id << playlist.name << playlist.iconUri;
    argument.endStructure();
    return argument;
}

QDBusArgument & operator<<(QDBusArgument &argument, const MprisActivePlaylist &ap) {
    argument.beginStructure();
    argument << ap.valid << ap.playlist;
    argument.endStructure();
    return argument;
}

const QDBusArgument & operator>>(const QDBusArgument &argument, MprisPlaylist &playlist) {
    argument.beginStructure();
    argument >> playlist.id >> playlist.name >> playlist.iconUri;
    argument.endStructure();
    return argument; // NOLINT
}

const QDBusArgument & operator>>(const QDBusArgument &argument, MprisActivePlaylist &ap) {
    argument.beginStructure();
    argument >> ap.valid;
    argument >> ap.playlist.id >> ap.playlist.name >> ap.playlist.iconUri;
    argument.endStructure();
    return argument; // NOLINT
}

static auto encodePlaylistId(const QString &name) -> QString {
    // Encode UTF-8 playlist name to a D-Bus compatible path
    // First convert to UTF-8, then to hex for safe path characters
    return QStringLiteral("/org/mpris/MediaPlayer2/Playlists/") + QString::fromUtf8(name.toUtf8().toHex());
}

static auto decodePlaylistId(const QDBusObjectPath &path) -> QString {
    QString p = path.path();
    if (p.startsWith("/org/mpris/MediaPlayer2/Playlists/")) {
        QString hex = p.mid(PLAYLIST_PATH_PREFIX_LENGTH);
        // Decode from hex back to UTF-8 string
        return QString::fromUtf8(QByteArray::fromHex(hex.toUtf8()));
    }
    return {};
}

DBusService::DBusService(MpdClient *mpdClient, QObject *parent)
    : QObject(parent)
    , m_mpdClient(mpdClient)
    , m_connection(QDBusConnection::sessionBus())
{
    // Check if D-Bus connection is valid
    if (!m_connection.isConnected()) {
        qWarning() << "Failed to connect to D-Bus session bus";
        return;
    }

    // Create adaptors
    new MprisRootAdaptor(this);
    new MprisPlayerAdaptor(this);
    new MprisTrackListAdaptor(this);
    new MprisPlaylistsAdaptor(this);

    // Register the MPRIS interface
    if (!m_connection.registerObject("/org/mpris/MediaPlayer2", this, QDBusConnection::ExportAdaptors)) {
        qWarning() << "Failed to register MPRIS object on D-Bus:" << m_connection.lastError().message();
    }

    // Register the MPRIS service - use a well-known name that follows MPRIS specification
    // This follows the convention: org.mpris.MediaPlayer2.<application_name>
    QString serviceName = "org.mpris.MediaPlayer2.quester";
    if (!m_connection.registerService(serviceName)) {
        // If the well-known name is taken, try with PID suffix as a unique name
        serviceName = QString("org.mpris.MediaPlayer2.quester.%1").arg(QCoreApplication::applicationPid());
        if (!m_connection.registerService(serviceName)) {
            qWarning() << "Failed to register MPRIS service on D-Bus:" << m_connection.lastError().message();
        } else {
            qDebug() << "Successfully registered MPRIS service" << serviceName;
        }
    } else {
        qDebug() << "Successfully registered MPRIS service" << serviceName;
    }

    // Emit initial TrackListReplaced
    QString currentTrackId = "/org/mpris/MediaPlayer2/TrackList/NoTrack";
    if (m_mpdClient && m_mpdClient->queueModel()->m_currentSongId != -1) {
        QList<QueueItem> queue = m_mpdClient->queueModel()->m_queue;
        int currentIndex = -1;
        for (int i = 0; i < queue.size(); ++i) {
            if (queue[i].id == m_mpdClient->queueModel()->m_currentSongId) {
                currentIndex = i;
                break;
            }
        }
        if (currentIndex != -1) {
            currentTrackId = positionToTrackId(currentIndex);
        }
    }
    emit TrackListReplaced(tracks(), QDBusObjectPath(currentTrackId));

    // Connect to MPD client signals
    if (mpdClient) {
        connect(mpdClient, &MpdClient::stateChanged, this, &DBusService::broadcastProperties);
        connect(mpdClient, &MpdClient::volumeChanged, this, &DBusService::broadcastProperties);
        connect(mpdClient, &MpdClient::artistChanged, this, &DBusService::broadcastProperties);
        connect(mpdClient, &MpdClient::titleChanged, this, &DBusService::broadcastProperties);
        connect(mpdClient, &MpdClient::albumChanged, this, &DBusService::broadcastProperties);
        connect(mpdClient, &MpdClient::durationChanged, this, &DBusService::broadcastProperties);
        connect(mpdClient, &MpdClient::repeatChanged, this, &DBusService::broadcastProperties);
        connect(mpdClient, &MpdClient::randomChanged, this, &DBusService::broadcastProperties);
        connect(mpdClient, &MpdClient::singleChanged, this, &DBusService::broadcastProperties);
        connect(mpdClient, &MpdClient::currentSongChanged, this, &DBusService::broadcastProperties);
        mpdClient->refreshPlaylists();

        // Populate initial tracklist
        for (const auto &item : mpdClient->queueModel()->m_queue) {
            createTrackId(item.uri);
        }
    }
}

DBusService::~DBusService()
= default;

auto DBusService::canGoNext() const -> bool
{
    return m_mpdClient != nullptr;
}

auto DBusService::canGoPrevious() const -> bool
{
    return m_mpdClient != nullptr;
}

auto DBusService::canPlay() const -> bool
{
    return m_mpdClient != nullptr;
}

auto DBusService::canPause() const -> bool
{
    return m_mpdClient != nullptr;
}

auto DBusService::getMetadataForTrack(const QueueItem &item) const -> QVariantMap
{
    QVariantMap metadata;

    QString uri = item.uri;
    QString title = item.title;
    QString artist = item.artist;
    QString album = item.album;
    QString albumArt = m_mpdClient->albumArt();
    qint64 duration = item.durationSecs;

    // Only include metadata if we have a valid track
    if (uri.isEmpty() && title.isEmpty() && artist.isEmpty()) {
        return metadata;
    }

    // Create a proper track ID for MPRIS
    QString trackId = createTrackId(uri);
    metadata["mpris:trackid"] = QDBusObjectPath(trackId);
    
    // Basic track information
    if (!title.isEmpty()) {
        metadata["xesam:title"] = title;
    }
    if (!artist.isEmpty()) {
        metadata["xesam:artist"] = QVariantList() << artist;
    }
    if (!album.isEmpty()) {
        metadata["xesam:album"] = album;
    }
    if (duration > 0) {
        metadata["mpris:length"] = static_cast<quint64>(duration * MICROSECONDS_PER_SECOND);
    }

    // Add album art if available
    if (!albumArt.isEmpty()) {
        if (albumArt.startsWith("file://") || albumArt.startsWith("http://") || albumArt.startsWith("https://")) {
            metadata["mpris:artUrl"] = albumArt;
        } else {
            metadata["mpris:artUrl"] = QUrl::fromLocalFile(albumArt).toString();
        }
    }

    // Add URI
    if (!uri.isEmpty()) {
        metadata["xesam:url"] = uri;
    }

    return metadata;
}

auto DBusService::metadata() const -> QVariantMap
{
    if (!m_mpdClient || m_mpdClient->queueModel()->m_queue.isEmpty())
        return {};

    const auto &queue = m_mpdClient->queueModel()->m_queue;

    // First, try to use currentSongId if available
    int currentSongId = m_mpdClient->queueModel()->m_currentSongId;
    for (const auto &item : queue) {
        if (item.id == currentSongId) {
            return getMetadataForTrack(item);
        }
    }

    // Fallback: Match by current title/artist/album
    QString currentTitle = m_mpdClient->title();
    QString currentArtist = m_mpdClient->artist();
    QString currentAlbum = m_mpdClient->album();

    for (const auto &item : queue) {
        if (item.title == currentTitle && item.artist == currentArtist && item.album == currentAlbum) {
            return getMetadataForTrack(item);
        }
    }

    return {};
}

auto DBusService::volume() const -> double
{
    return m_mpdClient ? m_mpdClient->volume() / static_cast<double>(VOLUME_SCALE) : 0.0;
}

void DBusService::setVolume(double volume)
{
    if (m_mpdClient)
        m_mpdClient->setVolume(static_cast<int>(volume * VOLUME_SCALE));
}

auto DBusService::position() const -> qlonglong
{
    return m_mpdClient ? m_mpdClient->elapsed() * MICROSECONDS_PER_SECOND : 0;
}

auto DBusService::playbackStatus() const -> QString
{
    if (!m_mpdClient)
        return "Stopped";

    QString state = m_mpdClient->state();
    if (state == "play")
        return "Playing";
    else if (state == "pause")
        return "Paused";
    else
        return "Stopped";
}

void DBusService::setShuffle(bool shuffle)
{
    if (m_mpdClient)
        m_mpdClient->setRandom(shuffle);
}

auto DBusService::loopStatus() const -> QString
{
    if (!m_mpdClient)
        return "None";

    if (m_mpdClient->single())
        return "Track";
    else if (m_mpdClient->repeat())
        return "Playlist";
    else
        return "None";
}

void DBusService::setLoopStatus(const QString &status)
{
    if (m_mpdClient) {
        if (status == "None") {
            m_mpdClient->setRepeat(false);
            m_mpdClient->setSingle(false);
        } else if (status == "Track") {
            m_mpdClient->setRepeat(false);
            m_mpdClient->setSingle(true);
        } else if (status == "Playlist") {
            m_mpdClient->setRepeat(true);
            m_mpdClient->setSingle(false);
        }
    }
}

void DBusService::quit()
{
    QCoreApplication::quit();
}

void DBusService::raise()
{
    if (m_mpdClient && m_mpdClient->window())
        m_mpdClient->window()->show();
}

void DBusService::next()
{
    if (m_mpdClient)
        m_mpdClient->next();
}

void DBusService::previous()
{
    if (m_mpdClient)
        m_mpdClient->previous();
}

void DBusService::pause()
{
    if (m_mpdClient)
        m_mpdClient->pause();
}

void DBusService::playPause()
{
    if (m_mpdClient) {
        if (m_mpdClient->state() == "play") {
            m_mpdClient->pause();
        } else {
            m_mpdClient->play();
        }
    }
}

void DBusService::stop()
{
    if (m_mpdClient)
        m_mpdClient->stop(); // MPD has a stop command
}

void DBusService::play()
{
    if (m_mpdClient)
        m_mpdClient->play();
}

void DBusService::setRate(double rate)
{
    Q_UNUSED(rate);
    // Not supported
}

void DBusService::seek(double offset)
{
    if (!m_mpdClient) return;

    QVariantMap meta = metadata();
    qlonglong duration = 0;
    if (meta.contains("mpris:length")) {
        duration = meta["mpris:length"].toLongLong();
    }

    qlonglong currentPosition = position();
    qlonglong newPosition = currentPosition + static_cast<qlonglong>(offset);

    if (newPosition < 0) newPosition = 0;

    if (duration > 0 && newPosition >= duration) {
        next();
        return;
    }

    double newPosSec = static_cast<double>(newPosition) / MICROSECONDS_PER_SECOND;
    m_mpdClient->seekTo(newPosSec);
    emit seeked(newPosition);
}

void DBusService::setPosition(const QString &trackId, double position)
{
    if (!m_mpdClient) return;

    QVariantMap meta = metadata();
    if (meta.contains("mpris:trackid")) {
        QDBusObjectPath currentTrackId = meta["mpris:trackid"].value<QDBusObjectPath>();
        if (currentTrackId.path() != trackId) {
            return;
        }
    }

    auto newPosition = static_cast<qlonglong>(position);
    if (newPosition < 0) newPosition = 0;

    qlonglong duration = 0;
    if (meta.contains("mpris:length")) {
        duration = meta["mpris:length"].toLongLong();
    }
    if (duration > 0 && newPosition > duration) {
        newPosition = duration;
    }

    double newPosSec = static_cast<double>(newPosition) / MICROSECONDS_PER_SECOND;
    m_mpdClient->seekTo(newPosSec);
    emit seeked(newPosition);
}

void DBusService::openUri(const QString &uri)
{
    if (m_mpdClient)
        m_mpdClient->playTrack(uri);
}

// Track List interface implementation
auto DBusService::tracks() const -> QList<QDBusObjectPath>
{
    QList<QDBusObjectPath> trackList;
    if (!m_mpdClient)
        return trackList;

    // Get the current queue from MPD
    QList<QueueItem> queue = m_mpdClient->queueModel()->m_queue;
    
    for (const auto &item : queue) {
        QString trackId = createTrackId(item.uri);
        trackList.append(QDBusObjectPath(trackId));
    }
    
    return trackList;
}

auto DBusService::getTracksMetadata(const QList<QDBusObjectPath> &trackIds) const -> QList<QVariantMap>
{
    QList<QVariantMap> result;
    if (!m_mpdClient)
        return result;

    for (const auto &trackId : trackIds) {
        int position = trackIdToPosition(trackId.path());
        if (position != -1) {
            const auto &item = m_mpdClient->queueModel()->m_queue.at(position);
            result.append(getMetadataForTrack(item));
        } else {
            result.append(QVariantMap());
        }
    }
    return result;
}

void DBusService::addTrack(const QString &uri, const QDBusObjectPath &afterTrack, bool setAsCurrent)
{
    if (!m_mpdClient)
        return;

    // Add track to MPD queue
    m_mpdClient->addTrack(uri);
    
    QString trackId = createTrackId(uri);
    QDBusObjectPath newTrack(trackId);
    
    // Emit TrackAdded signal
    emit TrackAdded(newTrack, afterTrack);
    
    // Refresh the queue to get updated track list
    m_mpdClient->refreshQueue();
    
    // If setAsCurrent is true, play the track
    if (setAsCurrent) {
        // Find the newly added track and play it
        QList<QueueItem> queue = m_mpdClient->queueModel()->m_queue;
        for (const auto &item : queue) {
            if (item.uri == uri) {
                m_mpdClient->playQueueId(item.id);
                break;
            }
        }
    }
}

void DBusService::removeTrack(const QDBusObjectPath &trackId)
{
    if (!m_mpdClient)
        return;

    QString uri = uriFromTrackId(trackId.path());
    if (!uri.isEmpty()) {
        // Find the track in the queue and remove it
        QList<QueueItem> queue = m_mpdClient->queueModel()->m_queue;
        for (const auto &item : queue) {
            if (item.uri == uri) {
                m_mpdClient->removeId(item.id);
                emit TrackRemoved(trackId);
                m_trackIdToUri.remove(trackId.path());
                m_uriToTrackId.remove(uri);
                m_mpdClient->refreshQueue();
                break;
            }
        }
    }
}

void DBusService::goNext()
{
    next();
}

void DBusService::goPrevious()
{
    previous();
}

void DBusService::goTo(const QDBusObjectPath &trackId)
{
    QString uri = uriFromTrackId(trackId.path());
    if (!uri.isEmpty()) {
        m_mpdClient->playTrack(uri);
    }
}

auto DBusService::createTrackId(const QString &uri) const -> QString
{
    if (m_uriToTrackId.contains(uri))
        return m_uriToTrackId.value(uri);

    // Create a unique track ID based on the URI
    QString hash = QString::number(qHash(uri));
    QString trackId = "/org/mpris/MediaPlayer2/Track/" + hash;

    m_trackIdToUri.insert(trackId, uri);
    m_uriToTrackId.insert(uri, trackId);

    return trackId;
}

auto DBusService::uriFromTrackId(const QString &trackId) const -> QString
{
    if (!m_trackIdToUri.contains(trackId))
        return {};

    return m_trackIdToUri.value(trackId);
}

auto DBusService::trackIdToPosition(const QString &trackId) const -> int
{
    QString uri = uriFromTrackId(trackId);
    if (uri.isEmpty())
        return -1;

    QList<QueueItem> queue = m_mpdClient->queueModel()->m_queue;
    for (int i = 0; i < queue.size(); ++i) {
        if (queue[i].uri == uri) {
            return i;
        }
    }
    return -1;
}

auto DBusService::positionToTrackId(int position) const -> QString
{
    if (!m_mpdClient || position < 0)
        return {};
    
    QList<QueueItem> queue = m_mpdClient->queueModel()->m_queue;
    if (position >= queue.size())
        return {};

    QString uri = queue[position].uri;
    if (m_uriToTrackId.contains(uri))
        return m_uriToTrackId.value(uri);

    return {};
}

auto DBusService::playlistCount() const -> quint32
{
    return m_mpdClient ? m_mpdClient->playlists().count() : 0;
}

auto DBusService::orderings() const -> QStringList
{
    return {QStringLiteral("Alphabetical")};
}

auto DBusService::activePlaylist() const -> MprisActivePlaylist
{
    // MPD doesn't have a concept of a persistent "active" playlist object,
    // it just has the current queue.
    return {.valid=false, .playlist={.id=QDBusObjectPath("/"), .name="", .iconUri=""}};
}

void DBusService::activatePlaylist(const QDBusObjectPath &playlistId)
{
    if (!m_mpdClient) return;
    QString name = decodePlaylistId(playlistId);
    if (!name.isEmpty()) {
        m_mpdClient->clearQueue();
        m_mpdClient->loadPlaylist(name);
        m_mpdClient->play();
    }
}

auto DBusService::getPlaylists(quint32 index, quint32 maxCount, const QString &order, bool reverseOrder) -> QList<MprisPlaylist>
{
    if (!m_mpdClient) return {};

    QStringList playlists = m_mpdClient->playlists();
    if (order == "Alphabetical") {
        playlists.sort(Qt::CaseInsensitive);
    }
    if (reverseOrder) {
        std::ranges::reverse(playlists);
    }

    if (std::cmp_greater_equal(index ,playlists.size())) return {};

    int count = (maxCount == 0) ? static_cast<int>(playlists.size() - index) : std::min(static_cast<int>(maxCount), static_cast<int>(playlists.size() - index));
    
    QList<MprisPlaylist> result;
    for (int i = 0; i < count; ++i) {
        QString name = playlists[index + i];
        result.append({.id=QDBusObjectPath(encodePlaylistId(name)), .name=name, .iconUri=""});
    }
    return result;
}

void DBusService::broadcastProperties()
{
    if (!m_mpdClient) {
        return;
    }

    // KISS: Only broadcast properties that actually change infrequently.
    // Per MPRIS spec, Position should NOT be broadcasted as it changes continuously.
    QVariantMap changedProperties;
    changedProperties["Metadata"] = metadata();
    changedProperties["PlaybackStatus"] = playbackStatus();
    changedProperties["Volume"] = volume();
    changedProperties["LoopStatus"] = loopStatus();
    changedProperties["Shuffle"] = shuffle();

    // Manually emit org.freedesktop.DBus.Properties.PropertiesChanged
    QDBusMessage msg = QDBusMessage::createSignal(
        "/org/mpris/MediaPlayer2",
        "org.freedesktop.DBus.Properties",
        "PropertiesChanged");
    msg << "org.mpris.MediaPlayer2.Player" << changedProperties << QStringList();
    m_connection.send(msg);
}

// --- MprisRootAdaptor Implementation ---

MprisRootAdaptor::MprisRootAdaptor(DBusService *parent) : QDBusAbstractAdaptor(parent), m_service(parent) { setAutoRelaySignals(true); }
auto MprisRootAdaptor::canQuit() const -> bool { return m_service->canQuit(); }
auto MprisRootAdaptor::fullscreen() const -> bool { return m_service->fullscreen(); }
auto MprisRootAdaptor::canSetFullscreen() const -> bool { return m_service->canSetFullscreen(); }
auto MprisRootAdaptor::canRaise() const -> bool { return m_service->canRaise(); }
auto MprisRootAdaptor::hasTrackList() const -> bool { return true; }
auto MprisRootAdaptor::identity() const -> QString { return m_service->identity(); }
auto MprisRootAdaptor::desktopEntry() const -> QString { return "quester"; }
auto MprisRootAdaptor::supportedUriSchemes() const -> QStringList { return {"file"}; }
auto MprisRootAdaptor::supportedMimeTypes() const -> QStringList { return {"audio/mpeg", "audio/ogg", "audio/flac", "audio/wav"}; }
void MprisRootAdaptor::Quit() { m_service->quit(); }
void MprisRootAdaptor::Raise() { m_service->raise(); }

// --- MprisPlayerAdaptor Implementation ---

MprisPlayerAdaptor::MprisPlayerAdaptor(DBusService *parent) : QDBusAbstractAdaptor(parent), m_service(parent) {
    setAutoRelaySignals(true);
    connect(parent, &DBusService::seeked, this, &MprisPlayerAdaptor::Seeked);
}
auto MprisPlayerAdaptor::canGoNext() const -> bool { return m_service->canGoNext(); }
auto MprisPlayerAdaptor::canGoPrevious() const -> bool { return m_service->canGoPrevious(); }
auto MprisPlayerAdaptor::canPlay() const -> bool { return m_service->canPlay(); }
auto MprisPlayerAdaptor::canPause() const -> bool { return m_service->canPause(); }
auto MprisPlayerAdaptor::canSeek() const -> bool { return m_service->canSeek(); }
auto MprisPlayerAdaptor::canControl() const -> bool { return m_service->canControl(); }
auto MprisPlayerAdaptor::rate() const -> double { return m_service->rate(); }
void MprisPlayerAdaptor::setRate(double rate) { m_service->setRate(rate); }
auto MprisPlayerAdaptor::minimumRate() const -> double { return m_service->minimumRate(); }
auto MprisPlayerAdaptor::maximumRate() const -> double { return m_service->maximumRate(); }
auto MprisPlayerAdaptor::shuffle() const -> bool { return m_service->shuffle(); }
void MprisPlayerAdaptor::setShuffle(bool shuffle) { m_service->setShuffle(shuffle); }
auto MprisPlayerAdaptor::loopStatus() const -> QString { return m_service->loopStatus(); }
void MprisPlayerAdaptor::setLoopStatus(const QString &status) { m_service->setLoopStatus(status); }
auto MprisPlayerAdaptor::metadata() const -> QVariantMap { return m_service->metadata(); }
auto MprisPlayerAdaptor::volume() const -> double { return m_service->volume(); }
void MprisPlayerAdaptor::setVolume(double volume) { m_service->setVolume(volume); }
auto MprisPlayerAdaptor::position() const -> qlonglong { return m_service->position(); }
auto MprisPlayerAdaptor::playbackStatus() const -> QString { return m_service->playbackStatus(); }

void MprisPlayerAdaptor::Next() { m_service->next(); }
void MprisPlayerAdaptor::Previous() { m_service->previous(); }
void MprisPlayerAdaptor::Pause() { m_service->pause(); }
void MprisPlayerAdaptor::PlayPause() { m_service->playPause(); }
void MprisPlayerAdaptor::Stop() { m_service->stop(); }
void MprisPlayerAdaptor::Play() { m_service->play(); }
void MprisPlayerAdaptor::Seek(qlonglong offset) { m_service->seek(static_cast<double>(offset)); }
void MprisPlayerAdaptor::SetPosition(const QDBusObjectPath &trackId, qlonglong position) { m_service->setPosition(trackId.path(), static_cast<double>(position)); }
void MprisPlayerAdaptor::OpenUri(const QString &uri) { m_service->openUri(uri); }

// --- MprisTrackListAdaptor Implementation ---

MprisTrackListAdaptor::MprisTrackListAdaptor(DBusService *parent) : QDBusAbstractAdaptor(parent), m_service(parent) {
    connect(parent, &DBusService::TrackListReplaced, this, &MprisTrackListAdaptor::TrackListReplaced);
    connect(parent, &DBusService::TrackAdded, this, &MprisTrackListAdaptor::TrackAdded);
    connect(parent, &DBusService::TrackRemoved, this, &MprisTrackListAdaptor::TrackRemoved);
    connect(parent, &DBusService::TrackMetadataChanged, this, &MprisTrackListAdaptor::TrackMetadataChanged);
}

auto MprisTrackListAdaptor::tracks() const -> QList<QDBusObjectPath> { 
    return m_service->tracks();
}

auto MprisTrackListAdaptor::canEditTracks() const -> bool { return m_service->canEditTracks(); }

auto MprisTrackListAdaptor::GetTracksMetadata(const QList<QDBusObjectPath> &trackIds) -> QList<QVariantMap> {
    return m_service->getTracksMetadata(trackIds);
}

void MprisTrackListAdaptor::AddTrack(const QString &uri, const QDBusObjectPath &afterTrack, bool setAsCurrent) { 
    m_service->addTrack(uri, afterTrack, setAsCurrent); 
}

void MprisTrackListAdaptor::RemoveTrack(const QDBusObjectPath &trackId) { 
    m_service->removeTrack(trackId); 
}

void MprisTrackListAdaptor::GoTo(const QDBusObjectPath &trackId) { 
    m_service->goTo(trackId); 
}

void MprisTrackListAdaptor::GoNext() { m_service->goNext(); }
void MprisTrackListAdaptor::GoPrevious() { m_service->goPrevious(); }

// --- MprisPlaylistsAdaptor Implementation ---

MprisPlaylistsAdaptor::MprisPlaylistsAdaptor(DBusService *parent) : QDBusAbstractAdaptor(parent), m_service(parent) {
    setAutoRelaySignals(true);
}

auto MprisPlaylistsAdaptor::playlistCount() const -> quint32 { return m_service->playlistCount(); }
auto MprisPlaylistsAdaptor::orderings() const -> QStringList { return m_service->orderings(); }
auto MprisPlaylistsAdaptor::activePlaylist() const -> MprisActivePlaylist { return m_service->activePlaylist(); }

void MprisPlaylistsAdaptor::ActivatePlaylist(const QDBusObjectPath &PlaylistId) {
    m_service->activatePlaylist(PlaylistId);
}

auto MprisPlaylistsAdaptor::GetPlaylists(quint32 Index, quint32 MaxCount, const QString &Order, bool ReverseOrder) -> QList<MprisPlaylist> {
    return m_service->getPlaylists(Index, MaxCount, Order, ReverseOrder);
}