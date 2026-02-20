#ifndef STATISTICS_H
#define STATISTICS_H

#include <QObject>
#include <QSqlDatabase>
#include <QVariantMap>
#include <QMutex>
#include <QFuture>
#include <QNetworkAccessManager>
const int FIFTY = 50;
class StatisticsManager : public QObject
{
    Q_OBJECT
public:
    explicit StatisticsManager(QObject *parent = nullptr);
    ~StatisticsManager() override;

    StatisticsManager(const StatisticsManager&) = delete;
    StatisticsManager& operator=(const StatisticsManager&) = delete;
    StatisticsManager(StatisticsManager&&) = delete;
    StatisticsManager& operator=(StatisticsManager&&) = delete;

    void setListenBrainzCredentials(const QString &token, const QString &username);
    void logPlay(const QString &artist, const QString &title, const QString &album, const QString &uri, qint64 durationMs);
    void submitPlayingNow(const QString &artist, const QString &title, const QString &album, qint64 durationMs);
    Q_INVOKABLE void validateListenBrainzCredentials();

    Q_INVOKABLE QVariantMap getWeeklyStats();
    Q_INVOKABLE QVariantMap getMonthlyStats();
    Q_INVOKABLE QVariantMap getYearlyStats();
    Q_INVOKABLE QVariantMap getAllTimeStats();
    Q_INVOKABLE QString generateWrappedImage(const QString &period);
    Q_INVOKABLE QList<QString> getMostPlayedUris(int limit = FIFTY);
    Q_INVOKABLE void fetchExternalActivityData(const QString &period);
    Q_INVOKABLE QString artistImageUrl(const QString &artist);

    // Playlist functions
    Q_INVOKABLE void sendQueueAsPlaylist(const QString &playlistName, const QVariantList &tracks);
    Q_INVOKABLE void savePlaylistToListenBrainz(const QString &playlistName, const QVariantList &tracks);
    Q_INVOKABLE void fetchUserPlaylists();

    // Last.fm scrobbling - public API
    Q_INVOKABLE void setLastfmCredentials(const QString &apiKey, const QString &secret, const QString &sessionKey);
    Q_INVOKABLE void validateLastfmCredentials();
    Q_INVOKABLE void startLastfmAuth();
    Q_INVOKABLE void completeLastfmAuth(const QString &token);
    
    // QML properties
    Q_PROPERTY(bool lastfmCredentialsValid READ lastfmCredentialsValid NOTIFY lastfmCredentialsValidChanged)
    Q_PROPERTY(QString lastfmUsername READ lastfmUsername NOTIFY lastfmUsernameChanged)
    Q_PROPERTY(int pendingScrobbles READ pendingScrobbles NOTIFY pendingScrobblesChanged)
    Q_PROPERTY(bool credentialsValid READ credentialsValid NOTIFY credentialsValidChanged)
    Q_PROPERTY(bool playlistSaved READ playlistSaved NOTIFY playlistSavedChanged)
    Q_PROPERTY(QVariantMap externalActivityData READ externalActivityData NOTIFY externalActivityDataChanged)

    // Public accessors
    auto lastfmCredentialsValid() const -> bool { return m_lastfmCredentialsValid; }
    auto lastfmUsername() const -> QString { return m_lastfmUsername; }
    auto pendingScrobbles() const -> int { return m_pendingScrobbles; }
    auto credentialsValid() const -> bool { return m_credentialsValid; }
    auto playlistSaved() const -> bool { return m_playlistSaved; }
    auto externalActivityData() const -> QVariantMap { return m_externalActivityData; }

signals:
    void wrappedGenerated(const QString &path);
    void playLogged();
    void credentialsValidChanged(bool valid);
    void playlistSavedChanged(bool saved);
    void playlistSaved(bool success, const QString &message);
    void playlistsLoaded(const QVariantList &playlists);
    void lastfmCredentialsValidChanged(bool valid);
    void lastfmUsernameChanged();
    void pendingScrobblesChanged();
    void lastfmScrobbleError(const QString &message);
    void lastfmScrobbleSuccess(int count);
    void lastfmAuthTokenReceived(const QString &token, const QString &authUrl);
    void externalActivityDataChanged();

private:
    void initDb();
    void checkAutomaticWrapped();
    QVariantMap getStatsForPeriod(qint64 startTime);
    QList<int> getActivityGraphData(const QString &period, int &outMax);
    void sendListenBrainzRequest(const QString &listenType, const QVariantMap &payload);
    void setCredentialsValid(bool valid) { 
        if (m_credentialsValid != valid) {
            m_credentialsValid = valid;
            emit credentialsValidChanged(valid);
        }
    }
    void setPlaylistSaved(bool saved) { 
        if (m_playlistSaved != saved) {
            m_playlistSaved = saved;
            emit playlistSavedChanged(saved);
        }
    }

    // Last.fm scrobbling implementation
    void setLastfmCredentialsInternal(const QString &apiKey, const QString &secret, const QString &sessionKey);
    void sendLastfmRequest(const QString &method, const QMap<QString, QString> &params);
    void scrobbleToLastfmInternal(const QString &artist, const QString &title, const QString &album, qint64 timestamp);
    void submitLastfmNowPlayingInternal(const QString &artist, const QString &title, const QString &album);
    QString getLastfmAuthUrl(const QString &token);
    void getLastfmToken();
    void getLastfmSessionKey(const QString &token);
    static QString generateLastfmSignature(const QMap<QString, QString> &params);

    // External activity data
    void fetchListenBrainzStats(const QString &period);
    void fetchLastfmStats(const QString &period);
    QVariantMap m_externalActivityData;

    QMutex m_mutex;
    QFuture<void> m_workerFuture;
    QNetworkAccessManager *m_nam;
    
    // ListenBrainz members
    QString m_lbToken;
    QString m_lbUsername;
    bool m_credentialsValid = false;
    bool m_playlistSaved = false;
    
    // Last.fm members
    QString m_lastfmApiKey;
    QString m_lastfmSecret;
    QString m_lastfmSessionKey;
    bool m_lastfmCredentialsValid = false;
    QString m_lastfmUsername;
    int m_pendingScrobbles = 0;
    QString m_pendingLastfmToken;
};

#endif // STATISTICS_H
