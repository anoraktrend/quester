#include "mprisclient.h"
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusVariant>
#include <QTimer>

MprisClient::MprisClient(MpdClient *mpdClient, QObject *parent)
    : QObject(parent)
    , m_mpdClient(mpdClient)
    , m_positionTimer(new QTimer(this))
{
    // Register the MPRIS interface
    QDBusConnection connection = QDBusConnection::sessionBus();
    connection.registerObject("/", this, QDBusConnection::ExportAllContents);
    connection.registerService("org.mpris.MediaPlayer2.quester");

    // Connect to MPD client signals
    connect(mpdClient, &MpdClient::stateChanged, this, &MprisClient::updatePosition);
    connect(mpdClient, &MpdClient::titleChanged, this, &MprisClient::updatePosition);
    connect(mpdClient, &MpdClient::artistChanged, this, &MprisClient::updatePosition);
    connect(mpdClient, &MpdClient::albumChanged, this, &MprisClient::updatePosition);
    connect(mpdClient, &MpdClient::elapsedChanged, this, &MprisClient::updatePosition);

    // Update position periodically
    m_positionTimer->setInterval(1000);
    connect(m_positionTimer, &QTimer::timeout, this, &MprisClient::updatePosition);
    m_positionTimer->start();
}

MprisClient::~MprisClient()
{
    m_positionTimer->stop();
}

bool MprisClient::canGoNext() const
{
    return mpdClient() && mpdClient()->state() != "stop";
}

bool MprisClient::canGoPrevious() const
{
    return mpdClient() && mpdClient()->state() != "stop";
}

bool MprisClient::canPlay() const
{
    return mpdClient() && mpdClient()->state() != "play";
}

bool MprisClient::canPause() const
{
    return mpdClient() && mpdClient()->state() == "play";
}

QVariantMap MprisClient::metadata() const
{
    QVariantMap metadata;
    if (!mpdClient())
        return metadata;

    metadata["mpris:trackid"] = mpdClient()->title();
    metadata["xesam:title"] = mpdClient()->title();
    metadata["xesam:artist"] = QVariantList() << mpdClient()->artist();
    metadata["xesam:album"] = mpdClient()->album();
    metadata["mpris:length"] = static_cast<quint64>(mpdClient()->duration() * 1000000);

    return metadata;
}

double MprisClient::position() const
{
    return mpdClient() ? mpdClient()->elapsed() : 0.0;
}

QString MprisClient::playbackStatus() const
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

void MprisClient::quit()
{
    QCoreApplication::quit();
}

void MprisClient::raise()
{
    if (mpdClient() && mpdClient()->window())
        mpdClient()->window()->show();
}

void MprisClient::next()
{
    if (mpdClient())
        mpdClient()->next();
}

void MprisClient::previous()
{
    if (mpdClient())
        mpdClient()->previous();
}

void MprisClient::pause()
{
    if (mpdClient())
        mpdClient()->pause();
}

void MprisClient::playPause()
{
    if (mpdClient())
        mpdClient()->togglePlayPause();
}

void MprisClient::stop()
{
    if (mpdClient())
        mpdClient()->pause(); // MPD doesn't have a stop command, pause is equivalent
}

void MprisClient::play()
{
    if (mpdClient())
        mpdClient()->play();
}

void MprisClient::seek(double offset)
{
    if (mpdClient())
        mpdClient()->seek(static_cast<qint64>(offset));
}

void MprisClient::setPosition(const QString &trackId, double position)
{
    Q_UNUSED(trackId);
    seek(position);
}

void MprisClient::openUri(const QString &uri)
{
    if (mpdClient())
        mpdClient()->playTrack(uri);
}

void MprisClient::updatePosition()
{
    QVariantMap changedProperties;
    changedProperties["Metadata"] = metadata();
    changedProperties["PlaybackStatus"] = playbackStatus();
    changedProperties["Position"] = position();

    emit propertiesChanged("org.mpris.MediaPlayer2.Player",
                          changedProperties,
                          QStringList());
}