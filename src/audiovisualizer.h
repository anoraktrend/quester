#ifndef AUDIOVISUALIZER_H
#define AUDIOVISUALIZER_H

#include <QObject>
#include <QTimer>
#include <QThread>
#include <QList>
#include <QByteArray>
#include <QtMultimedia/QAudioSource>
#include <QtMultimedia/QAudioFormat>
#ifndef __APPLE__
#include <pulse/pulseaudio.h>
#include <pulse/simple.h>
#include <pulse/error.h>
#include <pulse/thread-mainloop.h>
#include <pipewire/pipewire.h>
#include <pipewire/node.h>
#include <pipewire/keys.h>
#include <spa/param/audio/format-utils.h>
#endif
#ifdef __APPLE__
#include <AudioToolbox/AudioToolbox.h>
#include <Accelerate/Accelerate.h>
#endif
#include <QMutex>
#include <QColor>
#include <QVariantList>
#include <thread>
#include <atomic>
#include <vector>
#include "../vendor/gist/src/Gist.h"

constexpr int FFT_SIZE = 4096;

class AudioInput : public QObject
{
    Q_OBJECT
public:
    explicit AudioInput(QObject *parent = nullptr) : QObject(parent) {}
    ~AudioInput() override = default;
    AudioInput(const AudioInput &) = delete;
    auto operator=(const AudioInput &) -> AudioInput & = delete;
    AudioInput(AudioInput &&) = delete;
    auto operator=(AudioInput &&) -> AudioInput & = delete;
    virtual void start() = 0;
    virtual void stop() = 0;

signals:
    void dataReady(const QByteArray &data);
    void error(const QString &errorString);
};

#ifndef __APPLE__
class PulseAudioInput : public AudioInput
{
    Q_OBJECT
public:
    explicit PulseAudioInput(QObject *parent = nullptr);
    ~PulseAudioInput() override;

    PulseAudioInput(const PulseAudioInput &) = delete;
    auto operator=(const PulseAudioInput &) -> PulseAudioInput & = delete;
    PulseAudioInput(PulseAudioInput &&) = delete;
    auto operator=(PulseAudioInput &&) -> PulseAudioInput & = delete;

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
    void stopImpl();

    pa_threaded_mainloop *m_mainloop = nullptr;
    pa_context *m_context = nullptr;
    pa_stream *m_stream = nullptr;
    
    std::atomic<bool> m_quit{false};
};
#endif

#ifndef __APPLE__
class PipeWireInput : public AudioInput
{
    Q_OBJECT
public:
    explicit PipeWireInput(QObject *parent = nullptr);
    ~PipeWireInput() override;
    PipeWireInput(const PipeWireInput &) = delete;
    auto operator=(const PipeWireInput &) -> PipeWireInput & = delete;
    PipeWireInput(PipeWireInput &&) = delete;
    auto operator=(PipeWireInput &&) -> PipeWireInput & = delete;
    void start() override;
    void stop() override;

private:
    // Callbacks
    static void on_process(void *userdata);
    static void on_core_error(void *userdata, uint32_t id, int seq, int res, const char *message);
    static void registry_event_global(void *userdata, uint32_t id, uint32_t permissions, const char *type, uint32_t version, const struct spa_dict *props);
    
    // Private methods
    void stopImpl();
    void createStream();
    void cleanup();

    // PipeWire objects
    struct pw_thread_loop *m_loop = nullptr;
    struct pw_context *m_context = nullptr;
    struct pw_core *m_core = nullptr;
    struct pw_stream *m_stream = nullptr;
    struct pw_registry *m_registry = nullptr;

    // Listeners
    struct spa_hook m_core_listener;
    struct spa_hook m_registry_listener;
    
    // State
    uint32_t m_target_id = PW_ID_ANY;
    std::atomic<bool> m_quit{false};
};
#endif

class FifoInput : public AudioInput
{
    Q_OBJECT
public:
    explicit FifoInput(QString path, QObject *parent = nullptr);
    ~FifoInput() override;
    FifoInput(const FifoInput &) = delete;
    auto operator=(const FifoInput &) -> FifoInput & = delete;
    FifoInput(FifoInput &&) = delete;
    auto operator=(FifoInput &&) -> FifoInput & = delete;
    void start() override;
    void stop() override;

private:
    void stopImpl();
    QString m_path;
    std::atomic<bool> m_running;
    std::thread m_thread;
    int m_fifoFd = -1;
    int m_cancelFd = -1;
    int m_cancelReadFd = -1;
};

