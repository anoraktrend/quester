#ifndef STATISTICS_H
#define STATISTICS_H

#include <QObject>
#include <QSqlDatabase>
#include <QVariantMap>
#include <QMutex>

class StatisticsManager : public QObject
{
    Q_OBJECT
public:
    explicit StatisticsManager(QObject *parent = nullptr);
    ~StatisticsManager() override;

    void logPlay(const QString &artist, const QString &title, const QString &album, qint64 durationMs);
    Q_INVOKABLE QVariantMap getWeeklyStats();
    Q_INVOKABLE QVariantMap getMonthlyStats();
    Q_INVOKABLE QVariantMap getYearlyStats();
    Q_INVOKABLE QVariantMap getAllTimeStats();
    Q_INVOKABLE QString generateWrappedImage(const QString &period);

signals:
    void wrappedGenerated(const QString &path);

private:
    void initDb();
    void checkAutomaticWrapped();
    QVariantMap getStatsForPeriod(qint64 startTime);
    QString getCachePath(const QString &artist, const QString &album);
    QList<int> getActivityGraphData(const QString &period, int &outMax);
    QMutex m_mutex;
};

#endif // STATISTICS_H