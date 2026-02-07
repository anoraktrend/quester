#ifndef DBUS_H
#define DBUS_H

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

class DBusService : public QObject
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
        "    <property access=\"rw\" type=\"d\" name=\"Rate\" WRITE setRate/>\n"
        "    <property access=\"r\" type=\"d\" name=\"MinimumRate\"/>\n"
        "    <property access=\"r\" type=\"d\" name=\"MaximumRate\"/>\n"
        "    <property access=\"rw\" type=\"b\" name=\"Shuffle\" WRITE setShuffle/>\n"
        "    <property access=\"rw\" type=\"s\" name=\"LoopStatus\" WRITE setLoopStatus/>\n"
        "    <property access=\"r\" type=\"a{sv}\" name=\"Metadata\"/>\n"
        "    <property access=\"rw\" type=\"d\" name=\"Volume\" WRITE setVolume/>\n"
        "    <property access=\"r\" type=\"x\" name=\"Position\"/>\n"
        "    <property access=\"r\" type=\"s\" name=\"PlaybackStatus\"/>\n"
        "    <method name=\"Next\"/>\n"
        "    <method name=\"Previous\"/>\n"
        "    <method name=\"Pause\"/>\n"
        "    <method name=\"PlayPause\"/>\n"
        "    <method name=\"Stop\"/>\n"
        "    <method name=\"Play\"/>\n"
        "    <method name=\"Seek\">\n"
        "      <arg direction=\"in\" type=\"x\" name=\"Offset\"/>\n"
        "    </method>\n"
        "    <method name=\"SetPosition\">\n"
        "      <arg direction=\"in\" type=\"o\" name=\"TrackId\"/>\n"
        "      <arg direction=\"in\" type=\"x\" name=\"Position\"/>\n"
        "    </method>\n"
        "    <method name=\"OpenUri\">\n"
        "      <arg direction=\"in\" type=\"s\" name=\"Uri\"/>\n"
        "    </method>\n"
        "    <signal name=\"Seeked\">\n"
        "      <arg name=\"Position\" type=\"x\"/>\n"
        "    </signal>\n"
        "  </interface>\n"
        "  <interface name=\"org.freedesktop.DBus.Properties\">\n"
        "    <method name=\"Get\">\n"
        "      <arg direction=\"in\" type=\"s\" name=\"interface_name\"/>\n"
        "      <arg direction=\"in\" type=\"s\" name=\"property_name\"/>\n"
        "      <arg direction=\"out\" type=\"v\" name=\"value\"/>\n"
        "    </method>\n"
        "    <method name=\"GetAll\">\n"
        "      <arg direction=\"in\" type=\"s\" name=\"interface_name\"/>\n"
        "      <arg direction=\"out\" type=\"a{sv}\" name=\"properties\"/>\n"
        "    </method>\n"
        "    <method name=\"Set\">\n"
        "      <arg direction=\"in\" type=\"s\" name=\"interface_name\"/>\n"
        "      <arg direction=\"in\" type=\"s\" name=\"property_name\"/>\n"
        "      <arg direction=\"in\" type=\"v\" name=\"value\"/>\n"
        "    </method>\n"
        "    <signal name=\"PropertiesChanged\">\n"
        "      <arg type=\"s\" name=\"interface_name\"/>\n"
        "      <arg type=\"a{sv}\" name=\"changed_properties\"/>\n"
        "      <arg type=\"as\" name=\"invalidated_properties\"/>\n"
        "    </signal>\n"
        "  </interface>\n"
    )

public:
    explicit DBusService(MpdClient *mpdClient, QObject *parent = nullptr);
    ~DBusService() override;

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
    Q_PROPERTY(double Rate READ rate WRITE setRate)
    Q_PROPERTY(double MinimumRate READ minimumRate)
    Q_PROPERTY(double MaximumRate READ maximumRate)
    Q_PROPERTY(bool Shuffle READ shuffle WRITE setShuffle)
    Q_PROPERTY(QString LoopStatus READ loopStatus WRITE setLoopStatus)
    Q_PROPERTY(QVariantMap Metadata READ metadata)
    Q_PROPERTY(double Volume READ volume WRITE setVolume)
    Q_PROPERTY(qlonglong Position READ position)
    Q_PROPERTY(QString PlaybackStatus READ playbackStatus)

    bool canGoNext() const;
    bool canGoPrevious() const;
    bool canPlay() const;
    bool canPause() const;
    bool canSeek() const { return true; }
    bool canControl() const { return true; }
    double rate() const { return 1.0; }
    void setRate(double rate);
    double minimumRate() const { return 1.0; }
    double maximumRate() const { return 1.0; }
    bool shuffle() const { return mpdClient()->random(); }
    void setShuffle(bool shuffle);
    QString loopStatus() const;
    void setLoopStatus(const QString &status);
    QVariantMap metadata() const;
    double volume() const;
    void setVolume(double volume);
    qlonglong position() const;
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
    void Seeked(qlonglong position);
    void propertiesChanged(const QString &interfaceName, const QVariantMap &changedProperties, const QStringList &invalidatedProperties);

private:
    MpdClient *m_mpdClient;
    QTimer *m_positionTimer;
    QDBusConnection m_connection;

    MpdClient *mpdClient() const { return m_mpdClient; }
    void updatePosition();
};

#endif // DBUS_H