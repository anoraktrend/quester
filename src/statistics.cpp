#include "statistics.h"
#include "quester.h"
#include <QSqlDatabase>
#include <QMutex>
#include <QStandardPaths>
#include <QDir>
#include <QSqlQuery>
#include <QSqlError>
#include <QDateTime>
#include <QPainter>
#include <QPainterPath>
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

    // Submit to Last.fm (Scrobble) - only if played for at least 30 seconds
    if (!m_lastfmSessionKey.isEmpty() && durationMs >= 30000) {
        qDebug() << "[Last.fm] Scrobbling:" << artist << "-" << title << "(" << album << ")";
        qint64 timestamp = QDateTime::currentSecsSinceEpoch() - (durationMs / 1000);
        scrobbleToLastfmInternal(artist, title, album, timestamp);
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

// Helper: draw a rounded-rect clipped image (album art tile)
static void drawArtTile(QPainter &p, const QImage &art, const QRect &rect, int radius = 16)
{
    if (art.isNull()) return;

    p.save();
    p.setRenderHint(QPainter::Antialiasing);
    p.setRenderHint(QPainter::SmoothPixmapTransform);

    QPainterPath clipPath;
    clipPath.addRoundedRect(rect, radius, radius);
    p.setClipPath(clipPath);

    QImage scaled = art.scaled(rect.size(), Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
    QRect srcRect((scaled.width() - rect.width()) / 2, (scaled.height() - rect.height()) / 2, rect.width(), rect.height());
    
    p.drawImage(rect, scaled, srcRect);
    
    p.restore();
}

// Helper: draw a circle-clipped image (artist portrait)
static void drawCircleArt(QPainter &p, const QImage &art, const QPoint &center, int diameter)
{
    if (art.isNull()) return;

    p.save();
    p.setRenderHint(QPainter::Antialiasing);
    p.setRenderHint(QPainter::SmoothPixmapTransform);

    QRect rect(center.x() - diameter / 2, center.y() - diameter / 2, diameter, diameter);
    QPainterPath clipPath;
    clipPath.addEllipse(rect);
    p.setClipPath(clipPath);

    QImage scaled = art.scaled(rect.size(), Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
    QRect srcRect((scaled.width() - rect.width()) / 2, (scaled.height() - rect.height()) / 2, rect.width(), rect.height());
    
    p.drawImage(rect, scaled, srcRect);

    p.restore();
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
    const int w = 1080;
    const int h = 1920;
    QImage img(w, h, QImage::Format_ARGB32);
    img.fill(QColor("#121212")); // Dark background

    QPainter p(&img);
    p.setRenderHint(QPainter::Antialiasing);
    p.setRenderHint(QPainter::TextAntialiasing);
    p.setRenderHint(QPainter::SmoothPixmapTransform);

    const QColor accentColor("#BB86FC");
    const QColor accentDim("#7C4DFF");
    const QColor textColor("#FFFFFF");
    const QColor dimColor("#B0B0B0");
    const QColor cardBg("#1E1E1E");

    // ── Header ────────────────────────────────────────────────────────────────
    p.setPen(textColor);
    p.setFont(QFont("Sans Serif", 72, QFont::Bold));
    p.drawText(QRect(50, 80, w - 100, 140), Qt::AlignCenter, titleText);

    p.setPen(accentColor);
    p.setFont(QFont("Sans Serif", 36));
    p.drawText(QRect(50, 220, w - 100, 70), Qt::AlignCenter, subTitle);

    int y = 340;

    // ── Total Time card ───────────────────────────────────────────────────────
    {
        long long totalMs = stats["totalMs"].toLongLong();
        double hours = totalMs / 1000.0 / 3600.0;
        QString timePhrase = hours > 100 ? "Do you even sleep?" : (hours > 10 ? "Music is life." : "Just getting started.");

        QRect cardRect(50, y, w - 100, 160);
        p.setBrush(cardBg);
        p.setPen(Qt::NoPen);
        p.drawRoundedRect(cardRect, 20, 20);

        p.setPen(dimColor);
        p.setFont(QFont("Sans Serif", 26));
        p.drawText(QRect(70, y + 18, cardRect.width() - 40, 40), Qt::AlignLeft | Qt::AlignVCenter, "You listened for");

        p.setPen(accentColor);
        p.setFont(QFont("Sans Serif", 60, QFont::Bold));
        p.drawText(QRect(70, y + 55, cardRect.width() - 40, 80), Qt::AlignLeft | Qt::AlignVCenter,
                   QString::number(hours, 'f', 1) + " hrs");

        p.setPen(dimColor);
        p.setFont(QFont("Sans Serif", 22, -1, true));
        p.drawText(QRect(70, y + 125, cardRect.width() - 40, 32), Qt::AlignLeft | Qt::AlignVCenter, timePhrase);

        y += 185;
    }

    // ── Top Artist section ────────────────────────────────────────────────────
    {
        QVariantList artists = stats["topArtists"].toList();
        if (!artists.isEmpty()) {
            // Section label
            p.setPen(dimColor);
            p.setFont(QFont("Sans Serif", 26, QFont::Bold));
            p.drawText(QRect(50, y, w - 100, 44), Qt::AlignLeft, "TOP ARTIST");
            y += 54;

            QRect cardRect(50, y, w - 100, 200);
            p.setBrush(cardBg);
            p.setPen(Qt::NoPen);
            p.drawRoundedRect(cardRect, 20, 20);

            // Artist image (circle)
            QString topArtistName = artists.first().toMap()["name"].toString();
            QString artistImgPath;
            {
                QString cacheDir = QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + "/artist_images/";
                QByteArray hash = QCryptographicHash::hash(topArtistName.toUtf8(), QCryptographicHash::Md5).toHex();
                QString candidate = cacheDir + hash + ".jpg";
                if (QFile::exists(candidate)) artistImgPath = candidate;
            }

            const int circleDia = 160;
            const int circleCx = 50 + circleDia / 2 + 20; // left padding inside card
            const int circleCy = y + cardRect.height() / 2;

            if (!artistImgPath.isEmpty()) {
                QImage art(artistImgPath);
                drawCircleArt(p, art, QPoint(circleCx, circleCy), circleDia);
            } else {
                // Placeholder circle
                p.setBrush(accentDim);
                p.setPen(Qt::NoPen);
                p.drawEllipse(QPoint(circleCx, circleCy), circleDia / 2, circleDia / 2);
            }

            // Artist name & listen time
            int textX = circleCx + circleDia / 2 + 30;
            int textW = cardRect.right() - textX - 20;

            p.setPen(textColor);
            p.setFont(QFont("Sans Serif", 40, QFont::Bold));
            // Elide if too long
            QFontMetrics fm(p.font());
            QString elided = fm.elidedText(topArtistName, Qt::ElideRight, textW);
            p.drawText(QRect(textX, y + 50, textW, 60), Qt::AlignLeft | Qt::AlignVCenter, elided);

            long long artistMs = artists.first().toMap()["ms"].toLongLong();
            p.setPen(accentColor);
            p.setFont(QFont("Sans Serif", 28));
            p.drawText(QRect(textX, y + 118, textW, 44), Qt::AlignLeft | Qt::AlignVCenter,
                       QString::number(artistMs / 60000) + " min played");

            y += cardRect.height() + 30;
        }
    }

    // ── Top Albums collage ────────────────────────────────────────────────────
    {
        QVariantList albums = stats["topAlbums"].toList();
        if (!albums.isEmpty()) {
            p.setPen(dimColor);
            p.setFont(QFont("Sans Serif", 26, QFont::Bold));
            p.drawText(QRect(50, y, w - 100, 44), Qt::AlignLeft, "TOP ALBUMS");
            y += 54;

            // Up to 3 album tiles in a row, then album name + time below #1
            const int maxAlbums = qMin(3, albums.size());
            const int gap = 20;
            const int tileW = (w - 100 - gap * (maxAlbums - 1)) / maxAlbums;
            const int tileH = tileW; // square

            for (int i = 0; i < maxAlbums; ++i) {
                QVariantMap albumMap = albums[i].toMap();
                QString albumName = albumMap["name"].toString();
                QString artistName = albumMap["artist"].toString();

                QRect tileRect(50 + i * (tileW + gap), y, tileW, tileH);

                // Try to load cached art
                QString artPath = MpdClient::getCachePath(artistName, albumName);
                QImage art(artPath);

                if (!art.isNull()) {
                    drawArtTile(p, art, tileRect, 14);
                } else {
                    // Placeholder
                    p.setBrush(cardBg);
                    p.setPen(Qt::NoPen);
                    p.drawRoundedRect(tileRect, 14, 14);
                    // Draw a music note placeholder text
                    p.setPen(dimColor);
                    p.setFont(QFont("Sans Serif", 48));
                    p.drawText(tileRect, Qt::AlignCenter, "♪");
                }

                // Rank badge
                p.setBrush(QColor(0, 0, 0, 180));
                p.setPen(Qt::NoPen);
                QRect badge(tileRect.x() + 8, tileRect.y() + 8, 40, 40);
                p.drawRoundedRect(badge, 8, 8);
                p.setPen(accentColor);
                p.setFont(QFont("Sans Serif", 22, QFont::Bold));
                p.drawText(badge, Qt::AlignCenter, QString::number(i + 1));
            }

            y += tileH + 16;

            // Album name and artist below the first tile
            if (!albums.isEmpty()) {
                QVariantMap topAlbum = albums.first().toMap();
                p.setPen(textColor);
                p.setFont(QFont("Sans Serif", 30, QFont::Bold));
                QFontMetrics fm(p.font());
                QString elided = fm.elidedText(topAlbum["name"].toString(), Qt::ElideRight, w - 100);
                p.drawText(QRect(50, y, w - 100, 46), Qt::AlignLeft, elided);
                y += 46;
                p.setPen(dimColor);
                p.setFont(QFont("Sans Serif", 24));
                p.drawText(QRect(50, y, w - 100, 36), Qt::AlignLeft,
                           topAlbum["artist"].toString() + " · " +
                           QString::number(topAlbum["ms"].toLongLong() / 60000) + " min");
                y += 50;
            }
        }
    }

    // ── Top Tracks list ───────────────────────────────────────────────────────
    {
        QVariantList tracks = stats["topTracks"].toList();
        if (!tracks.isEmpty()) {
            p.setPen(dimColor);
            p.setFont(QFont("Sans Serif", 26, QFont::Bold));
            p.drawText(QRect(50, y, w - 100, 44), Qt::AlignLeft, "TOP TRACKS");
            y += 54;

            const int maxTracks = qMin(3, tracks.size());
            const int rowH = 90;
            const int thumbSz = 70;

            for (int i = 0; i < maxTracks; ++i) {
                QVariantMap track = tracks[i].toMap();
                QString title = track["title"].toString();
                QString artist = track["artist"].toString();

                QRect rowRect(50, y, w - 100, rowH);
                p.setBrush(cardBg);
                p.setPen(Qt::NoPen);
                p.drawRoundedRect(rowRect, 14, 14);

                // Rank number
                p.setPen(accentColor);
                p.setFont(QFont("Sans Serif", 28, QFont::Bold));
                p.drawText(QRect(rowRect.x() + 12, y, 44, rowH), Qt::AlignCenter, QString::number(i + 1));

                // Try to find album art for the artist (artist image as proxy)
                QString artistImgPath;
                {
                    QString cacheDir = QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + "/artist_images/";
                    QByteArray hash = QCryptographicHash::hash(artist.toUtf8(), QCryptographicHash::Md5).toHex();
                    QString candidate = cacheDir + hash + ".jpg";
                    if (QFile::exists(candidate)) artistImgPath = candidate;
                }

                QRect thumbRect(rowRect.x() + 64, y + (rowH - thumbSz) / 2, thumbSz, thumbSz);
                if (!artistImgPath.isEmpty()) {
                    QImage art(artistImgPath);
                    drawArtTile(p, art, thumbRect, 10);
                } else {
                    p.setBrush(accentDim);
                    p.setPen(Qt::NoPen);
                    p.drawRoundedRect(thumbRect, 10, 10);
                }

                // Track title & artist
                int textX = thumbRect.right() + 16;
                int textW = rowRect.right() - textX - 12;
                p.setPen(textColor);
                p.setFont(QFont("Sans Serif", 26, QFont::Bold));
                QFontMetrics fmTitle(p.font());
                p.drawText(QRect(textX, y + 12, textW, 36),
                           Qt::AlignLeft | Qt::AlignVCenter,
                           fmTitle.elidedText(title, Qt::ElideRight, textW));
                p.setPen(dimColor);
                p.setFont(QFont("Sans Serif", 22));
                QFontMetrics fmArtist(p.font());
                p.drawText(QRect(textX, y + 50, textW, 32),
                           Qt::AlignLeft | Qt::AlignVCenter,
                           fmArtist.elidedText(artist, Qt::ElideRight, textW));

                y += rowH + 10;
            }
            y += 10;
        }
    }

    // ── Activity Graph ────────────────────────────────────────────────────────
    {
        int maxVal = 0;
        QList<int> graphData = getActivityGraphData(period, maxVal);

        if (maxVal > 0 && !graphData.isEmpty()) {
            // Pin graph to bottom area; leave room if y is still above it
            const int graphH = 180;
            const int graphW = w - 140;
            const int graphX = 70;
            const int graphY = h - graphH - 120;

            if (y < graphY - 60) {
                p.setPen(dimColor);
                p.setFont(QFont("Sans Serif", 24, QFont::Bold));
                p.drawText(QRect(graphX, graphY - 54, graphW, 40), Qt::AlignCenter, "LISTENING ACTIVITY");

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
            }
        }
    }

    // ── Footer ────────────────────────────────────────────────────────────────
    p.setPen(QColor("#555555"));
    p.setFont(QFont("Sans Serif", 22));
    p.drawText(QRect(50, h - 80, w - 100, 50), Qt::AlignCenter, "Quester · Your Music, Your Story");

    QString fileName = QString("QuesterWrapped_%1_%2.png")
                           .arg(period, QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss"));
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



auto StatisticsManager::artistImageUrl(const QString &artist) -> QString
{
    if (artist.isEmpty())
        return {};
    QString cacheDir = QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + "/artist_images/";
    QByteArray hashName = QCryptographicHash::hash(artist.toUtf8(), QCryptographicHash::Md5).toHex();
    QString path = cacheDir + hashName + ".jpg";
    if (QFile::exists(path))
        return QStringLiteral("file://") + path;
    return {};
}

void StatisticsManager::setListenBrainzCredentials(const QString &token, const QString &username)
{
    m_lbToken = token;
    m_lbUsername = username;
}

void StatisticsManager::submitPlayingNow(const QString &artist, const QString &title, const QString &album, qint64 durationMs)
{
    // Submit to ListenBrainz
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
        
        sendListenBrainzRequest("playing_now", payload);
    }

    // Submit to Last.fm (Now Playing)
    if (!m_lastfmSessionKey.isEmpty()) {
        qDebug() << "[Last.fm] Now Playing:" << artist << "-" << title << "(" << album << ")";
        submitLastfmNowPlayingInternal(artist, title, album);
    }
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
            qDebug().noquote() << "[ListenBrainz] Token validation response:" << QString::fromUtf8(response);
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
    
    qDebug().noquote() << "[ListenBrainz] API Call:" << listenType << "|" << QString::fromUtf8(requestBody);
    
    QNetworkReply *reply = m_nam->post(request, requestBody);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() -> void {
        reply->deleteLater();
        QByteArray response = reply->readAll();
        if (reply->error() == QNetworkReply::NoError) {
            qDebug().noquote() << "[ListenBrainz] API Response:" << QString::fromUtf8(response);
            // Valid credentials if we got a successful response
            setCredentialsValid(true);
        } else {
            qWarning().noquote() << "[ListenBrainz] API Error:" << reply->errorString() << "|" << QString::fromUtf8(response);
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
    
    qDebug().noquote() << "[ListenBrainz] Creating playlist:" << QString::fromUtf8(requestBody);
    
    QNetworkReply *reply = m_nam->post(request, requestBody);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() -> void {
        reply->deleteLater();
        QByteArray response = reply->readAll();
        if (reply->error() == QNetworkReply::NoError) {
            qDebug().noquote() << "[ListenBrainz] Playlist created:" << QString::fromUtf8(response);
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
            qWarning().noquote() << "[ListenBrainz] Playlist error:" << reply->errorString() << "|" << QString::fromUtf8(response);
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
            qDebug().noquote() << "[ListenBrainz] Playlists loaded:" << QString::fromUtf8(response);
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
    
    bool isValid = !sessionKey.isEmpty();
    if (m_lastfmCredentialsValid != isValid) {
        m_lastfmCredentialsValid = isValid;
        emit lastfmCredentialsValidChanged(isValid);
    }
    
    // Clear username if session is invalid
    if (!isValid) {
        m_lastfmUsername.clear();
        emit lastfmUsernameChanged();
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
    
    qDebug().noquote() << "[Last.fm] API Call:" << method << "|" << postData;
    
    QNetworkReply *reply = m_nam->post(request, postData.toUtf8());
    connect(reply, &QNetworkReply::finished, this, [this, reply, method]() -> void {
        reply->deleteLater();
        QByteArray response = reply->readAll();
        if (reply->error() == QNetworkReply::NoError) {
            qDebug().noquote() << "[Last.fm] API Response:" << QString::fromUtf8(response);
        } else {
            qWarning().noquote() << "[Last.fm] API Error:" << reply->errorString() << "|" << QString::fromUtf8(response);
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
    
    qDebug().noquote() << "[Last.fm] API Call: auth.gettoken |" << postData;
    
    QNetworkReply *reply = m_nam->post(request, postData.toUtf8());
    connect(reply, &QNetworkReply::finished, this, [this, reply]() -> void {
        reply->deleteLater();
        QByteArray response = reply->readAll();
        if (reply->error() == QNetworkReply::NoError) {
            qDebug().noquote() << "[Last.fm] API Response: auth.gettoken |" << QString::fromUtf8(response);
        } else {
            qWarning().noquote() << "[Last.fm] API Error: auth.gettoken |" << reply->errorString() << "|" << QString::fromUtf8(response);
        }
        
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
                
                // Automatically complete authentication after token is received
                // This eliminates the need for the "Complete Last.fm Auth" button
                QTimer::singleShot(10000, this, [this, token]() {
                    qDebug() << "[Last.fm] Automatically completing authentication with token:" << token;
                    getLastfmSessionKey(token);
                });

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
    
    qDebug().noquote() << "[Last.fm] API Call: auth.getsession |" << postData;
    
    QNetworkReply *reply = m_nam->post(request, postData.toUtf8());
    connect(reply, &QNetworkReply::finished, this, [this, reply]() -> void {
        reply->deleteLater();
        QByteArray response = reply->readAll();
        if (reply->error() == QNetworkReply::NoError) {
            qDebug().noquote() << "[Last.fm] API Response: auth.getsession |" << QString::fromUtf8(response);
        } else {
            qWarning().noquote() << "[Last.fm] API Error: auth.getsession |" << reply->errorString() << "|" << QString::fromUtf8(response);
        }

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
    QUrl url(QString("https://api.listenbrainz.org/1/stats/user/%1/listening-range?start=%2&end=%3")
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
    
    qDebug().noquote() << "[Last.fm] API Call: user.getRecentTracks |" << postData;
    
    QNetworkReply *reply = m_nam->post(request, postData.toUtf8());
    connect(reply, &QNetworkReply::finished, this, [this, reply]() -> void {
        reply->deleteLater();
        
        if (reply->error() != QNetworkReply::NoError) {
            qWarning().noquote() << "[Last.fm] API Error: user.getRecentTracks |" << reply->errorString();
            return;
        }
        
        QByteArray response = reply->readAll();
        qDebug().noquote() << "[Last.fm] API Response: user.getRecentTracks |" << QString::fromUtf8(response);
        
        // Parse XML response
        QXmlStreamReader reader(response);
        QVariantMap stats;
        
        QMap<QString, int> trackCounts;
        QMap<QString, QString> trackArtists;
        bool statusOk = false;
        QString totalPages;
        
        while (!reader.atEnd()) {
            reader.readNext();
            
            if (reader.isStartElement()) {
                if (reader.name() == "lfm") {
                    statusOk = reader.attributes().value("status") == "ok";
                } else if (reader.name() == "recenttracks" && statusOk) {
                    // Get total tracks count from attributes
                    QString total = reader.attributes().value("total").toString();
                    if (!total.isEmpty()) {
                        stats["total_tracks"] = total.toLongLong();
                    }
                } else if (reader.name() == "track" && statusOk) {
                    QString trackName;
                    QString artistName;
                    
                    while (!reader.atEnd()) {
                        reader.readNext();
                        
                        if (reader.isStartElement()) {
                            if (reader.name() == "name") {
                                trackName = reader.readElementText();
                            } else if (reader.name() == "artist") {
                                // Artist name is in <artist><name>...</name></artist> or <artist>#text="..."</artist>
                                if (reader.attributes().hasAttribute("#text")) {
                                    artistName = reader.attributes().value("#text").toString();
                                } else {
                                    // Look for <name> inside <artist>
                                    while (!reader.atEnd()) {
                                        reader.readNext();
                                        if (reader.isStartElement() && reader.name() == "name") {
                                            artistName = reader.readElementText();
                                            break;
                                        } else if (reader.isEndElement() && reader.name() == "artist") {
                                            break;
                                        }
                                    }
                                }
                            }
                        } else if (reader.isEndElement() && reader.name() == "track") {
                            break;
                        }
                    }
                    
                    if (!trackName.isEmpty()) {
                        trackCounts[trackName]++;
                        if (!trackArtists.contains(trackName)) {
                            trackArtists[trackName] = artistName;
                        }
                    }
                }
            }
        }
        
        if (reader.hasError()) {
            qWarning().noquote() << "[Last.fm] XML parse error:" << reader.errorString();
            return;
        }
        
        if (!statusOk) {
            qWarning().noquote() << "[Last.fm] API response status not OK";
            return;
        }
        
        // Process top tracks from recent tracks
        QVariantList topTracks;
        
        // Convert to list and sort by count descending
        QList<QPair<QString, int>> trackCountList;
        for (auto it = trackCounts.begin(); it != trackCounts.end(); ++it) {
            trackCountList.append(qMakePair(it.key(), it.value()));
        }
        
        std::sort(trackCountList.begin(), trackCountList.end(), 
            [](const QPair<QString, int>& a, const QPair<QString, int>& b) {
                return a.second > b.second;
            });
        
        // Get top 5 tracks
        int limit = qMin(5, trackCountList.size());
        for (int i = 0; i < limit; ++i) {
            QVariantMap track;
            track["title"] = trackCountList[i].first;
            track["artist"] = trackArtists[trackCountList[i].first];
            track["play_count"] = trackCountList[i].second;
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
