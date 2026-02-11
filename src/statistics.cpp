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
#include <QtConcurrent/QtConcurrent>
#include <QThread>
#include <QNetworkAccessManager>
#include <QXmlStreamReader>
#include <QDesktopServices>
#include <QUrl>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <algorithm>

#include <ws.h>
#include <Auth.h>
#include <Audioscrobbler.h>

// Define the extern constants for liblastfm
namespace lastfm {
namespace ws {
const char* ApiKey = "5b184bbfb5f3d1ac3a4955a6676d7dc3";
const char* SharedSecret = "cc7e582cd3e1f5e79c0e9098fbc019ff";
}
}

const QString LASTFM_API_KEY = "5b184bbfb5f3d1ac3a4955a6676d7dc3";
const QString LASTFM_SECRET = "cc7e582cd3e1f5e79c0e9098fbc019ff";

StatisticsManager::StatisticsManager(QObject *parent) : QObject(parent), m_nam(new QNetworkAccessManager(this))
{
    initDb();
    QTimer::singleShot(5000, this, &StatisticsManager::checkAutomaticWrapped);
    
    // Validate existing credentials at startup if both token and username exist
    QTimer::singleShot(1000, this, &StatisticsManager::validateListenBrainzCredentials);
    QTimer::singleShot(1000, this, &StatisticsManager::validateLastfmCredentials);

    // Set built-in Last.fm credentials
    m_lastfmApiKey = LASTFM_API_KEY;
    m_lastfmSecret = LASTFM_SECRET;
    QSettings settings("Quester", "Quester");
    QString sessionKey = settings.value("lastfmSessionKey").toString();
    if (!sessionKey.isEmpty()) {
        m_lastfmSessionKey = sessionKey;
    }

    if (!m_lastfmSessionKey.isEmpty()) {
        lastfm::ws::SessionKey = m_lastfmSessionKey;
        lastfm::setNetworkAccessManager(m_nam);
        qDebug() << "[Last.fm] Loaded existing session key";
    } else {
        qDebug() << "[Last.fm] No existing session key found";
    }
}

StatisticsManager::~StatisticsManager()
{
    m_workerFuture.waitForFinished();
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

    QSqlQuery pragma(db);
    pragma.exec("PRAGMA journal_mode=WAL;");
    pragma.exec("PRAGMA synchronous=NORMAL;");

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
    
    // Migration: Add uri column if it doesn't exist
    bool hasUri = false;
    QSqlQuery check(db);
    check.exec("PRAGMA table_info(play_history)");
    while (check.next()) {
        if (check.value(1).toString() == "uri") hasUri = true;
    }
    if (!hasUri) {
        QSqlQuery alter(db);
        alter.exec("ALTER TABLE play_history ADD COLUMN uri TEXT");
    }

    query.exec("CREATE INDEX IF NOT EXISTS idx_ph_timestamp ON play_history(timestamp)");
}

void StatisticsManager::logPlay(const QString &artist, const QString &title, const QString &album, const QString &uri, qint64 durationMs)
{
    // Submit to ListenBrainz (Main Thread)
    if (!m_lbToken.isEmpty()) {
        QVariantMap payload;
        QVariantMap metadata;
        metadata["artist_name"] = artist;
        metadata["track_name"] = title;
        metadata["release_name"] = album;
        QVariantMap additional;
        // Ensure duration_ms is a positive integer (minimum 1 second if 0 or negative)
        if (durationMs <= 0) {
            durationMs = 1000; // Default to 1 second if invalid duration
        }
        additional["duration_ms"] = durationMs;
        metadata["additional_info"] = additional;
        
        payload["track_metadata"] = metadata;
        payload["listened_at"] = QDateTime::currentSecsSinceEpoch();
        
        sendListenBrainzRequest("single", payload);
    }

    // Submit to Last.fm (Now Playing)
    if (!m_lastfmSessionKey.isEmpty()) {
        qDebug() << "[Last.fm] Scrobbling now playing:" << artist << "-" << title << "(" << album << ")";
        lastfm::Audioscrobbler scrobbler( "Quester" );
        lastfm::MutableTrack track;
        track.setArtist( artist );
        track.setTitle( title );
        track.setAlbum( album );
        scrobbler.nowPlaying( track );
    } else {
        qDebug() << "[Last.fm] No session key, skipping scrobble";
    }

    // Log to Local DB (Worker Thread)
    m_workerFuture = QtConcurrent::run([this, artist, title, album, uri, durationMs]() -> void {
        const QString connectionName = QStringLiteral("QuesterStatsWorker-%1").arg(reinterpret_cast<quintptr>(QThread::currentThreadId()));
        {
            QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connectionName);
            db.setDatabaseName(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + QStringLiteral("/stats.db"));

            if (db.open()) {
                QSqlQuery query(db);
                query.prepare(QStringLiteral("INSERT INTO play_history (artist, title, album, uri, timestamp, duration_ms) "
                              "VALUES (:artist, :title, :album, :uri, :timestamp, :duration)"));
                query.bindValue(QStringLiteral(":artist"), artist);
                query.bindValue(QStringLiteral(":title"), title);
                query.bindValue(QStringLiteral(":album"), album);
                query.bindValue(QStringLiteral(":uri"), uri);
                query.bindValue(QStringLiteral(":timestamp"), QDateTime::currentSecsSinceEpoch());
                query.bindValue(QStringLiteral(":duration"), durationMs);

                if (!query.exec()) {
                    qWarning() << "Failed to log play:" << query.lastError().text();
                }
            } else {
                qWarning() << "Failed to open stats db in worker:" << db.lastError().text();
            }
        }
        QSqlDatabase::removeDatabase(connectionName);
        emit playLogged();
    });
}

