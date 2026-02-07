#include "dbus.h"
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusVariant>
#include <QTimer>

DBusService::DBusService(MpdClient *mpdClient, QObject *parent)
    : QObject(parent)
    , m_mpdClient(mpdClient)
    , m_positionTimer(new QTimer(this))
    , m_connection(QDBusConnection::sessionBus())
{
    // Register the MPRIS interface
    m_connection.registerObject("/", this, QDBusConnection::ExportAllContents);
    m_connection.registerService("org.mpris.MediaPlayer2.quester");

    // Connect to MPD client signals
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

    // Update position periodically
    m_positionTimer->setInterval(1000);
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

    metadata["mpris:trackid"] = mpdClient()->uri();
    metadata["xesam:title"] = mpdClient()->title();
    metadata["xesam:artist"] = QVariantList() << mpdClient()->artist();
    metadata["xesam:album"] = mpdClient()->album();
    metadata["mpris:length"] = static_cast<quint64>(mpdClient()->duration() * 1000000);

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

        mpdClient()->seek(static_cast<qint64>(offset_sec));
        emit Seeked(static_cast<qlonglong>(new_pos_sec * 1000000.0));
    }
}

void DBusService::setPosition(const QString &trackId, double position)
{
    Q_UNUSED(trackId);
    if (mpdClient()) {
        mpdClient()->seekTo(static_cast<qint64>(position / 1000000.0));
        emit Seeked(static_cast<qlonglong>(position));
    }
}

void DBusService::openUri(const QString &uri)
{
    if (mpdClient())
        mpdClient()->playTrack(uri);
}

void DBusService::updatePosition()
{
    QVariantMap changedProperties;
    changedProperties["Metadata"] = metadata();
    changedProperties["PlaybackStatus"] = playbackStatus();
    changedProperties["Position"] = position();
    changedProperties["Volume"] = volume();
    changedProperties["LoopStatus"] = loopStatus();
    changedProperties["Shuffle"] = shuffle();

    emit propertiesChanged("org.mpris.MediaPlayer2.Player",
                          changedProperties,
                          QStringList());
}