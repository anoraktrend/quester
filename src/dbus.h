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
    bool valid{};
    MprisPlaylist playlist;
};
Q_DECLARE_METATYPE(MprisActivePlaylist)

auto operator<<(QDBusArgument &argument, const MprisPlaylist &playlist) -> QDBusArgument &;
auto operator>>(const QDBusArgument &argument, MprisPlaylist &playlist) -> const QDBusArgument &;
auto operator<<(QDBusArgument &argument, const MprisActivePlaylist &ap) -> QDBusArgument &;
auto operator>>(const QDBusArgument &argument, MprisActivePlaylist &ap) -> const QDBusArgument &;

class DBusService : public QObject
{
    Q_OBJECT

public:
    explicit DBusService(MpdClient *mpdClient, QObject *parent = nullptr);
    ~DBusService() override;

    // Helper methods for Adaptors
    auto canQuit() const -> bool { return true; }
    auto canSetFullscreen() const -> bool { return false; }
    auto fullscreen() const -> bool { return false; }
    auto canRaise() const -> bool { return true; }
    auto identity() const -> QString { return "Quester"; }
    auto supportedUriSchemes() const -> QStringList { return {"file"}; }
    auto supportedMimeTypes() const -> QStringList { return {"audio/mpeg", "audio/ogg", "audio/flac"}; }

    // Logic implementation
    void quit();
    void raise();
    auto canGoNext() const -> bool;
    auto canGoPrevious() const -> bool;
    auto canPlay() const -> bool;
    auto canPause() const -> bool;
    auto canSeek() const -> bool { return true; }
    auto canControl() const -> bool { return true; }
    auto rate() const -> double { return 1.0; }
    void setRate(double rate);
    auto minimumRate() const -> double { return 1.0; }
    auto maximumRate() const -> double { return 1.0; }
    auto shuffle() const -> bool { return m_mpdClient->random(); }
    void setShuffle(bool shuffle);
    auto loopStatus() const -> QString;
    void setLoopStatus(const QString &status);
    auto metadata() const -> QVariantMap;
    auto volume() const -> double;
    void setVolume(double volume);
    auto position() const -> qlonglong;
    auto playbackStatus() const -> QString;

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
    auto tracks() const -> QList<QDBusObjectPath>;
    auto canEditTracks() const -> bool { return true; }
    auto getTracksMetadata(const QList<QDBusObjectPath> &trackIds) const -> QList<QVariantMap>;
    void addTrack(const QString &uri, const QDBusObjectPath &afterTrack, bool setAsCurrent);
    void removeTrack(const QDBusObjectPath &trackId);
    void goNext();
    void goPrevious();

    // Playlists interface methods
    auto playlistCount() const -> quint32;
    auto orderings() const -> QStringList;
    auto activePlaylist() const -> MprisActivePlaylist;
    void activatePlaylist(const QDBusObjectPath &playlistId);
    auto getPlaylists(quint32 index, quint32 maxCount, const QString &order, bool reverseOrder) -> QList<MprisPlaylist>;

signals:
    void seeked(qlonglong position);
    void TrackListReplaced(const QList<QDBusObjectPath> &tracks, const QDBusObjectPath &currentTrack);
    void TrackAdded(const QDBusObjectPath &track, const QDBusObjectPath &afterTrack);
    void TrackRemoved(const QDBusObjectPath &track);
    void TrackMetadataChanged(const QDBusObjectPath &track, const QVariantMap &metadata);

private:
    MpdClient *m_mpdClient;
    QDBusConnection m_connection;

    void broadcastProperties();
    auto createTrackId(const QString &uri) const -> QString;
    auto uriFromTrackId(const QString &trackId) const -> QString;
    auto trackIdToPosition(const QString &trackId) const -> int;
    auto positionToTrackId(int position) const -> QString;

private:
    auto getMetadataForTrack(const QueueItem &item) const -> QVariantMap;
    mutable QHash<QString, QString> m_trackIdToUri;
    mutable QHash<QString, QString> m_uriToTrackId;
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
    [[nodiscard]] auto canQuit() const -> bool;
    [[nodiscard]] auto canSetFullscreen() const -> bool;
    [[nodiscard]] auto fullscreen() const -> bool;
    [[nodiscard]] auto canRaise() const -> bool;
    [[nodiscard]] auto identity() const -> QString;
    [[nodiscard]] auto supportedUriSchemes() const -> QStringList;
    [[nodiscard]] auto supportedMimeTypes() const -> QStringList;

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
    [[nodiscard]] auto canGoNext() const -> bool;
    [[nodiscard]] auto canGoPrevious() const -> bool;
    [[nodiscard]] auto canPlay() const -> bool;
    [[nodiscard]] auto canPause() const -> bool;
    [[nodiscard]] auto canSeek() const -> bool;
    [[nodiscard]] auto canControl() const -> bool;
    [[nodiscard]] auto rate() const -> double;
    void setRate(double rate);
    [[nodiscard]] auto minimumRate() const -> double;
    [[nodiscard]] auto maximumRate() const -> double;
    [[nodiscard]] auto shuffle() const -> bool;
    void setShuffle(bool shuffle);
    [[nodiscard]] auto loopStatus() const -> QString;
    void setLoopStatus(const QString &status);
    [[nodiscard]] auto metadata() const -> QVariantMap;
    [[nodiscard]] auto volume() const -> double;
    void setVolume(double volume);
    [[nodiscard]] auto position() const -> qlonglong;
    [[nodiscard]] auto playbackStatus() const -> QString;

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
    [[nodiscard]] auto tracks() const -> QList<QDBusObjectPath>;
    [[nodiscard]] auto canEditTracks() const -> bool;

public slots:
    auto GetTracksMetadata(const QList<QDBusObjectPath> &trackIds) -> QList<QVariantMap>;
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
    [[nodiscard]] auto playlistCount() const -> quint32;
    [[nodiscard]] auto orderings() const -> QStringList;
    [[nodiscard]] auto activePlaylist() const -> MprisActivePlaylist;

public slots:
    void ActivatePlaylist(const QDBusObjectPath &PlaylistId);
    auto GetPlaylists(quint32 Index, quint32 MaxCount, const QString &Order, bool ReverseOrder) -> QList<MprisPlaylist>;

signals:
    void PlaylistChanged(const MprisPlaylist &Playlist);

private:
    DBusService *m_service;
};

#endif // DBUS_H