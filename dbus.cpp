#include "dbus.h"
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusVariant>
#include <QDBusObjectPath>
#include <QTimer>

QDBusArgument &operator<<(QDBusArgument &argument, const MprisPlaylist &playlist) {
    argument.beginStructure();
    argument << playlist.id << playlist.name << playlist.iconUri;
    argument.endStructure();
    return argument;
}

const QDBusArgument &operator>>(const QDBusArgument &argument, MprisPlaylist &playlist) {
    argument.beginStructure();
    argument >> playlist.id >> playlist.name >> playlist.iconUri;
    argument.endStructure();
    return argument;
}

QDBusArgument &operator<<(QDBusArgument &argument, const MprisActivePlaylist &ap) {
    argument.beginStructure();
    argument << ap.valid << ap.playlist;
    argument.endStructure();
    return argument;
}

const QDBusArgument &operator>>(const QDBusArgument &argument, MprisActivePlaylist &ap) {
    argument.beginStructure();
    argument >> ap.valid >> ap.playlist;
    argument.endStructure();
    return argument;
}

static QString encodePlaylistId(const QString &name) {
    return QStringLiteral("/org/mpris/MediaPlayer2/Playlists/") + QString::fromLatin1(name.toUtf8().toHex());
}

static QString decodePlaylistId(const QDBusObjectPath &path) {
    QString p = path.path();
    if (p.startsWith("/org/mpris/MediaPlayer2/Playlists/")) {
        QString hex = p.mid(34);
        return QString::fromUtf8(QByteArray::fromHex(hex.toLatin1()));
    }
    return QString();
}

DBusService::DBusService(MpdClient *mpdClient, QObject *parent)
    : QObject(parent)
    , m_mpdClient(mpdClient)
    , m_positionTimer(new QTimer(this))
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

    // Try to register the service with a unique name to avoid conflicts
    QString serviceName = QString("org.mpris.MediaPlayer2.quester.%1").arg(QCoreApplication::applicationPid());
    if (!m_connection.registerService(serviceName)) {
        qWarning() << "Failed to register MPRIS service on D-Bus:" << m_connection.lastError().message();
        // Try the standard name as fallback
        if (!m_connection.registerService("org.mpris.MediaPlayer2.quester")) {
            qWarning() << "Failed to register MPRIS service with standard name:" << m_connection.lastError().message();
        } else {
            qDebug() << "Successfully registered MPRIS service org.mpris.MediaPlayer2.quester";
        }
    } else {
        qDebug() << "Successfully registered MPRIS service" << serviceName;
    }

    // Connect to MPD client signals
    if (mpdClient) {
        connect(mpdClient, &MpdClient::stateChanged, this, &DBusService::updatePosition);
        connect(mpdClient, &MpdClient::volumeChanged, this, &DBusService::updatePosition);
        connect(mpdClient, &MpdClient::artistChanged, this, &DBusService::updatePosition);
        connect(mpdClient, &MpdClient::titleChanged, this, &DBusService::updatePosition);
        connect(mpdClient, &MpdClient::albumChanged, this, &DBusService::updatePosition);
        connect(mpdClient, &MpdClient::durationChanged, this, &DBusService::updatePosition);
        connect(mpdClient, &MpdClient::elapsedChanged, this, &DBusService::updatePosition);
        connect(mpdClient, &MpdClient::repeatChanged, this, &DBusService::updatePosition);
        connect(mpdClient, &MpdClient::randomChanged, this, &DBusService::updatePosition);
        connect(mpdClient, &MpdClient::singleChanged, this, &DBusService::updatePosition);
        mpdClient->refreshPlaylists();
    }

    // Update position periodically
    m_positionTimer->setInterval(500); // Update more frequently for better responsiveness
    connect(m_positionTimer, &QTimer::timeout, this, &DBusService::updatePosition);
    m_positionTimer->start();
}

DBusService::~DBusService()
{
    m_positionTimer->stop();
}

bool DBusService::canGoNext() const
{
    return mpdClient() && mpdClient()->state() != "stop";
}

bool DBusService::canGoPrevious() const
{
    return mpdClient() && mpdClient()->state() != "stop";
}

bool DBusService::canPlay() const
{
    return mpdClient() && mpdClient()->state() != "play";
}

bool DBusService::canPause() const
{
    return mpdClient() && mpdClient()->state() == "play";
}

