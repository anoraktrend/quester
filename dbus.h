#ifndef DBUS_H
#define DBUS_H

#include <QObject>
#include <QString>
#include <QDBusAbstractAdaptor>
#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusObjectPath>
#include <QDBusVariant>
#include <QVariantMap>
#include <QVariantList>
#include <QtDBus>
#include <QtCore>
#include "quester.h"

struct MprisPlaylist {
    QDBusObjectPath id;
    QString name;
    QString iconUri;
};
Q_DECLARE_METATYPE(MprisPlaylist)

struct MprisActivePlaylist {
    bool valid;
    MprisPlaylist playlist;
};
Q_DECLARE_METATYPE(MprisActivePlaylist)

QDBusArgument &operator<<(QDBusArgument &argument, const MprisPlaylist &playlist);
const QDBusArgument &operator>>(const QDBusArgument &argument, MprisPlaylist &playlist);
QDBusArgument &operator<<(QDBusArgument &argument, const MprisActivePlaylist &ap);
const QDBusArgument &operator>>(const QDBusArgument &argument, MprisActivePlaylist &ap);

class DBusService : public QObject
{
    Q_OBJECT

public:
    explicit DBusService(MpdClient *mpdClient, QObject *parent = nullptr);
    ~DBusService() override;

    // Helper methods for Adaptors
    bool canQuit() const { return true; }
    bool canSetFullscreen() const { return false; }
    bool fullscreen() const { return false; }
    bool canRaise() const { return true; }
    QString identity() const { return "Quester"; }
    QStringList supportedUriSchemes() const { return {"file"}; }
    QStringList supportedMimeTypes() const { return {"audio/mpeg", "audio/ogg", "audio/flac"}; }

    // Logic implementation
    void quit();
    void raise();
    bool canGoNext() const;
    bool canGoPrevious() const;
    bool canPlay() const;
    bool canPause() const;
    bool canSeek() const { return true; }
    bool canControl() const { return true; }
    double rate() const { return 1.0; }
    void setRate(double rate);
    double minimumRate() const { return 1.0; }
    double maximumRate() const { return 1.0; }
    bool shuffle() const { return mpdClient()->random(); }
    void setShuffle(bool shuffle);
    QString loopStatus() const;
    void setLoopStatus(const QString &status);
    QVariantMap metadata() const;
    double volume() const;
    void setVolume(double volume);
    qlonglong position() const;
    QString playbackStatus() const;

    void next();
    void previous();
    void pause();
    void playPause();
    void stop();
    void play();
    void seek(double offset);
    void setPosition(const QString &trackId, double position);
    void openUri(const QString &uri);

    // Track List interface methods
    QList<QDBusObjectPath> tracks() const;
    bool canEditTracks() const { return true; }
    void addTrack(const QString &uri, const QDBusObjectPath &afterTrack, bool setAsCurrent);
    void removeTrack(const QDBusObjectPath &trackId);
    void goNext();
    void goPrevious();

    // Playlists interface methods
    quint32 playlistCount() const;
    QStringList orderings() const;
    MprisActivePlaylist activePlaylist() const;
    void activatePlaylist(const QDBusObjectPath &playlistId);
    QList<MprisPlaylist> getPlaylists(quint32 index, quint32 maxCount, const QString &order, bool reverseOrder);

signals:
    void seeked(qlonglong position);
    void TrackListReplaced(const QList<QDBusObjectPath> &tracks, const QDBusObjectPath &currentTrack);
    void TrackAdded(const QDBusObjectPath &track, const QDBusObjectPath &afterTrack);
    void TrackRemoved(const QDBusObjectPath &track);
    void TrackMetadataChanged(const QDBusObjectPath &track, const QVariantMap &metadata);

private:
    MpdClient *m_mpdClient;
    QDBusConnection m_connection;

    MpdClient *mpdClient() const { return m_mpdClient; }
    void broadcastProperties();
    QString createTrackId(const QString &uri) const;
    QString uriFromTrackId(const QString &trackId) const;
    int trackIdToPosition(const QString &trackId) const;
    QString positionToTrackId(int position) const;
};

class MprisRootAdaptor : public QDBusAbstractAdaptor
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.mpris.MediaPlayer2")
    Q_PROPERTY(bool CanQuit READ canQuit)
    Q_PROPERTY(bool CanSetFullscreen READ canSetFullscreen)
    Q_PROPERTY(bool Fullscreen READ fullscreen)
    Q_PROPERTY(bool CanRaise READ canRaise)
    Q_PROPERTY(QString Identity READ identity)
    Q_PROPERTY(QStringList SupportedUriSchemes READ supportedUriSchemes)
    Q_PROPERTY(QStringList SupportedMimeTypes READ supportedMimeTypes)