auto StatisticsManager::getStatsForPeriod(qint64 startTime) -> QVariantMap
{
    QVariantMap stats;
    QSqlDatabase db = QSqlDatabase::database("QuesterStats");
    if (!db.isOpen()) return stats;

    // Total Time and Plays
    QSqlQuery query(db);
    if (startTime > 0) {
        query.prepare("SELECT SUM(duration_ms), COUNT(*) FROM play_history WHERE timestamp > :time");
        query.bindValue(":time", startTime);
    } else {
        query.prepare("SELECT SUM(duration_ms), COUNT(*) FROM play_history");
    }

    if (query.exec() && query.next()) {
        stats["totalMs"] = query.value(0).toLongLong();
        stats["totalPlays"] = query.value(1).toInt();
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

    return stats;
}

auto StatisticsManager::getWeeklyStats() -> QVariantMap
{
    QMutexLocker locker(&m_mutex);
    qint64 oneWeekAgo = QDateTime::currentSecsSinceEpoch() - (7 * 24 * 60 * 60);
    return getStatsForPeriod(oneWeekAgo);
}

auto StatisticsManager::getMonthlyStats() -> QVariantMap
{
    QMutexLocker locker(&m_mutex);
    qint64 oneMonthAgo = QDateTime::currentSecsSinceEpoch() - (30 * 24 * 60 * 60);
    return getStatsForPeriod(oneMonthAgo);
}

auto StatisticsManager::getYearlyStats() -> QVariantMap
{
    QMutexLocker locker(&m_mutex);
    qint64 oneYearAgo = QDateTime::currentSecsSinceEpoch() - (365 * 24 * 60 * 60);
    return getStatsForPeriod(oneYearAgo);
}

auto StatisticsManager::getAllTimeStats() -> QVariantMap
{
    QMutexLocker locker(&m_mutex);
    return getStatsForPeriod(0);
}

auto StatisticsManager::generateWrappedImage(const QString &period) -> QString
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

    auto check = [&](const QString &period, const QString &key, qint64 interval, const std::function<QVariantMap()>& getStats) -> void {
        qint64 last = settings.value(key, 0).toLongLong();
        if (now - last > interval) {
            if (getStats()["totalMs"].toLongLong() > 0) {
                generateWrappedImage(period);
                settings.setValue(key, now);
            }
        }
    };

    check("weekly", "lastWeeklyWrapped", 7 * 24 * 3600, [this]() -> QVariantMap { return getWeeklyStats(); });
    check("monthly", "lastMonthlyWrapped", 30 * 24 * 3600, [this]() -> QVariantMap { return getMonthlyStats(); });
    check("yearly", "lastYearlyWrapped", 365 * 24 * 3600, [this]() -> QVariantMap { return getYearlyStats(); });
}

auto StatisticsManager::getActivityGraphData(const QString &period, int &outMax) -> QList<int>
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

auto StatisticsManager::getCachePath(const QString &artist, const QString &album) -> QString
{
    QString cacheDir = QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + "/covers/";
    QByteArray hashName = QCryptographicHash::hash((artist + album).toUtf8(), QCryptographicHash::Md5).toHex();
    return cacheDir + hashName + ".jpg";
}

void StatisticsManager::setListenBrainzCredentials(const QString &token, const QString &username)
{
    m_lbToken = token;
    m_lbUsername = username;
}