#ifdef __APPLE__
class CoreAudioInput : public AudioInput
{
    Q_OBJECT
public:
    explicit CoreAudioInput(QObject *parent = nullptr);
    ~CoreAudioInput() override;
    CoreAudioInput(const CoreAudioInput &) = delete;
    auto operator=(const CoreAudioInput &) -> CoreAudioInput & = delete;
    CoreAudioInput(CoreAudioInput &&) = delete;
    auto operator=(CoreAudioInput &&) -> CoreAudioInput & = delete;
    void start() override;
    void stop() override;

private:
    static OSStatus audioTapCallback(void *inClientData, AudioUnitRenderActionFlags *ioActionFlags, const AudioTimeStamp *inTimeStamp, UInt32 inBusNumber, UInt32 inNumberFrames, AudioBufferList *ioData);
    OSStatus setupAudioTap();
    void cleanupAudioTap();
    
    AudioComponentInstance m_remoteIOUnit = nullptr;
    AudioComponent m_remoteIOComponent = nullptr;
    bool m_isRunning = false;
    AudioStreamBasicDescription m_streamFormat;
};
#endif

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
    Q_PROPERTY(QString audioSource READ audioSource WRITE setAudioSource NOTIFY audioSourceChanged)
    Q_PROPERTY(int visualizerBarSize READ visualizerBarSize WRITE setVisualizerBarSize NOTIFY visualizerBarSizeChanged)
    Q_PROPERTY(int visualizerBarGap READ visualizerBarGap WRITE setVisualizerBarGap NOTIFY visualizerBarGapChanged)
    Q_PROPERTY(QString fifoPath READ fifoPath WRITE setFifoPath NOTIFY fifoPathChanged)

public:
    explicit AudioVisualizer(QObject *parent = nullptr);
    ~AudioVisualizer() override;
    AudioVisualizer(const AudioVisualizer &) = delete;
    auto operator=(const AudioVisualizer &) -> AudioVisualizer & = delete;
    AudioVisualizer(AudioVisualizer &&) = delete;
    auto operator=(AudioVisualizer &&) -> AudioVisualizer & = delete;

    [[nodiscard]] auto magnitudes() const -> QList<qreal>;
    [[nodiscard]] auto active() const -> bool;
    [[nodiscard]] auto width() const -> int;
    void setWidth(int width, bool forceUpdate = false);
    [[nodiscard]] auto height() const -> int;
    void setHeight(int height, bool forceUpdate = false);
    [[nodiscard]] auto presetNames() const -> QStringList;
    [[nodiscard]] auto currentPreset() const -> QString;
    void setCurrentPreset(const QString &name);
    [[nodiscard]] auto barColors() const -> QVariantList;
    Q_INVOKABLE void updateSystemColors(const QColor &highlight, const QColor &text);
    Q_INVOKABLE QString loadVisualizerGradients();
    [[nodiscard]] auto topDownMode() const -> bool;
    void setTopDownMode(bool topDownMode);
    [[nodiscard]] auto audioSource() const -> QString;
    void setAudioSource(const QString &source);
    [[nodiscard]] auto visualizerBarSize() const -> int;
    void setVisualizerBarSize(int size);
    [[nodiscard]] auto visualizerBarGap() const -> int;
    void setVisualizerBarGap(int gap);
    [[nodiscard]] auto fifoPath() const -> QString;
    void setFifoPath(const QString &path);

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
    void audioSourceChanged();
    void visualizerBarSizeChanged();
    void visualizerBarGapChanged();
    void fifoPathChanged();
    // Emitted with every raw stereo-interleaved int16 PCM chunk
    // (same data that feeds Gist). Consumed by ProjectMItem.
    void pcmDataReady(const QByteArray &data);

private:
    // Gist audio analysis instance
    std::unique_ptr<Gist<double>> m_gist;

    AudioInput *m_input{nullptr};
    QList<qreal> m_magnitudes;
    QList<double> m_smoothBuffer;
    QList<double> m_bars;
    std::vector<double> m_monoFrame;
    std::vector<double> m_logScaleFactors;
    QByteArray m_buffer;
    bool m_active{false};
    QTimer *m_decayTimer{};
    int m_width = 0;
    int m_fft_size{FFT_SIZE};
    // Define named constants to avoid magic numbers
    static constexpr int DefaultHeight = 600;
    static constexpr int DefaultNumBars = 32;

    // Use the constants for initialization
    int m_height = DefaultHeight;
    int m_numBars = DefaultNumBars;
    int m_visualizerBarSize = 20;
    int m_visualizerBarGap = 2;
    double m_maxPeak = 100.0;
    bool m_topDown = false;
    QString m_audioSource;

    struct Preset {
        QList<QColor> colors;
        QList<double> weights;
    };
    QMap<QString, Preset> m_presets;
    QString m_currentPresetName;
    QVariantList m_barColors;

    struct BarRange {
        int startIndex;
        int endIndex;
    };
    QList<BarRange> m_leftBarRanges;
    QList<BarRange> m_rightBarRanges;

    void loadPresets();
    void updateBarColors();
    void computeBarRanges();
};

#endif // AUDIOVISUALIZER_H