QVariantMap DBusService::metadata() const
{
    QVariantMap metadata;
    if (!mpdClient())
        return metadata;

    QString uri = mpdClient()->uri();
    QString title = mpdClient()->title();
    QString artist = mpdClient()->artist();
    QString album = mpdClient()->album();
    qint64 duration = mpdClient()->duration();

    // Only include metadata if we have a valid track playing
    if (uri.isEmpty() && title.isEmpty() && artist.isEmpty()) {
        return metadata;
    }

    // Create a proper track ID for MPRIS
    QString trackId = "/org/mpris/MediaPlayer2/Track/" + QString::number(qHash(uri));
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
        metadata["mpris:length"] = static_cast<quint64>(duration * 1000000);
    }

    // Add album art if available
    QString albumArtPath = mpdClient()->albumArt();
    if (!albumArtPath.isEmpty()) {
        metadata["mpris:artUrl"] = albumArtPath;
    }

    // Add URI
    if (!uri.isEmpty()) {
        metadata["xesam:url"] = uri;
    }

    // Add additional metadata fields according to MPRIS specification
    // Note: These would need to be implemented in the MpdClient class
    // if the MPD server provides this information
    
    // Track number (if available)
    // metadata["xesam:trackNumber"] = trackNumber;
    
    // Album artist (if different from artist)
    // metadata["xesam:albumArtist"] = albumArtist;
    
    // Genre
    // metadata["xesam:genre"] = genreList;
    
    // Disc number
    // metadata["xesam:discNumber"] = discNumber;
    
    // Date
    // metadata["xesam:date"] = date;
    
    // Comment
    // metadata["xesam:comment"] = comment;
    
    // User rating
    // metadata["xesam:userRating"] = userRating;

    return metadata;
}

double DBusService::volume() const
{
    return mpdClient() ? mpdClient()->volume() / 100.0 : 0.0;
}

void DBusService::setVolume(double volume)
{
    if (mpdClient())
        mpdClient()->setVolume(static_cast<int>(volume * 100));
}

qlonglong DBusService::position() const
{
    return mpdClient() ? mpdClient()->elapsed() * 1000000 : 0;
}

QString DBusService::playbackStatus() const
{
    if (!mpdClient())
        return "Stopped";

    QString state = mpdClient()->state();
    if (state == "play")
        return "Playing";
    else if (state == "pause")
        return "Paused";
    else
        return "Stopped";
}

void DBusService::setShuffle(bool shuffle)
{
    if (mpdClient())
        mpdClient()->setRandom(shuffle);
}

QString DBusService::loopStatus() const
{
    if (!mpdClient())
        return "None";

    if (mpdClient()->single())
        return "Track";
    else if (mpdClient()->repeat())
        return "Playlist";
    else
        return "None";
}

void DBusService::setLoopStatus(const QString &status)
{
    if (mpdClient()) {
        if (status == "None") {
            mpdClient()->setRepeat(false);
            mpdClient()->setSingle(false);
        } else if (status == "Track") {
            mpdClient()->setRepeat(false);
            mpdClient()->setSingle(true);
        } else if (status == "Playlist") {
            mpdClient()->setRepeat(true);
            mpdClient()->setSingle(false);
        }
    }
}

void DBusService::quit()
{
    QCoreApplication::quit();
}

void DBusService::raise()
{
    if (mpdClient() && mpdClient()->window())
        mpdClient()->window()->show();
}

void DBusService::next()
{
    if (mpdClient())
        mpdClient()->next();
}

void DBusService::previous()
{
    if (mpdClient())
        mpdClient()->previous();
}

void DBusService::pause()
{
    if (mpdClient())
        mpdClient()->pause();
}

void DBusService::playPause()
{
    if (mpdClient())
        mpdClient()->togglePlayPause();
}

void DBusService::stop()
{
    if (mpdClient())
        mpdClient()->stop(); // MPD has a stop command
}

void DBusService::play()
{
    if (mpdClient())
        mpdClient()->play();
}

void DBusService::setRate(double rate)
{
    Q_UNUSED(rate);
    // Not supported
}

void DBusService::seek(double offset)
{
    if (mpdClient()) {
        double offset_sec = offset / 1000000.0;
        double current_pos_sec = mpdClient()->elapsed();
        double new_pos_sec = current_pos_sec + offset_sec;

        // Seek to the new absolute position, not by the offset
        mpdClient()->seekTo(static_cast<qint64>(new_pos_sec));
        emit seeked(static_cast<qlonglong>(new_pos_sec * 1000000.0));
    }
}

void DBusService::setPosition(const QString &trackId, double position)
{
    Q_UNUSED(trackId);
    if (mpdClient()) {
        mpdClient()->seekTo(static_cast<qint64>(position / 1000000.0));
        emit seeked(static_cast<qlonglong>(position));
    }
}

void DBusService::openUri(const QString &uri)
{
    if (mpdClient())
        mpdClient()->playTrack(uri);
}

// Track List interface implementation
QList<QDBusObjectPath> DBusService::tracks() const
{
    QList<QDBusObjectPath> trackList;
    if (!mpdClient())
        return trackList;

    // Get the current queue from MPD
    QList<QueueItem> queue = mpdClient()->queueModel()->m_queue;
    
    for (const auto &item : queue) {
        QString trackId = createTrackId(item.uri);
        trackList.append(QDBusObjectPath(trackId));
    }
    
    return trackList;
}