void StatisticsManager::submitPlayingNow(const QString &artist, const QString &title, const QString &album, qint64 durationMs)
{
    if (m_lbToken.isEmpty()) return;

    QVariantMap payload;
    QVariantMap metadata;
    metadata["artist_name"] = artist;
    metadata["track_name"] = title;
    metadata["release_name"] = album;
    QVariantMap additional;
    // Ensure duration_ms is a positive integer (minimum 1 second if 0 or negative)
    if (durationMs <= 0) {
        durationMs = 1000; // Default to 1 second if invalid duration
    }
    additional["duration_ms"] = durationMs;
    metadata["additional_info"] = additional;
    
    payload["track_metadata"] = metadata;
    
    sendListenBrainzRequest("playing_now", payload);
}

void StatisticsManager::validateListenBrainzCredentials()
{
    if (m_lbToken.isEmpty()) {
        qWarning() << "[ListenBrainz] Cannot validate: token is empty";
        setCredentialsValid(false);
        return;
    }
    
    // Validate token using the official ListenBrainz API endpoint
    QUrl url("https://api.listenbrainz.org/1/validate-token");
    QNetworkRequest request(url);
    request.setRawHeader("Authorization", QString("Token %1").arg(m_lbToken).toUtf8());
    
    QNetworkReply *reply = m_nam->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() -> void {
        reply->deleteLater();
        QByteArray response = reply->readAll();
        if (reply->error() == QNetworkReply::NoError) {
            qDebug().noquote() << "[ListenBrainz] Token validation response:" << QString::fromLatin1(response);
            // Parse response to check if token is valid
            QJsonParseError error;
            QJsonDocument doc = QJsonDocument::fromJson(response, &error);
            if (error.error == QJsonParseError::NoError && doc.isObject()) {
                QJsonObject obj = doc.object();
                bool valid = obj["valid"].toBool();
                if (valid) {
                    setCredentialsValid(true);
                    qDebug() << "[ListenBrainz] Token is valid";
                } else {
                    setCredentialsValid(false);
                    qWarning() << "[ListenBrainz] Token validation failed";
                }
            } else {
                setCredentialsValid(false);
            }
        } else {
            qWarning().noquote() << "[ListenBrainz] Token validation error:" << reply->errorString();
            setCredentialsValid(false);
        }
    });
}

void StatisticsManager::sendListenBrainzRequest(const QString &listenType, const QVariantMap &payload)
{
    QUrl url("https://api.listenbrainz.org/1/submit-listens");
    QNetworkRequest request(url);
    request.setRawHeader("Authorization", QString("Token %1").arg(m_lbToken).toUtf8());
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");

    QJsonObject root;
    root["listen_type"] = listenType;
    QJsonArray payloadArray;
    payloadArray.append(QJsonObject::fromVariantMap(payload));
    root["payload"] = payloadArray;

    QByteArray requestBody = QJsonDocument(root).toJson();
    
    qDebug().noquote() << "[ListenBrainz] API Call:" << listenType << "|" << QString::fromLatin1(requestBody);
    
    QNetworkReply *reply = m_nam->post(request, requestBody);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() -> void {
        reply->deleteLater();
        QByteArray response = reply->readAll();
        if (reply->error() == QNetworkReply::NoError) {
            qDebug().noquote() << "[ListenBrainz] API Response:" << QString::fromLatin1(response);
            // Valid credentials if we got a successful response
            setCredentialsValid(true);
        } else {
            qWarning().noquote() << "[ListenBrainz] API Error:" << reply->errorString() << "|" << QString::fromLatin1(response);
            // Invalid credentials if we got an error
            setCredentialsValid(false);
        }
    });
}

auto StatisticsManager::getMostPlayedUris(int limit) -> QList<QString>
{
    QList<QString> uris;
    QSqlDatabase db = QSqlDatabase::database("QuesterStats");
    if (!db.isOpen()) return uris;

    QSqlQuery query(db);
    query.prepare("SELECT uri, COUNT(*) as cnt FROM play_history WHERE uri IS NOT NULL AND uri != '' GROUP BY uri ORDER BY cnt DESC LIMIT :limit");
    query.bindValue(":limit", limit);

    if (query.exec()) {
        while (query.next()) {
            uris.append(query.value(0).toString());
        }
    }
    return uris;
}

