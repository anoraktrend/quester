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
#include <pipewire/pipewire.h>
#include <spa/param/audio/format-utils.h>
#include <thread>
#include <atomic>

class AudioInput : public QObject
{
    Q_OBJECT
public:
    explicit AudioInput(QObject *parent = nullptr) : QObject(parent) {}
    virtual ~AudioInput() = default;
    virtual void start() = 0;
    virtual void stop() = 0;

signals:
    void dataReady(const QByteArray &data);
    void error(const QString &errorString);
};

class PulseAudioInput : public AudioInput
{
    Q_OBJECT
public:
    explicit PulseAudioInput(QObject *parent = nullptr);
    ~PulseAudioInput();

    void start() override;
    void stop() override;

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

class PipeWireInput : public AudioInput
{
    Q_OBJECT
public:
    explicit PipeWireInput(QObject *parent = nullptr);
    ~PipeWireInput();
    void start() override;
    void stop() override;

private:
    static void on_process(void *userdata);
    struct pw_thread_loop *m_loop = nullptr;
    struct pw_context *m_context = nullptr;
    struct pw_core *m_core = nullptr;
    struct pw_stream *m_stream = nullptr;
};

class FifoInput : public AudioInput
{
    Q_OBJECT
public:
    explicit FifoInput(QString path, QObject *parent = nullptr);
    ~FifoInput();
    void start() override;
    void stop() override;

private:
    QString m_path;
    std::atomic<bool> m_running;
    std::thread m_thread;
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
    Q_PROPERTY(bool topDownMode READ topDownMode WRITE setTopDownMode NOTIFY topDownModeChanged)

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
    bool topDownMode() const;
    void setTopDownMode(bool topDownMode);

public slots:
    void start();
    void stop();

private slots:
    void onDataReady(const QByteArray &data);
    void onPulseError(const QString &errorString);
    void performDecay();

signals:
    void magnitudesChanged();
    void activeChanged();
    void widthChanged();
    void heightChanged();
    void presetsChanged();
    void currentPresetChanged();
    void barColorsChanged();
    void topDownModeChanged();

private:
    fftw_plan m_fftw_plan;
    double *m_fftw_in;
    fftw_complex *m_fftw_out;
    int m_fft_size;

    AudioInput *m_input;
    QList<qreal> m_magnitudes;
    QList<double> m_smoothBuffer;
    QByteArray m_buffer;
    bool m_active;
    QTimer *m_decayTimer{};
    int m_width = 0;
    int m_height = 600;
    int m_numBars = 32;
    double m_maxPeak = 100.0;
    bool m_topDown = false;

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
