#ifndef AUDIOVISUALIZER_H
#define AUDIOVISUALIZER_H

#include <QObject>
#include <QTimer>
#include <QThread>
#include <QList>
#include <QByteArray>
#include <fftw3.h>
#include <pulse/pulseaudio.h>
#include <pulse/simple.h>
#include <pulse/error.h>
#include <pulse/thread-mainloop.h>
#include <QMutex>
#include <QColor>
#include <QVariantList>

class PulseAudioThread : public QObject
{
    Q_OBJECT
public:
    explicit PulseAudioThread(QObject *parent = nullptr);
    ~PulseAudioThread();

    void start();
    void stop();

signals:
    void dataReady(const QByteArray &data);
    void finished();
    void error(const QString &errorString);

private:
    static void context_state_callback(pa_context *c, void *userdata);
    static void stream_state_callback(pa_stream *s, void *userdata);
    static void stream_read_callback(pa_stream *s, size_t length, void *userdata);
    static void server_info_callback(pa_context *c, const pa_server_info *i, void *userdata);
    static void sink_input_info_callback(pa_context *c, const pa_sink_input_info *i, int eol, void *userdata);
    static void sink_info_callback(pa_context *c, const pa_sink_info *i, int eol, void *userdata);


    void createContext();
    void createStream(const char* deviceName);

    pa_threaded_mainloop *m_mainloop = nullptr;
    pa_context *m_context = nullptr;
    pa_stream *m_stream = nullptr;
    
    volatile bool m_quit = false;
};


class AudioVisualizer : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QList<qreal> magnitudes READ magnitudes NOTIFY magnitudesChanged)
    Q_PROPERTY(bool active READ active NOTIFY activeChanged)
    Q_PROPERTY(int width READ width WRITE setWidth NOTIFY widthChanged)
    Q_PROPERTY(int height READ height WRITE setHeight NOTIFY heightChanged)
    Q_PROPERTY(QStringList presetNames READ presetNames NOTIFY presetsChanged)
    Q_PROPERTY(QString currentPreset READ currentPreset WRITE setCurrentPreset NOTIFY currentPresetChanged)
    Q_PROPERTY(QVariantList barColors READ barColors NOTIFY barColorsChanged)

public:
    explicit AudioVisualizer(QObject *parent = nullptr);
    ~AudioVisualizer();

    QList<qreal> magnitudes() const;
    bool active() const;
    int width() const;
    void setWidth(int width);
    int height() const;
    void setHeight(int height);
    QStringList presetNames() const;
    QString currentPreset() const;
    void setCurrentPreset(const QString &name);
    QVariantList barColors() const;
    Q_INVOKABLE void updateSystemColors(const QColor &highlight, const QColor &text);

public slots:
    void start();
    void stop();

private slots:
    void onDataReady(const QByteArray &data);
    void onPulseError(const QString &errorString);

signals:
    void magnitudesChanged();
    void activeChanged();
    void widthChanged();
    void heightChanged();
    void presetsChanged();
    void currentPresetChanged();
    void barColorsChanged();

private:
    fftw_plan m_fftw_plan;
    double *m_fftw_in;
    fftw_complex *m_fftw_out;
    int m_fft_size;

    PulseAudioThread *m_pulseThread;
    QList<qreal> m_magnitudes;
    QList<double> m_smoothBuffer;
    QByteArray m_buffer;
    bool m_active;
    int m_width = 0;
    int m_height = 600;
    int m_numBars = 32;
    double m_maxPeak = 100.0;

    struct Preset {
        QList<QColor> colors;
        QList<double> weights;
    };
    QMap<QString, Preset> m_presets;
    QString m_currentPresetName;
    QVariantList m_barColors;

    void loadPresets();
    void updateBarColors();
};

#endif // AUDIOVISUALIZER_H