void StatisticsManager::sendQueueAsPlaylist(const QString &playlistName, const QVariantList &tracks)
{
    if (m_lbToken.isEmpty()) {
        emit playlistSaved(false, "Please configure your ListenBrainz token first");
        return;
    }

    if (tracks.isEmpty()) {
        emit playlistSaved(false, "Queue is empty");
        return;
    }

    QUrl url("https://api.listenbrainz.org/1/playlist/create");
    QNetworkRequest request(url);
    request.setRawHeader("Authorization", QString("Token %1").arg(m_lbToken).toUtf8());
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");

    QJsonObject root;
    root["name"] = playlistName;
    root["source"] = "quester";

    QJsonArray itemsArray;
    for (const QVariant &track : tracks) {
        QVariantMap trackMap = track.toMap();
        QJsonObject item;
        item["artist_name"] = trackMap.value("artist", "").toString();
        item["track_name"] = trackMap.value("title", "").toString();
        item["release_name"] = trackMap.value("album", "").toString();
        itemsArray.append(item);
    }
    root["items"] = itemsArray;

    QByteArray requestBody = QJsonDocument(root).toJson();
    
    qDebug().noquote() << "[ListenBrainz] Creating playlist:" << QString::fromLatin1(requestBody);
    
    QNetworkReply *reply = m_nam->post(request, requestBody);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() -> void {
        reply->deleteLater();
        QByteArray response = reply->readAll();
        if (reply->error() == QNetworkReply::NoError) {
            qDebug().noquote() << "[ListenBrainz] Playlist created:" << QString::fromLatin1(response);
            QJsonParseError error;
            QJsonDocument doc = QJsonDocument::fromJson(response, &error);
            if (error.error == QJsonParseError::NoError && doc.isObject()) {
                QJsonObject obj = doc.object();
                QString playlistId = obj["playlist_id"].toString();
                if (!playlistId.isEmpty()) {
                    setPlaylistSaved(true);
                    emit playlistSaved(true, "Playlist created successfully!");
                    return;
                }
            }
            setPlaylistSaved(true);
            emit playlistSaved(true, "Playlist created successfully!");
        } else {
            qWarning().noquote() << "[ListenBrainz] Playlist error:" << reply->errorString() << "|" << QString::fromLatin1(response);
            emit playlistSaved(false, QString("Failed to create playlist: %1").arg(reply->errorString()));
        }
    });
}

void StatisticsManager::savePlaylistToListenBrainz(const QString &playlistName, const QVariantList &tracks)
{
    // Same as sendQueueAsPlaylist - the API creates a new playlist
    sendQueueAsPlaylist(playlistName, tracks);
}

void StatisticsManager::fetchUserPlaylists()
{
    if (m_lbToken.isEmpty() || m_lbUsername.isEmpty()) {
        emit playlistsLoaded(QVariantList());
        return;
    }

    QUrl url(QString("https://api.listenbrainz.org/1/user/%1/playlists").arg(m_lbUsername));
    QNetworkRequest request(url);
    request.setRawHeader("Authorization", QString("Token %1").arg(m_lbToken).toUtf8());
    
    QNetworkReply *reply = m_nam->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() -> void {
        reply->deleteLater();
        QByteArray response = reply->readAll();
        if (reply->error() == QNetworkReply::NoError) {
            qDebug().noquote() << "[ListenBrainz] Playlists loaded:" << QString::fromLatin1(response);
            QJsonParseError error;
            QJsonDocument doc = QJsonDocument::fromJson(response, &error);
            QVariantList playlists;
            
            if (error.error == QJsonParseError::NoError && doc.isObject()) {
                QJsonObject obj = doc.object();
                QJsonArray playlistArray = obj["playlists"].toArray();
                for (const QJsonValue &value : playlistArray) {
                    QJsonObject playlistObj = value.toObject();
                    QJsonObject playlist = playlistObj["playlist"].toObject();
                    QVariantMap playlistMap;
                    playlistMap["name"] = playlist["title"].toString();
                    playlistMap["creator"] = playlist["creator"].toString();
                    playlistMap["url"] = playlist["identifier"].toString();
                    playlistMap["date"] = playlist["date"].toString();
                    playlistMap["track_count"] = playlist["track"].toArray().count();
                    playlists.append(playlistMap);
                }
            }
            emit playlistsLoaded(playlists);
        } else {
            qWarning().noquote() << "[ListenBrainz] Failed to fetch playlists:" << reply->errorString();
            emit playlistsLoaded(QVariantList());
        }
    });
}

// --- Last.fm Scrobbling Implementation ---

void StatisticsManager::setLastfmCredentials(const QString &apiKey, const QString &secret, const QString &sessionKey)
{
    setLastfmCredentialsInternal(apiKey, secret, sessionKey);
}