public:
    explicit MprisRootAdaptor(DBusService *parent);
    bool canQuit() const;
    bool canSetFullscreen() const;
    bool fullscreen() const;
    bool canRaise() const;
    QString identity() const;
    QStringList supportedUriSchemes() const;
    QStringList supportedMimeTypes() const;

public slots:
    void Quit();
    void Raise();

private:
    DBusService *m_service;
};

class MprisPlayerAdaptor : public QDBusAbstractAdaptor
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.mpris.MediaPlayer2.Player")
    Q_PROPERTY(bool CanGoNext READ canGoNext)
    Q_PROPERTY(bool CanGoPrevious READ canGoPrevious)
    Q_PROPERTY(bool CanPlay READ canPlay)
    Q_PROPERTY(bool CanPause READ canPause)
    Q_PROPERTY(bool CanSeek READ canSeek)
    Q_PROPERTY(bool CanControl READ canControl)
    Q_PROPERTY(double Rate READ rate WRITE setRate)
    Q_PROPERTY(double MinimumRate READ minimumRate)
    Q_PROPERTY(double MaximumRate READ maximumRate)
    Q_PROPERTY(bool Shuffle READ shuffle WRITE setShuffle)
    Q_PROPERTY(QString LoopStatus READ loopStatus WRITE setLoopStatus)
    Q_PROPERTY(QVariantMap Metadata READ metadata)
    Q_PROPERTY(double Volume READ volume WRITE setVolume)
    Q_PROPERTY(qlonglong Position READ position)
    Q_PROPERTY(QString PlaybackStatus READ playbackStatus)

public:
    explicit MprisPlayerAdaptor(DBusService *parent);
    bool canGoNext() const;
    bool canGoPrevious() const;
    bool canPlay() const;
    bool canPause() const;
    bool canSeek() const;
    bool canControl() const;
    double rate() const;
    void setRate(double rate);
    double minimumRate() const;
    double maximumRate() const;
    bool shuffle() const;
    void setShuffle(bool shuffle);
    QString loopStatus() const;
    void setLoopStatus(const QString &status);
    QVariantMap metadata() const;
    double volume() const;
    void setVolume(double volume);
    qlonglong position() const;
    QString playbackStatus() const;

public slots:
    void Next();
    void Previous();
    void Pause();
    void PlayPause();
    void Stop();
    void Play();
    void Seek(qlonglong offset);
    void SetPosition(const QDBusObjectPath &trackId, qlonglong position);
    void OpenUri(const QString &uri);

signals:
    void Seeked(qlonglong position);

private:
    DBusService *m_service;
};

class MprisTrackListAdaptor : public QDBusAbstractAdaptor
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.mpris.MediaPlayer2.TrackList")
    Q_PROPERTY(QList<QDBusObjectPath> Tracks READ tracks)
    Q_PROPERTY(bool CanEditTracks READ canEditTracks)

public:
    explicit MprisTrackListAdaptor(DBusService *parent);
    QList<QDBusObjectPath> tracks() const;
    bool canEditTracks() const;

public slots:
    QList<QVariantMap> GetTracksMetadata(const QList<QDBusObjectPath> &trackIds);
    void AddTrack(const QString &uri, const QDBusObjectPath &afterTrack, bool setAsCurrent);
    void RemoveTrack(const QDBusObjectPath &trackId);
    void GoNext();
    void GoPrevious();

signals:
    void TrackListReplaced(const QList<QDBusObjectPath> &tracks, const QDBusObjectPath &currentTrack);
    void TrackAdded(const QDBusObjectPath &track, const QDBusObjectPath &afterTrack);
    void TrackRemoved(const QDBusObjectPath &track);
    void TrackMetadataChanged(const QDBusObjectPath &track, const QVariantMap &metadata);

private:
    DBusService *m_service;
};

class MprisPlaylistsAdaptor : public QDBusAbstractAdaptor
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.mpris.MediaPlayer2.Playlists")
    Q_PROPERTY(quint32 PlaylistCount READ playlistCount)
    Q_PROPERTY(QStringList Orderings READ orderings)
    Q_PROPERTY(MprisActivePlaylist ActivePlaylist READ activePlaylist)

public:
    explicit MprisPlaylistsAdaptor(DBusService *parent);
    quint32 playlistCount() const;
    QStringList orderings() const;
    MprisActivePlaylist activePlaylist() const;

public slots:
    void ActivatePlaylist(const QDBusObjectPath &PlaylistId);
    QList<MprisPlaylist> GetPlaylists(quint32 Index, quint32 MaxCount, const QString &Order, bool ReverseOrder);

signals:
    void PlaylistChanged(const MprisPlaylist &Playlist);

private:
    DBusService *m_service;
};

#endif // DBUS_H