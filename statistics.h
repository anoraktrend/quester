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

private:
    void initDb();
    QVariantMap getStatsForPeriod(qint64 startTime);
    QString getCachePath(const QString &artist, const QString &album);
    QMutex m_mutex;
};

#endif // STATISTICS_H