void StatisticsManager::setLastfmCredentialsInternal(const QString &apiKey, const QString &secret, const QString &sessionKey)
{
    m_lastfmApiKey = apiKey;
    m_lastfmSecret = secret;
    m_lastfmSessionKey = sessionKey;
    
    QSettings settings("Quester", "Quester");
    settings.setValue("lastfmApiKey", apiKey);
    settings.setValue("lastfmSecret", secret);
    settings.setValue("lastfmSessionKey", sessionKey);
    
    if (!sessionKey.isEmpty()) {
        m_lastfmCredentialsValid = true;
        emit lastfmCredentialsValidChanged(true);
    }
}

auto StatisticsManager::generateLastfmSignature(const QMap<QString, QString> &params) -> QString
{
    QString signature;
    QList<QString> keys = params.keys();
    std::ranges::sort(keys);
    for (const QString &key : keys) {
        signature += key + params.value(key);
    }
    return QCryptographicHash::hash(signature.toUtf8(), QCryptographicHash::Md5).toHex();
}

void StatisticsManager::sendLastfmRequest(const QString &method, const QMap<QString, QString> &params)
{
    if (m_lastfmSessionKey.isEmpty() || m_lastfmApiKey.isEmpty()) {
        qWarning() << "[Last.fm] Cannot send request: missing credentials";
        return;
    }

    QUrl url("https://ws.audioscrobbler.com/2.0/");
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/x-www-form-urlencoded");
    
    QMap<QString, QString> postParams = params;
    postParams["method"] = method;
    postParams["api_key"] = m_lastfmApiKey;
    postParams["sk"] = m_lastfmSessionKey;
    
    QMap<QString, QString> sigParams;
    for (auto it = postParams.begin(); it != postParams.end(); ++it) {
        if (it.key() != "api_sig") {
            sigParams[it.key()] = it.value();
        }
    }
    
    QString signature;
    QList<QString> keys = sigParams.keys();
    std::ranges::sort(keys);
    for (const QString &key : keys) {
        signature += key + sigParams.value(key);
    }
    signature += m_lastfmSecret;
    postParams["api_sig"] = QCryptographicHash::hash(signature.toUtf8(), QCryptographicHash::Md5).toHex();
    
    QString postData;
    for (auto it = postParams.begin(); it != postParams.end(); ++it) {
        if (!postData.isEmpty()) postData += "&";
        postData += it.key() + "=" + QUrl::toPercentEncoding(it.value());
    }
    
    qDebug().noquote() << "[Last.fm] Sending request:" << method;
    
    QNetworkReply *reply = m_nam->post(request, postData.toUtf8());
    connect(reply, &QNetworkReply::finished, this, [this, reply, method]() -> void {
        reply->deleteLater();
        QByteArray response = reply->readAll();
        if (reply->error() == QNetworkReply::NoError) {
            qDebug().noquote() << "[Last.fm] Response:" << QString::fromLatin1(response);
        } else {
            qWarning().noquote() << "[Last.fm] Network error:" << reply->errorString();
        }
    });
}

void StatisticsManager::scrobbleToLastfmInternal(const QString &artist, const QString &title, const QString &album, qint64 timestamp)
{
    if (m_lastfmSessionKey.isEmpty()) return;
    
    QMap<QString, QString> params;
    params["artist"] = artist;
    params["track"] = title;
    params["timestamp"] = QString::number(timestamp);
    if (!album.isEmpty() && album != "Unknown Album") {
        params["album"] = album;
    }
    
    sendLastfmRequest("track.scrobble", params);
}

void StatisticsManager::submitLastfmNowPlayingInternal(const QString &artist, const QString &title, const QString &album)
{
    if (m_lastfmSessionKey.isEmpty()) return;
    
    QMap<QString, QString> params;
    params["artist"] = artist;
    params["track"] = title;
    if (!album.isEmpty() && album != "Unknown Album") {
        params["album"] = album;
    }
    
    sendLastfmRequest("track.updateNowPlaying", params);
}

void StatisticsManager::validateLastfmCredentials()
{
    m_lastfmCredentialsValid = !m_lastfmSessionKey.isEmpty();
    emit lastfmCredentialsValidChanged(m_lastfmCredentialsValid);
}

auto StatisticsManager::getLastfmAuthUrl(const QString &token) -> QString
{
    return QString("https://www.last.fm/api/auth/?api_key=%1&token=%2")
        .arg(m_lastfmApiKey).arg(token);
}

