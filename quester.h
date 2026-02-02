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
#include <mpd/client.h>

struct AlbumItem {
    QString artist; // Added artist for more accurate searches
    QString name;
    QString artUrl;
    QString uri;
    bool artLoading = false;
};

struct TrackItem {
    QString title;
    QString duration;
    QString uri;
};

struct BrowserItem {
    QString name;
    QString path;
    bool isDir;
};

class AlbumModel : public QAbstractListModel
{
    Q_OBJECT
public:
    enum AlbumRoles {
        NameRole = Qt::UserRole + 1,
        ArtRole,
        ArtistRole // Added ArtistRole
    };

    explicit AlbumModel(QObject *parent = nullptr) : QAbstractListModel(parent) {}
    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;
    void setAlbums(const QList<AlbumItem> &albums);
    void updateArt(int index, const QString &url);
    QList<AlbumItem> m_albums;
};

class TrackModel : public QAbstractListModel
{
    Q_OBJECT
public:
    enum TrackRoles {
        TitleRole = Qt::UserRole + 1,
        DurationRole,
        UriRole
    };

    explicit TrackModel(QObject *parent = nullptr) : QAbstractListModel(parent) {}
    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;
    void setTracks(const QList<TrackItem> &tracks);
    QList<TrackItem> m_tracks;
};

class BrowserModel : public QAbstractListModel
{
    Q_OBJECT
public:
    enum BrowserRoles {
        NameRole = Qt::UserRole + 1,
        PathRole,
        IsDirRole
    };

    explicit BrowserModel(QObject *parent = nullptr) : QAbstractListModel(parent) {}
    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;
    void setItems(const QList<BrowserItem> &items);
    QList<BrowserItem> m_items;
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
    Q_PROPERTY(int currentAlbumIndex READ currentAlbumIndex NOTIFY currentAlbumIndexChanged)
    Q_PROPERTY(QString currentPath READ currentPath NOTIFY currentPathChanged)

public:
    explicit MpdClient(QObject *parent = nullptr);
    ~MpdClient();

    QString artist() const;
    QString title() const;
    QString album() const;
    QString state() const;
    QString albumArt() const;
    qint64 duration() const;
    qint64 elapsed() const;
    AlbumModel* albumModel() const;
    TrackModel* trackModel() const;
    BrowserModel* browserModel() const;
    int currentAlbumIndex() const;
    QString currentPath() const;

    void setWindow(QQuickWindow *window);

public slots:
    void setArtist(const QString &artist);
    void setTitle(const QString &title);
    void setAlbum(const QString &album);
    void setState(const QString &state);

    // Playback controls
    void play();
    void pause();
    void togglePlayPause();
    void next();
    void previous();

    // Library
    void refreshLibrary(); // Existing
    void loadAlbumTracks(int index);
    Q_INVOKABLE void playTrack(const QString &uri);
    Q_INVOKABLE void playAlbum(const QString &artistName, const QString &albumName); // New slot
    Q_INVOKABLE void browsePath(const QString &path);

    // Application/Window controls
    Q_INVOKABLE void quitApplication();
    Q_INVOKABLE void toggleFullscreen();

signals:
    void artistChanged();
    void titleChanged();
    void albumChanged();
    void stateChanged();
    void albumArtChanged();
    void durationChanged();
    void elapsedChanged();
    void currentAlbumIndexChanged();
    void currentPathChanged();

private slots:
    void updateStatus();
    void handleMpdEvent();

private:
    void fetchAlbumArt(const QString &album);
    void fetchCoverForModel(int index, const QString &albumName);
    void fetchAlbumArtFromAPIs(const QString &artist, const QString &album, const QString &cachePath, bool isMainArt, int modelIndex); // New helper
    QByteArray getMpdPicture(const QString &uri);
    void connect();
    void sendIdle();
    void leaveIdle();
    void saveLibraryToCache(const QList<AlbumItem> &albums);
    void loadLibraryFromCache();
    struct mpd_connection *m_connection;
    QSocketNotifier *m_notifier;
    QNetworkAccessManager *m_networkManager;
    QTimer *m_timer;

    QString m_artist;
    QString m_title;
    QString m_album;
    QString m_state;
    QString m_albumArt;
    qint64 m_duration = 0;
    qint64 m_elapsed = 0;
    int m_currentSongId = -1;
    QString m_currentUri;
    int m_currentAlbumIndex = -1;
    bool m_isIdle = false;

    QQuickWindow *m_window = nullptr;

    AlbumModel *m_albumModel;
    TrackModel *m_trackModel;
    BrowserModel *m_browserModel;
    QString m_currentPath;
};

#endif // QUESTER_H