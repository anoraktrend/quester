#ifndef STATISTICS_H
#define STATISTICS_H

#include <QObject>
#include <QSqlDatabase>
#include <QVariantMap>
#include <QMutex>
#include <QFuture>
#include <QNetworkAccessManager>

class StatisticsManager : public QObject
{
    Q_OBJECT
public:
    explicit StatisticsManager(QObject *parent = nullptr);
    ~StatisticsManager() override;

    void setListenBrainzCredentials(const QString &token, const QString &username);
    void logPlay(const QString &artist, const QString &title, const QString &album, const QString &uri, qint64 durationMs);
    void submitPlayingNow(const QString &artist, const QString &title, const QString &album, qint64 durationMs);
    Q_INVOKABLE void validateListenBrainzCredentials();

    Q_INVOKABLE QVariantMap getWeeklyStats();
    Q_INVOKABLE QVariantMap getMonthlyStats();
    Q_INVOKABLE QVariantMap getYearlyStats();
    Q_INVOKABLE QVariantMap getAllTimeStats();
    Q_INVOKABLE QString generateWrappedImage(const QString &period);
    Q_INVOKABLE QList<QString> getMostPlayedUris(int limit = 50);

    // Playlist functions
    Q_INVOKABLE void sendQueueAsPlaylist(const QString &playlistName, const QVariantList &tracks);
    Q_INVOKABLE void savePlaylistToListenBrainz(const QString &playlistName, const QVariantList &tracks);
    Q_INVOKABLE void fetchUserPlaylists();

    Q_PROPERTY(bool credentialsValid READ credentialsValid NOTIFY credentialsValidChanged)
    Q_PROPERTY(bool playlistSaved READ playlistSaved NOTIFY playlistSavedChanged)

signals:
    void wrappedGenerated(const QString &path);
    void playLogged();
    void credentialsValidChanged(bool valid);
    void playlistSavedChanged(bool saved);
    void playlistSaved(bool success, const QString &message);
    void playlistsLoaded(const QVariantList &playlists);

private:
    void initDb();
    void checkAutomaticWrapped();
    QVariantMap getStatsForPeriod(qint64 startTime);
    QString getCachePath(const QString &artist, const QString &album);
    QList<int> getActivityGraphData(const QString &period, int &outMax);
    QMutex m_mutex;
    QFuture<void> m_workerFuture;
    QNetworkAccessManager *m_nam;
    QString m_lbToken;
    QString m_lbUsername;
    bool m_credentialsValid = false;
    bool credentialsValid() const { return m_credentialsValid; }
    void setCredentialsValid(bool valid) { 
        if (m_credentialsValid != valid) {
            m_credentialsValid = valid;
            emit credentialsValidChanged(valid);
        }
    }
    void sendListenBrainzRequest(const QString &listenType, const QVariantMap &payload);
    bool playlistSaved() const { return m_playlistSaved; }
    void setPlaylistSaved(bool saved) { 
        if (m_playlistSaved != saved) {
            m_playlistSaved = saved;
            emit playlistSavedChanged(saved);
        }
    }

    bool m_playlistSaved = false;
};

#endif // STATISTICS_H