void StatisticsManager::getLastfmToken()
{
    if (m_lastfmApiKey.isEmpty()) return;

    QUrl url("https://ws.audioscrobbler.com/2.0/");
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/x-www-form-urlencoded");
    
    QMap<QString, QString> postParams;
    postParams["method"] = "auth.gettoken";
    postParams["api_key"] = m_lastfmApiKey;
    
    QString signature;
    QList<QString> keys = {"api_key", "method"};
    for (const QString &key : keys) {
        signature += key + postParams.value(key);
    }
    signature += m_lastfmSecret;
    postParams["api_sig"] = QCryptographicHash::hash(signature.toUtf8(), QCryptographicHash::Md5).toHex();
    
    QString postData;
    for (auto it = postParams.begin(); it != postParams.end(); ++it) {
        if (!postData.isEmpty()) postData += "&";
        postData += it.key() + "=" + QUrl::toPercentEncoding(it.value());
    }
    
    qDebug() << "[Last.fm] Requesting auth token from Last.fm";
    
    QNetworkReply *reply = m_nam->post(request, postData.toUtf8());
    connect(reply, &QNetworkReply::finished, this, [this, reply]() -> void {
        reply->deleteLater();
        QByteArray response = reply->readAll();
        qDebug() << "[Last.fm] Auth token response:" << QString::fromLatin1(response);
        
        if (reply->error() == QNetworkReply::NoError) {

            QXmlStreamReader reader(response);

            QString token;

            bool statusOk = false;

            while (!reader.atEnd()) {

                reader.readNext();

                if (reader.isStartElement()) {

                    if (reader.name() == "lfm") {

                        if (reader.attributes().value("status") == "ok") {

                            statusOk = true;

                        }

                    } else if (reader.name() == "token" && statusOk) {

                        token = reader.readElementText();

                        break;

                    }

                }

            }

            if (reader.hasError()) {

                qWarning() << "[Last.fm] Failed to parse XML token response:" << reader.errorString();

            } else if (statusOk && !token.isEmpty()) {

                qDebug() << "[Last.fm] Got token:" << token;

                QString authUrl = getLastfmAuthUrl(token);

                qDebug() << "[Last.fm] Opening auth URL:" << authUrl;

                emit lastfmAuthTokenReceived(token, authUrl);

                QDesktopServices::openUrl(QUrl(authUrl));

            } else {

                qWarning() << "[Last.fm] Bad response status or no token";

            }

        } else {

            qWarning() << "[Last.fm] Token request error:" << reply->errorString();

        }
    });
}

void StatisticsManager::getLastfmSessionKey(const QString &token)
{
    if (m_lastfmApiKey.isEmpty() || m_lastfmSecret.isEmpty() || token.isEmpty()) return;

    QUrl url("https://ws.audioscrobbler.com/2.0/");
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/x-www-form-urlencoded");
    
    QMap<QString, QString> postParams;
    postParams["method"] = "auth.getsession";
    postParams["api_key"] = m_lastfmApiKey;
    postParams["token"] = token;
    
    QString signature;
    QList<QString> keys = {"api_key", "method", "token"};
    for (const QString &key : keys) {
        signature += key + postParams.value(key);
    }
    signature += m_lastfmSecret;
    postParams["api_sig"] = QCryptographicHash::hash(signature.toUtf8(), QCryptographicHash::Md5).toHex();
    
    QString postData;
    for (auto it = postParams.begin(); it != postParams.end(); ++it) {
        if (!postData.isEmpty()) postData += "&";
        postData += it.key() + "=" + QUrl::toPercentEncoding(it.value());
    }
    
    qDebug() << "[Last.fm] Requesting session key from Last.fm with token:" << token;
    
    QNetworkReply *reply = m_nam->post(request, postData.toUtf8());
    connect(reply, &QNetworkReply::finished, this, [this, reply]() -> void {
        reply->deleteLater();

        QByteArray response = reply->readAll();

        qDebug() << "[Last.fm] Session key response:" << QString::fromLatin1(response);

        if (reply->error() == QNetworkReply::NoError) {

            QXmlStreamReader reader(response);

            QString sessionKey;

            QString username;

            bool statusOk = false;

            while (!reader.atEnd()) {

                reader.readNext();

                if (reader.isStartElement()) {

                    if (reader.name() == "lfm") {

                        if (reader.attributes().value("status") == "ok") {

                            statusOk = true;

                        }

                    } else if (reader.name() == "key" && statusOk) {

                        sessionKey = reader.readElementText();

                    } else if (reader.name() == "name" && statusOk) {

                        username = reader.readElementText();

                    }

                }

            }

            if (reader.hasError()) {

                qWarning() << "[Last.fm] Failed to parse XML session response:" << reader.errorString();

            } else if (statusOk && !sessionKey.isEmpty()) {

                setLastfmCredentialsInternal(m_lastfmApiKey, m_lastfmSecret, sessionKey);

                m_lastfmUsername = username;

                emit lastfmUsernameChanged();

                qDebug() << "[Last.fm] Authenticated as:" << username << "with session key:" << sessionKey;

            } else {

                qWarning() << "[Last.fm] Bad response status or no session key";

            }

        } else {

            qWarning() << "[Last.fm] Session key request error:" << reply->errorString();

        }
    });
}

