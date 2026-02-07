#include "statistics.h"
#include <QStandardPaths>
#include <QDir>
#include <QSqlQuery>
#include <QSqlError>
#include <QDateTime>
#include <QDebug>

StatisticsManager::StatisticsManager(QObject *parent) : QObject(parent)
{
    initDb();
}

StatisticsManager::~StatisticsManager()
{
    QSqlDatabase::removeDatabase("QuesterStats");
}

void StatisticsManager::initDb()
{
    QString dataDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir dir(dataDir);
    if (!dir.exists()) dir.mkpath(".");

    QSqlDatabase db = QSqlDatabase::addDatabase("QSQLITE", "QuesterStats");
    db.setDatabaseName(dataDir + "/stats.db");

    if (!db.open()) {
        qWarning() << "Failed to open statistics database:" << db.lastError().text();
        return;
    }

    QSqlQuery query(db);
    if (!query.exec("CREATE TABLE IF NOT EXISTS play_history ("
                    "id INTEGER PRIMARY KEY AUTOINCREMENT, "
                    "artist TEXT, "
                    "title TEXT, "
                    "album TEXT, "
                    "timestamp INTEGER, "
                    "duration_ms INTEGER)")) {
        qWarning() << "Failed to create stats table:" << query.lastError().text();
    }
}

void StatisticsManager::logPlay(const QString &artist, const QString &title, const QString &album, qint64 durationMs)
{
    QMutexLocker locker(&m_mutex);
    QSqlDatabase db = QSqlDatabase::database("QuesterStats");
    if (!db.isOpen()) return;

    QSqlQuery query(db);
    query.prepare("INSERT INTO play_history (artist, title, album, timestamp, duration_ms) "
                  "VALUES (:artist, :title, :album, :timestamp, :duration)");
    query.bindValue(":artist", artist);
    query.bindValue(":title", title);
    query.bindValue(":album", album);
    query.bindValue(":timestamp", QDateTime::currentSecsSinceEpoch());
    query.bindValue(":duration", durationMs);

    if (!query.exec()) {
        qWarning() << "Failed to log play:" << query.lastError().text();
    }
}

QVariantMap StatisticsManager::getStatsForPeriod(qint64 startTime)
{
    QVariantMap stats;
    QSqlDatabase db = QSqlDatabase::database("QuesterStats");
    if (!db.isOpen()) return stats;

    // Total Time
    QSqlQuery query(db);
    if (startTime > 0) {
        query.prepare("SELECT SUM(duration_ms) FROM play_history WHERE timestamp > :time");
        query.bindValue(":time", startTime);
    } else {
        query.prepare("SELECT SUM(duration_ms) FROM play_history");
    }

    if (query.exec() && query.next()) {
        stats["totalMs"] = query.value(0).toLongLong();
    }

    // Top Artists
    QVariantList topArtists;
    if (startTime > 0) {
        query.prepare("SELECT artist, SUM(duration_ms) as total FROM play_history "
                      "WHERE timestamp > :time GROUP BY artist ORDER BY total DESC LIMIT 5");
        query.bindValue(":time", startTime);
    } else {
        query.prepare("SELECT artist, SUM(duration_ms) as total FROM play_history "
                      "GROUP BY artist ORDER BY total DESC LIMIT 5");
    }

    if (query.exec()) {
        while (query.next()) {
            QVariantMap artist;
            artist["name"] = query.value(0).toString();
            artist["ms"] = query.value(1).toLongLong();
            topArtists.append(artist);
        }
    }
    stats["topArtists"] = topArtists;

    // Top Tracks
    QVariantList topTracks;
    if (startTime > 0) {
        query.prepare("SELECT title, artist, SUM(duration_ms) as total FROM play_history "
                      "WHERE timestamp > :time GROUP BY title, artist ORDER BY total DESC LIMIT 5");
        query.bindValue(":time", startTime);
    } else {
        query.prepare("SELECT title, artist, SUM(duration_ms) as total FROM play_history "
                      "GROUP BY title, artist ORDER BY total DESC LIMIT 5");
    }

    if (query.exec()) {
        while (query.next()) {
            QVariantMap track;
            track["title"] = query.value(0).toString();
            track["artist"] = query.value(1).toString();
            track["ms"] = query.value(2).toLongLong();
            topTracks.append(track);
        }
    }
    stats["topTracks"] = topTracks;

    // Top Albums
    QVariantList topAlbums;
    if (startTime > 0) {
        query.prepare("SELECT album, SUM(duration_ms) as total FROM play_history "
                      "WHERE timestamp > :time GROUP BY album ORDER BY total DESC LIMIT 5");
        query.bindValue(":time", startTime);
    } else {
        query.prepare("SELECT album, SUM(duration_ms) as total FROM play_history "
                      "GROUP BY album ORDER BY total DESC LIMIT 5");
    }

    if (query.exec()) {
        while (query.next()) {
            QVariantMap album;
            album["name"] = query.value(0).toString();
            album["ms"] = query.value(1).toLongLong();
            topAlbums.append(album);
        }
    }
    stats["topAlbums"] = topAlbums;

    // Total Plays
    if (startTime > 0) {
        query.prepare("SELECT COUNT(*) FROM play_history WHERE timestamp > :time");
        query.bindValue(":time", startTime);
    } else {
        query.prepare("SELECT COUNT(*) FROM play_history");
    }

    if (query.exec() && query.next()) {
        stats["totalPlays"] = query.value(0).toInt();
    }

    return stats;
}

QVariantMap StatisticsManager::getWeeklyStats()
{
    QMutexLocker locker(&m_mutex);
    qint64 oneWeekAgo = QDateTime::currentSecsSinceEpoch() - (7 * 24 * 60 * 60);
    return getStatsForPeriod(oneWeekAgo);
}

QVariantMap StatisticsManager::getMonthlyStats()
{
    QMutexLocker locker(&m_mutex);
    qint64 oneMonthAgo = QDateTime::currentSecsSinceEpoch() - (30 * 24 * 60 * 60);
    return getStatsForPeriod(oneMonthAgo);
}

QVariantMap StatisticsManager::getYearlyStats()
{
    QMutexLocker locker(&m_mutex);
    qint64 oneYearAgo = QDateTime::currentSecsSinceEpoch() - (365 * 24 * 60 * 60);
    return getStatsForPeriod(oneYearAgo);
}

QVariantMap StatisticsManager::getAllTimeStats()
{
    QMutexLocker locker(&m_mutex);
    return getStatsForPeriod(0);
}