void DBusService::addTrack(const QString &uri, const QDBusObjectPath &afterTrack, bool setAsCurrent)
{
    if (!mpdClient())
        return;

    // Add track to MPD queue
    mpdClient()->addTrack(uri);
    
    QString trackId = createTrackId(uri);
    QDBusObjectPath newTrack(trackId);
    
    // Emit TrackAdded signal
    emit TrackAdded(newTrack, afterTrack);
    
    // Refresh the queue to get updated track list
    mpdClient()->refreshQueue();
    
    // If setAsCurrent is true, play the track
    if (setAsCurrent) {
        // Find the newly added track and play it
        QList<QueueItem> queue = mpdClient()->queueModel()->m_queue;
        for (const auto &item : queue) {
            if (item.uri == uri) {
                mpdClient()->playQueueId(item.id);
                break;
            }
        }
    }
}

void DBusService::removeTrack(const QDBusObjectPath &trackId)
{
    if (!mpdClient())
        return;

    QString uri = uriFromTrackId(trackId.path());
    if (!uri.isEmpty()) {
        // Find the track in the queue and remove it
        QList<QueueItem> queue = mpdClient()->queueModel()->m_queue;
        for (const auto &item : queue) {
            if (item.uri == uri) {
                mpdClient()->removeId(item.id);
                emit TrackRemoved(trackId);
                mpdClient()->refreshQueue();
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

QString DBusService::createTrackId(const QString &uri) const
{
    // Create a unique track ID based on the URI
    QString hash = QString::number(qHash(uri));
    return "/org/mpris/MediaPlayer2/Track/" + hash;
}

QString DBusService::uriFromTrackId(const QString &trackId) const
{
    // Extract URI from track ID - this is a simplified implementation
    // In a real implementation, you'd need to maintain a mapping
    Q_UNUSED(trackId);
    return QString();
}

int DBusService::trackIdToPosition(const QString &trackId) const
{
    // Convert track ID to position in queue
    Q_UNUSED(trackId);
    return -1;
}

QString DBusService::positionToTrackId(int position) const
{
    // Convert position to track ID
    Q_UNUSED(position);
    return QString();
}

quint32 DBusService::playlistCount() const
{
    return mpdClient() ? mpdClient()->playlists().count() : 0;
}

QStringList DBusService::orderings() const
{
    return {QStringLiteral("Alphabetical")};
}

MprisActivePlaylist DBusService::activePlaylist() const
{
    // MPD doesn't have a concept of a persistent "active" playlist object,
    // it just has the current queue.
    return {false, {QDBusObjectPath("/"), "", ""}};
}

void DBusService::activatePlaylist(const QDBusObjectPath &playlistId)
{
    if (!mpdClient()) return;
    QString name = decodePlaylistId(playlistId);
    if (!name.isEmpty()) {
        mpdClient()->clearQueue();
        mpdClient()->loadPlaylist(name);
        mpdClient()->play();
    }
}

QList<MprisPlaylist> DBusService::getPlaylists(quint32 index, quint32 maxCount, const QString &order, bool reverseOrder)
{
    if (!mpdClient()) return {};

    QStringList playlists = mpdClient()->playlists();
    if (order == "Alphabetical") {
        playlists.sort(Qt::CaseInsensitive);
    }
    if (reverseOrder) {
        std::reverse(playlists.begin(), playlists.end());
    }

    if (index >= static_cast<quint32>(playlists.size())) return {};

    int count = (maxCount == 0) ? (playlists.size() - index) : std::min((int)maxCount, static_cast<int>(playlists.size() - index));
    
    QList<MprisPlaylist> result;
    for (int i = 0; i < count; ++i) {
        QString name = playlists[index + i];
        result.append({QDBusObjectPath(encodePlaylistId(name)), name, ""});
    }
    return result;
}

void DBusService::updatePosition()
{
    if (!mpdClient()) {
        return;
    }

    QVariantMap changedProperties;
    changedProperties["Metadata"] = metadata();
    changedProperties["PlaybackStatus"] = playbackStatus();
    changedProperties["Position"] = position();
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
bool MprisRootAdaptor::canQuit() const { return m_service->canQuit(); }
bool MprisRootAdaptor::canSetFullscreen() const { return m_service->canSetFullscreen(); }
bool MprisRootAdaptor::fullscreen() const { return m_service->fullscreen(); }
bool MprisRootAdaptor::canRaise() const { return m_service->canRaise(); }
QString MprisRootAdaptor::identity() const { return m_service->identity(); }
QStringList MprisRootAdaptor::supportedUriSchemes() const { return m_service->supportedUriSchemes(); }
QStringList MprisRootAdaptor::supportedMimeTypes() const { return m_service->supportedMimeTypes(); }
void MprisRootAdaptor::Quit() { m_service->quit(); }
void MprisRootAdaptor::Raise() { m_service->raise(); }

// --- MprisPlayerAdaptor Implementation ---

MprisPlayerAdaptor::MprisPlayerAdaptor(DBusService *parent) : QDBusAbstractAdaptor(parent), m_service(parent) {
    setAutoRelaySignals(true);
    connect(parent, &DBusService::seeked, this, &MprisPlayerAdaptor::Seeked);
}
bool MprisPlayerAdaptor::canGoNext() const { return m_service->canGoNext(); }
bool MprisPlayerAdaptor::canGoPrevious() const { return m_service->canGoPrevious(); }
bool MprisPlayerAdaptor::canPlay() const { return m_service->canPlay(); }
bool MprisPlayerAdaptor::canPause() const { return m_service->canPause(); }
bool MprisPlayerAdaptor::canSeek() const { return m_service->canSeek(); }
bool MprisPlayerAdaptor::canControl() const { return m_service->canControl(); }
double MprisPlayerAdaptor::rate() const { return m_service->rate(); }
void MprisPlayerAdaptor::setRate(double rate) { m_service->setRate(rate); }
double MprisPlayerAdaptor::minimumRate() const { return m_service->minimumRate(); }
double MprisPlayerAdaptor::maximumRate() const { return m_service->maximumRate(); }
bool MprisPlayerAdaptor::shuffle() const { return m_service->shuffle(); }
void MprisPlayerAdaptor::setShuffle(bool shuffle) { m_service->setShuffle(shuffle); }
QString MprisPlayerAdaptor::loopStatus() const { return m_service->loopStatus(); }
void MprisPlayerAdaptor::setLoopStatus(const QString &status) { m_service->setLoopStatus(status); }
QVariantMap MprisPlayerAdaptor::metadata() const { return m_service->metadata(); }
double MprisPlayerAdaptor::volume() const { return m_service->volume(); }
void MprisPlayerAdaptor::setVolume(double volume) { m_service->setVolume(volume); }
qlonglong MprisPlayerAdaptor::position() const { return m_service->position(); }
QString MprisPlayerAdaptor::playbackStatus() const { return m_service->playbackStatus(); }

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
    setAutoRelaySignals(true);
    connect(parent, &DBusService::TrackListReplaced, this, &MprisTrackListAdaptor::TrackListReplaced);
    connect(parent, &DBusService::TrackAdded, this, &MprisTrackListAdaptor::TrackAdded);
    connect(parent, &DBusService::TrackRemoved, this, &MprisTrackListAdaptor::TrackRemoved);
    connect(parent, &DBusService::TrackMetadataChanged, this, &MprisTrackListAdaptor::TrackMetadataChanged);
}

QList<QDBusObjectPath> MprisTrackListAdaptor::tracks() const { 
    return m_service->tracks();
}

bool MprisTrackListAdaptor::canEditTracks() const { return m_service->canEditTracks(); }

QList<QVariantMap> MprisTrackListAdaptor::GetTracksMetadata(const QList<QDBusObjectPath> &trackIds) {
    Q_UNUSED(trackIds);
    // Simplified implementation returning empty list
    return QList<QVariantMap>();
}

void MprisTrackListAdaptor::AddTrack(const QString &uri, const QDBusObjectPath &afterTrack, bool setAsCurrent) { 
    m_service->addTrack(uri, afterTrack, setAsCurrent); 
}

void MprisTrackListAdaptor::RemoveTrack(const QDBusObjectPath &trackId) { 
    m_service->removeTrack(trackId); 
}

void MprisTrackListAdaptor::GoNext() { m_service->goNext(); }
void MprisTrackListAdaptor::GoPrevious() { m_service->goPrevious(); }

// --- MprisPlaylistsAdaptor Implementation ---

MprisPlaylistsAdaptor::MprisPlaylistsAdaptor(DBusService *parent) : QDBusAbstractAdaptor(parent), m_service(parent) {
    setAutoRelaySignals(true);
}

quint32 MprisPlaylistsAdaptor::playlistCount() const { return m_service->playlistCount(); }
QStringList MprisPlaylistsAdaptor::orderings() const { return m_service->orderings(); }
MprisActivePlaylist MprisPlaylistsAdaptor::activePlaylist() const { return m_service->activePlaylist(); }

void MprisPlaylistsAdaptor::ActivatePlaylist(const QDBusObjectPath &PlaylistId) {
    m_service->activatePlaylist(PlaylistId);
}

QList<MprisPlaylist> MprisPlaylistsAdaptor::GetPlaylists(quint32 Index, quint32 MaxCount, const QString &Order, bool ReverseOrder) {
    return m_service->getPlaylists(Index, MaxCount, Order, ReverseOrder);
}