// --- External Activity Data for Wrapped ---

void StatisticsManager::fetchExternalActivityData(const QString &period)
{
    qDebug().noquote() << "[Activity] Fetching external data for period:" << period;
    
    // Clear previous data
    m_externalActivityData = QVariantMap();
    
    // Fetch from both services in parallel
    fetchListenBrainzStats(period);
    fetchLastfmStats(period);
}

void StatisticsManager::fetchListenBrainzStats(const QString &period)
{
    if (m_lbToken.isEmpty() || m_lbUsername.isEmpty()) {
        qDebug().noquote() << "[ListenBrainz] No credentials for stats";
        return;
    }

    // Calculate time range based on period
    qint64 now = QDateTime::currentSecsSinceEpoch();
    qint64 startTime = 0;
    
    if (period == "weekly") {
        startTime = now - (7 * 24 * 60 * 60);
    } else if (period == "monthly") {
        startTime = now - (30 * 24 * 60 * 60);
    } else if (period == "yearly") {
        startTime = now - (365 * 24 * 60 * 60);
    }
    
    // Use ListenBrainz stats API
    QUrl url(QString("https://api.listenbrainz.org/1/stats/user/%1/ listening-range?start=%2&end=%3")
        .arg(m_lbUsername)
        .arg(startTime)
        .arg(now));
    
    QNetworkRequest request(url);
    request.setRawHeader("Authorization", QString("Token %1").arg(m_lbToken).toUtf8());
    
    QNetworkReply *reply = m_nam->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() -> void {
        reply->deleteLater();
        
        if (reply->error() != QNetworkReply::NoError) {
            qWarning().noquote() << "[ListenBrainz] Stats fetch error:" << reply->errorString();
            return;
        }
        
        QByteArray response = reply->readAll();
        QJsonParseError error;
        QJsonDocument doc = QJsonDocument::fromJson(response, &error);
        
        if (error.error != QJsonParseError::NoError) {
            qWarning().noquote() << "[ListenBrainz] Stats parse error:" << error.errorString();
            return;
        }
        
        QJsonObject root = doc.object();
        QVariantMap lbStats;
        
        // Extract listening range info
        if (root.contains("listening_range")) {
            QJsonObject range = root["listening_range"].toObject();
            lbStats["start"] = range["start_ts"].toVariant().toLongLong();
            lbStats["end"] = range["end_ts"].toVariant().toLongLong();
        }
        
        // Get top artists
        QUrl artistsUrl(QString("https://api.listenbrainz.org/1/stats/user/%1/top-artists?count=5").arg(m_lbUsername));
        QNetworkRequest artistsRequest(artistsUrl);
        artistsRequest.setRawHeader("Authorization", QString("Token %1").arg(m_lbToken).toUtf8());
        
        QNetworkReply *artistsReply = m_nam->get(artistsRequest);
        connect(artistsReply, &QNetworkReply::finished, this, [this, artistsReply, lbStats]() -> void {
            artistsReply->deleteLater();
            
            if (artistsReply->error() == QNetworkReply::NoError) {
                QByteArray response = artistsReply->readAll();
                QJsonParseError error;
                QJsonDocument doc = QJsonDocument::fromJson(response, &error);
                
                if (error.error == QJsonParseError::NoError) {
                    QVariantMap stats = lbStats;
                    QJsonArray topArtists = doc.object()["top_artists"].toArray();
                    QVariantList artists;
                    
                    for (const QJsonValue &value : topArtists) {
                        QJsonObject artist = value.toObject();
                        QVariantMap artistMap;
                        artistMap["name"] = artist["artist_name"].toString();
                        artistMap["listen_count"] = artist["listen_count"].toVariant().toLongLong();
                        artists.append(artistMap);
                    }
                    
                    stats["lb_top_artists"] = artists;
                    m_externalActivityData["listenbrainz"] = stats;
                    emit externalActivityDataChanged();
                    
                    qDebug().noquote() << "[ListenBrainz] Got" << artists.count() << "top artists";
                }
            }
        });
    });
}

