#ifndef MPRISCLIENT_H
#define MPRISCLIENT_H

#include <QObject>
#include <QString>
#include <QDBusAbstractAdaptor>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusVariant>
#include <QVariantMap>
#include <QVariantList>
#include <QTimer>
#include <QtDBus>
#include <QtCore>
#include "quester.h"

class MprisClient : public QObject
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.mpris.MediaPlayer2.quester")
    Q_CLASSINFO("D-Bus Introspection",
        "  <interface name=\"org.mpris.MediaPlayer2\">\n"
        "    <property access=\"read\" type=\"b\" name=\"CanQuit\"/>\n"
        "    <property access=\"read\" type=\"b\" name=\"CanSetFullscreen\"/>\n"
        "    <property access=\"read\" type=\"b\" name=\"Fullscreen\"/>\n"
        "    <property access=\"read\" type=\"b\" name=\"CanRaise\"/>\n"
        "    <property access=\"read\" type=\"s\" name=\"Identity\"/>\n"
        "    <property access=\"read\" type=\"as\" name=\"SupportedUriSchemes\"/>\n"
        "    <property access=\"read\" type=\"as\" name=\"SupportedMimeTypes\"/>\n"
        "    <method name=\"Quit\"/>\n"
        "    <method name=\"Raise\"/>\n"
        "  </interface>\n"
        "  <interface name=\"org.mpris.MediaPlayer2.Player\">\n"
        "    <property access=\"read\" type=\"b\" name=\"CanGoNext\"/>\n"
        "    <property access=\"read\" type=\"b\" name=\"CanGoPrevious\"/>\n"
        "    <property access=\"read\" type=\"b\" name=\"CanPlay\"/>\n"
        "    <property access=\"read\" type=\"b\" name=\"CanPause\"/>\n"
        "    <property access=\"read\" type=\"b\" name=\"CanSeek\"/>\n"
        "    <property access=\"read\" type=\"b\" name=\"CanControl\"/>\n"
        "    <property access=\"r\" type=\"d\" name=\"Rate\"/>\n"
        "    <property access=\"r\" type=\"d\" name=\"MinimumRate\"/>\n"
        "    <property access=\"r\" type=\"d\" name=\"MaximumRate\"/>\n"
        "    <property access=\"r\" type=\"s\" name=\"Shuffle\"/>\n"
        "    <property access=\"r\" type=\"s\" name=\"LoopStatus\"/>\n"
        "    <property access=\"r\" type=\"(ss)\" name=\"Metadata\"/>\n"
        "    <property access=\"r\" type=\"d\" name=\"Volume\"/>\n"
        "    <property access=\"r\" type=\"d\" name=\"Position\"/>\n"
        "    <property access=\"r\" type=\"s\" name=\"PlaybackStatus\"/>\n"
        "    <method name=\"Next\"/>\n"
        "    <method name=\"Previous\"/>\n"
        "    <method name=\"Pause\"/>\n"
        "    <method name=\"PlayPause\"/>\n"
        "    <method name=\"Stop\"/>\n"
        "    <method name=\"Play\"/>\n"
        "    <method name=\"Seek\"/>\n"
        "    <method name=\"SetPosition\"/>\n"
        "    <method name=\"OpenUri\"/>\n"
        "  </interface>\n"
        "  <interface name=\"org.freedesktop.DBus.Properties\">\n"
        "    <method name=\"Get\"/>\n"
        "    <method name=\"Set\"/>\n"
        "    <method name=\"GetAll\"/>\n"
        "    <signal name=\"PropertiesChanged\"/>\n"
        "  </interface>\n"
    )

public:
    explicit MprisClient(MpdClient *mpdClient, QObject *parent = nullptr);
    ~MprisClient() override;

    // org.mpris.MediaPlayer2 interface
    Q_PROPERTY(bool CanQuit READ canQuit)
    Q_PROPERTY(bool CanSetFullscreen READ canSetFullscreen)
    Q_PROPERTY(bool Fullscreen READ fullscreen)
    Q_PROPERTY(bool CanRaise READ canRaise)
    Q_PROPERTY(QString Identity READ identity)
    Q_PROPERTY(QStringList SupportedUriSchemes READ supportedUriSchemes)
    Q_PROPERTY(QStringList SupportedMimeTypes READ supportedMimeTypes)

    bool canQuit() const { return true; }
    bool canSetFullscreen() const { return false; }
    bool fullscreen() const { return false; }
    bool canRaise() const { return true; }
    QString identity() const { return "Quester"; }
    QStringList supportedUriSchemes() const { return {"file"}; }
    QStringList supportedMimeTypes() const { return {"audio/mpeg", "audio/ogg", "audio/flac"}; }

    Q_INVOKABLE void quit();
    Q_INVOKABLE void raise();

    // org.mpris.MediaPlayer2.Player interface
    Q_PROPERTY(bool CanGoNext READ canGoNext)
    Q_PROPERTY(bool CanGoPrevious READ canGoPrevious)
    Q_PROPERTY(bool CanPlay READ canPlay)
    Q_PROPERTY(bool CanPause READ canPause)
    Q_PROPERTY(bool CanSeek READ canSeek)
    Q_PROPERTY(bool CanControl READ canControl)
    Q_PROPERTY(double Rate READ rate)
    Q_PROPERTY(double MinimumRate READ minimumRate)
    Q_PROPERTY(double MaximumRate READ maximumRate)
    Q_PROPERTY(QString Shuffle READ shuffle)
    Q_PROPERTY(QString LoopStatus READ loopStatus)
    Q_PROPERTY(QVariantMap Metadata READ metadata)
    Q_PROPERTY(double Volume READ volume)
    Q_PROPERTY(double Position READ position)
    Q_PROPERTY(QString PlaybackStatus READ playbackStatus)

    bool canGoNext() const;
    bool canGoPrevious() const;
    bool canPlay() const;
    bool canPause() const;
    bool canSeek() const { return false; }
    bool canControl() const { return true; }
    double rate() const { return 1.0; }
    double minimumRate() const { return 1.0; }
    double maximumRate() const { return 1.0; }
    QString shuffle() const { return mpdClient()->random() ? "true" : "false"; }
    QString loopStatus() const { return "none"; }
    QVariantMap metadata() const;
    double volume() const { return 0.0; }
    double position() const;
    QString playbackStatus() const;

    Q_INVOKABLE void next();
    Q_INVOKABLE void previous();
    Q_INVOKABLE void pause();
    Q_INVOKABLE void playPause();
    Q_INVOKABLE void stop();
    Q_INVOKABLE void play();
    Q_INVOKABLE void seek(double offset);
    Q_INVOKABLE void setPosition(const QString &trackId, double position);
    Q_INVOKABLE void openUri(const QString &uri);

Q_SIGNALS:
    void propertiesChanged(const QString &interfaceName, const QVariantMap &changedProperties, const QStringList &invalidatedProperties);

private:
    MpdClient *m_mpdClient;
    QTimer *m_positionTimer;

    MpdClient *mpdClient() const { return m_mpdClient; }
    void updatePosition();
};

#endif // MPRISCLIENT_H