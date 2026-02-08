#include "statistics.h"
#include <QStandardPaths>
#include <QDir>
#include <QSqlQuery>
#include <QSqlError>
#include <QDateTime>
#include <QPainter>
#include <QImage>
#include <QCryptographicHash>
#include <QDebug>
#include <QSettings>
#include <QTimer>

StatisticsManager::StatisticsManager(QObject *parent) : QObject(parent)
{
    initDb();
    QTimer::singleShot(5000, this, &StatisticsManager::checkAutomaticWrapped);
}

StatisticsManager::~StatisticsManager()
{
    QSqlDatabase::removeDatabase("QuesterStats");
}

void StatisticsManager::initDb()
{
    // KISS: Direct SQLite usage allows for a self-contained, zero-configuration
    // statistics engine without needing an ORM or external database server.
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
        query.prepare("SELECT album, artist, SUM(duration_ms) as total FROM play_history "
                      "WHERE timestamp > :time GROUP BY album, artist ORDER BY total DESC LIMIT 5");
        query.bindValue(":time", startTime);
    } else {
        query.prepare("SELECT album, artist, SUM(duration_ms) as total FROM play_history "
                      "GROUP BY album, artist ORDER BY total DESC LIMIT 5");
    }

    if (query.exec()) {
        while (query.next()) {
            QVariantMap album;
            album["name"] = query.value(0).toString();
            album["artist"] = query.value(1).toString();
            album["ms"] = query.value(2).toLongLong();
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

QString StatisticsManager::generateWrappedImage(const QString &period)
{
    QVariantMap stats;
    QString titleText;
    QString subTitle;

    if (period == "weekly") {
        stats = getWeeklyStats();
        titleText = "This Week";
        subTitle = "Vibe Check";
    } else if (period == "monthly") {
        stats = getMonthlyStats();
        titleText = "This Month";
        subTitle = "The Soundtrack";
    } else if (period == "yearly") {
        stats = getYearlyStats();
        titleText = "The Year";
        subTitle = "What a ride.";
    } else {
        stats = getAllTimeStats();
        titleText = "All Time";
        subTitle = "Legendary Status";
    }

    // Canvas setup (Story format 9:16)
    int w = 1080;
    int h = 1920;
    QImage img(w, h, QImage::Format_ARGB32);
    img.fill(QColor("#121212")); // Dark background

    QPainter p(&img);
    p.setRenderHint(QPainter::Antialiasing);
    p.setRenderHint(QPainter::TextAntialiasing);

    QColor accentColor("#BB86FC");
    QColor textColor("#FFFFFF");
    QColor dimColor("#B0B0B0");

    // Header
    p.setPen(textColor);
    p.setFont(QFont("Sans Serif", 80, QFont::Bold));
    p.drawText(QRect(50, 100, w-100, 150), Qt::AlignCenter, titleText);

    p.setPen(accentColor);
    p.setFont(QFont("Sans Serif", 40, QFont::Medium));
    p.drawText(QRect(50, 250, w-100, 80), Qt::AlignCenter, subTitle);

    int y = 450;

    // Total Time
    long long totalMs = stats["totalMs"].toLongLong();
    double hours = totalMs / 1000.0 / 3600.0;
    QString timePhrase = hours > 100 ? "Do you ever sleep?" : (hours > 10 ? "Music is life." : "Warming up.");

    p.setPen(textColor);
    p.setFont(QFont("Sans Serif", 30));
    p.drawText(QRect(50, y, w-100, 50), Qt::AlignCenter, "You listened for");
    p.setPen(accentColor);
    p.setFont(QFont("Sans Serif", 70, QFont::Bold));
    p.drawText(QRect(50, y + 60, w-100, 100), Qt::AlignCenter, QString::number(hours, 'f', 1) + " Hours");
    p.setPen(dimColor);
    p.setFont(QFont("Sans Serif", 30, -1, true));
    p.drawText(QRect(50, y + 170, w-100, 50), Qt::AlignCenter, timePhrase);

    y += 350;

    // Top Artist
    QVariantList artists = stats["topArtists"].toList();
    if (!artists.isEmpty()) {
        QString artistName = artists.first().toMap()["name"].toString();
        p.setPen(textColor);
        p.setFont(QFont("Sans Serif", 30));
        p.drawText(QRect(50, y, w-100, 50), Qt::AlignCenter, "Top Artist");
        p.setPen(accentColor);
        p.setFont(QFont("Sans Serif", 50, QFont::Bold));
        p.drawText(QRect(50, y + 60, w-100, 80), Qt::AlignCenter, artistName);
        p.setPen(dimColor);
        p.setFont(QFont("Sans Serif", 25));
        p.drawText(QRect(50, y + 140, w-100, 40), Qt::AlignCenter, "We get it, you're a fan.");
    }

    y += 250;

    // Top Album Art
    QVariantList albums = stats["topAlbums"].toList();
    if (!albums.isEmpty()) {
        QVariantMap albumMap = albums.first().toMap();
        QString albumName = albumMap["name"].toString();
        QString artistName = albumMap["artist"].toString();

        if (!artistName.isEmpty()) {
            QImage art(getCachePath(artistName, albumName));
            if (!art.isNull()) {
                int artSize = 500;
                p.drawImage(QRect((w - artSize) / 2, y, artSize, artSize), art);
            }
        }
    }

    // Activity Graph
    int maxVal = 0;
    QList<int> graphData = getActivityGraphData(period, maxVal);
    
    if (maxVal > 0 && !graphData.isEmpty()) {
        int graphH = 200;
        int graphW = w - 140;
        int graphX = 70;
        int graphY = h - graphH - 150;
        
        p.setPen(Qt::NoPen);
        p.setBrush(accentColor);
        
        int step = graphW / graphData.size();
        int barWidth = step - 4;
        if (barWidth < 2) barWidth = 2;
        
        for (int i = 0; i < graphData.size(); ++i) {
            int val = graphData[i];
            if (val == 0) continue;
            int barH = (int)((double)val / maxVal * graphH);
            if (barH < 5) barH = 5;
            
            p.drawRoundedRect(graphX + i * step, graphY + (graphH - barH), barWidth, barH, 4, 4);
        }
        
        p.setPen(dimColor);
        p.setFont(QFont("Sans Serif", 24));
        p.drawText(QRect(graphX, graphY - 50, graphW, 40), Qt::AlignCenter, "Listening Activity");
    }

    QString fileName = QString("QuesterWrapped_%1_%2.png").arg(period, QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss"));
    QString path = QStandardPaths::writableLocation(QStandardPaths::PicturesLocation) + "/" + fileName;
    img.save(path);
    emit wrappedGenerated(path);
    return path;
}

void StatisticsManager::checkAutomaticWrapped()
{
    QSettings settings("Quester", "Quester");
    qint64 now = QDateTime::currentSecsSinceEpoch();

    auto check = [&](const QString &period, const QString &key, qint64 interval, std::function<QVariantMap()> getStats) {
        qint64 last = settings.value(key, 0).toLongLong();
        if (now - last > interval) {
            if (getStats()["totalMs"].toLongLong() > 0) {
                generateWrappedImage(period);
                settings.setValue(key, now);
            }
        }
    };

    check("weekly", "lastWeeklyWrapped", 7 * 24 * 3600, [this](){ return getWeeklyStats(); });
    check("monthly", "lastMonthlyWrapped", 30 * 24 * 3600, [this](){ return getMonthlyStats(); });
    check("yearly", "lastYearlyWrapped", 365 * 24 * 3600, [this](){ return getYearlyStats(); });
}

QList<int> StatisticsManager::getActivityGraphData(const QString &period, int &outMax)
{
    QList<int> data;
    outMax = 0;
    QSqlDatabase db = QSqlDatabase::database("QuesterStats");
    if (!db.isOpen()) return data;

    QSqlQuery query(db);
    QString dateFormat;
    int numPoints = 0;
    qint64 startTime = 0;
    qint64 now = QDateTime::currentSecsSinceEpoch();

    if (period == "weekly") {
        dateFormat = "%w"; // 0-6 (Sun-Sat)
        numPoints = 7;
        startTime = now - (7 * 24 * 3600);
    } else if (period == "monthly") {
        dateFormat = "%d"; // 01-31
        numPoints = 31;
        startTime = now - (30 * 24 * 3600);
    } else if (period == "yearly") {
        dateFormat = "%m"; // 01-12
        numPoints = 12;
        startTime = now - (365 * 24 * 3600);
    } else {
        return data;
    }

    for(int i=0; i<numPoints; ++i) data.append(0);

    QString sql = QString("SELECT strftime('%1', datetime(timestamp, 'unixepoch', 'localtime')), COUNT(*) "
                          "FROM play_history WHERE timestamp > :start "
                          "GROUP BY strftime('%1', datetime(timestamp, 'unixepoch', 'localtime'))")
                          .arg(dateFormat);
    
    query.prepare(sql);
    query.bindValue(":start", startTime);
    
    if (query.exec()) {
        while(query.next()) {
            int key = query.value(0).toInt();
            int count = query.value(1).toInt();
            int index = -1;

            if (period == "weekly") index = key; // 0-6
            else if (period == "monthly" || period == "yearly") index = key - 1;

            if (index >= 0 && index < data.size()) {
                data[index] = count;
                if (count > outMax) outMax = count;
            }
        }
    }
    return data;
}

QString StatisticsManager::getCachePath(const QString &artist, const QString &album)
{
    QString cacheDir = QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + "/covers/";
    QByteArray hashName = QCryptographicHash::hash((artist + album).toUtf8(), QCryptographicHash::Md5).toHex();
    return cacheDir + hashName + ".jpg";
}