void StatisticsManager::fetchLastfmStats(const QString &period)
{
    if (m_lastfmSessionKey.isEmpty() || m_lastfmApiKey.isEmpty()) {
        qDebug().noquote() << "[Last.fm] No credentials for stats";
        return;
    }
    
    if (m_lastfmUsername.isEmpty()) {
        qDebug().noquote() << "[Last.fm] No username for stats";
        return;
    }
    
    // Calculate time range
    qint64 now = QDateTime::currentSecsSinceEpoch();
    qint64 from = 0;
    
    if (period == "weekly") {
        from = now - (7 * 24 * 60 * 60);
    } else if (period == "monthly") {
        from = now - (30 * 24 * 60 * 60);
    } else if (period == "yearly") {
        from = now - (365 * 24 * 60 * 60);
    }
    
    // Get recent tracks (includes play counts)
    QUrl url("https://ws.audioscrobbler.com/2.0/");
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/x-www-form-urlencoded");
    
    QMap<QString, QString> postParams;
    postParams["method"] = "user.getRecentTracks";
    postParams["user"] = m_lastfmUsername;
    postParams["api_key"] = m_lastfmApiKey;
    postParams["limit"] = "100";
    postParams["from"] = QString::number(from);
    postParams["to"] = QString::number(now);
    
    // Generate signature
    QString signature;
    QList<QString> keys = {"api_key", "from", "method", "to", "user"};
    for (const QString &key : keys) {
        signature += key + postParams.value(key);
    }
    signature += m_lastfmSecret;
    postParams["api_sig"] = QCryptographicHash::hash(signature.toUtf8(), QCryptographicHash::Md5).toHex();
    
    QString postData;
    for (auto it = postParams.begin(); it != postParams.end(); ++it) {
        if (!postData.isEmpty()) postData += "&";
        postData += it.key() + "=" + QUrl::toPercentEncoding(it.value());
    }
    
    QNetworkReply *reply = m_nam->post(request, postData.toUtf8());
    connect(reply, &QNetworkReply::finished, this, [this, reply]() -> void {
        reply->deleteLater();
        
        if (reply->error() != QNetworkReply::NoError) {
            qWarning().noquote() << "[Last.fm] Recent tracks error:" << reply->errorString();
            return;
        }
        
        QByteArray response = reply->readAll();
        QJsonParseError error;
        QJsonDocument doc = QJsonDocument::fromJson(response, &error);
        
        if (error.error != QJsonParseError::NoError) {
            qWarning().noquote() << "[Last.fm] Recent tracks parse error:" << error.errorString();
            return;
        }
        
        QJsonObject root = doc.object();
        QVariantMap stats;
        stats["total_tracks"] = root["recenttracks"].toObject()["@attr"].toObject()["total"].toVariant().toLongLong();
        
        // Process top tracks from recent tracks
        QJsonArray tracks = root["recenttracks"].toObject()["track"].toArray();
        QMap<QString, int> trackCounts;
        QMap<QString, QString> trackArtists;
        
        for (const QJsonValue &value : tracks) {
            QJsonObject track = value.toObject();
            QString name = track["name"].toString();
            QString artist = track["artist"].toObject()["#text"].toString();
            trackCounts[name]++;
            if (!trackArtists.contains(name)) {
                trackArtists[name] = artist;
            }
        }
        
        // Sort and get top 5
        QVariantList topTracks;
        auto it = trackCounts.begin();
        for (int i = 0; i < qMin(5, trackCounts.size()); ++i, ++it) {
            QVariantMap track;
            track["title"] = it.key();
            track["artist"] = trackArtists[it.key()];
            track["play_count"] = it.value();
            topTracks.append(track);
        }
        
        stats["top_tracks"] = topTracks;
        m_externalActivityData["lastfm"] = stats;
        emit externalActivityDataChanged();
        
        qDebug().noquote() << "[Last.fm] Got" << topTracks.count() << "top tracks, total plays:" << stats["total_tracks"].toLongLong();
    });
}

void StatisticsManager::startLastfmAuth() {
    qDebug() << "[Last.fm] Starting Last.fm authentication";
    getLastfmToken();
}

void StatisticsManager::completeLastfmAuth(const QString &token) {
    qDebug() << "[Last.fm] Completing Last.fm authentication with token:" << token;
    getLastfmSessionKey(token);
}

