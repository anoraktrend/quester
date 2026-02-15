#include "audiovisualizer.h"
#include <algorithm>
#include <cmath>
#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QDateTime>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMutexLocker>
#include <QSettings>
#include <QStandardPaths>
#include <QtMath>
#include <utility>
#include <fcntl.h>
#include <unistd.h>

constexpr int SAMPLE_RATE = 44100;
constexpr int LATENCY_USEC = 50000;
constexpr int BUFFER_SIZE = 1024;
constexpr int FIFO_BUFFER_SIZE = 4096;
constexpr int FIFO_SLEEP_USEC = 10000;
constexpr int DECAY_TIMER_MS = 50;
constexpr int DECAY_TIMER_FAST_MS = 20;
constexpr double MAX_PCM_VALUE = 32768.0;
constexpr double MONSTERCAT_FACTOR = 1.5;
constexpr double MONSTERCAT_SCALE = 1.25;
constexpr int MIN_BIN_INDEX = 4;
constexpr int MAX_BIN_INDEX = 768;
constexpr double HANN_MULTIPLIER = 0.5;
constexpr double CIRCLE_RAD = 2.0;
constexpr double PEAK_DECAY_RATE = 0.995;
constexpr double NOISE_FLOOR = 50.0;
constexpr double SMOOTHING_ATTACK = 0.4;
constexpr double SMOOTHING_DECAY = 0.85;
constexpr double DECAY_FACTOR = 0.75;
constexpr double MIN_SIGNAL_THRESHOLD = 0.001;

template<class T>
auto max(const T &a, const T &b) -> T
{
    return (a < b) ? b : a;
}

struct MonstercatParams {
    int waves;
    double monstercat;
    int height;
};

// KISS: A standalone, stateless function for the specific "Monstercat" smoothing algorithm.
// Keeps the visualizer logic decoupled from the math.
static void monstercat_filter(
    QList<double> &bars, const MonstercatParams &params)
{
    int number_of_bars = static_cast<int>(bars.size());

    if (params.waves > 0) {
        int z = 0;
        int m_y = 0, de = 0;
        for (z = 0; z < number_of_bars; z++) { // waves
            bars[z] = bars[z] / MONSTERCAT_SCALE;
            for (m_y = z - 1; m_y >= 0; m_y--) {
                de = z - m_y;
                bars[m_y] = max(bars[z] - (double)de * de, bars[m_y]);
            }
            for (m_y = z + 1; m_y < number_of_bars; m_y++) {
                de = m_y - z;
                bars[m_y] = max(bars[z] - (double)de * de, bars[m_y]);
            }
        }
    } else if (params.monstercat > 0) {
        // Optimized O(N) implementation for exponential decay
        double decay = 1.0 / params.monstercat;

        // Pass 1: Left to Right
        for (int i = 1; i < number_of_bars; i++) {
            bars[i] = max(bars[i], bars[i - 1] * decay);
        }

        // Pass 2: Right to Left
        for (int i = number_of_bars - 2; i >= 0; i--) {
            bars[i] = max(bars[i], bars[i + 1] * decay);
        }
    }
}

// --- PulseAudioInput Implementation ---

PulseAudioInput::PulseAudioInput(QObject *parent)
    : AudioInput(parent)
{}

PulseAudioInput::~PulseAudioInput()
{
    stopImpl();
}

void PulseAudioInput::stop()
{
    stopImpl();
}

void PulseAudioInput::stopImpl()
{
    m_quit = true;
    if (m_mainloop) {
        pa_threaded_mainloop_stop(m_mainloop);
    }
    // Cleanup
    if (m_stream) {
        pa_stream_disconnect(m_stream);
        pa_stream_unref(m_stream);
        m_stream = nullptr;
    }
    if (m_context) {
        pa_context_disconnect(m_context);
        pa_context_unref(m_context);
        m_context = nullptr;
    }
    if (m_mainloop) {
        pa_threaded_mainloop_free(m_mainloop);
        m_mainloop = nullptr;
    }
}

void PulseAudioInput::start()
{
    m_mainloop = pa_threaded_mainloop_new();
    if (!m_mainloop) {
        Q_EMIT error(QStringLiteral("pa_threaded_mainloop_new() failed"));
        return;
    }
    pa_mainloop_api *api = pa_threaded_mainloop_get_api(m_mainloop);
    m_context = pa_context_new(api, "Quester");

    createContext();

    if (pa_threaded_mainloop_start(m_mainloop) < 0) {
        Q_EMIT error(QStringLiteral("pa_threaded_mainloop_start() failed"));
    }
}

void PulseAudioInput::createContext()
{
    pa_context_set_state_callback(m_context, context_state_callback, this);
    if (pa_context_connect(m_context, nullptr, PA_CONTEXT_NOFLAGS, nullptr) < 0) {
        Q_EMIT error(QStringLiteral("pa_context_connect() failed"));
    }
}

void PulseAudioInput::context_state_callback(pa_context *c, void *userdata)
{
    auto *p = static_cast<PulseAudioInput *>(userdata);
    if (p->m_quit)
        return;

    switch (pa_context_get_state(c)) {
    case PA_CONTEXT_READY: {
        pa_operation *o = pa_context_get_sink_input_info_list(c, sink_input_info_callback, p);
        if (o)
            pa_operation_unref(o);
        break;
    }
    case PA_CONTEXT_FAILED:
    case PA_CONTEXT_TERMINATED:
        if (p->m_mainloop)
            pa_threaded_mainloop_stop(p->m_mainloop);
        break;
    default:
        break;
    }
}

void PulseAudioInput::server_info_callback(pa_context * /*c*/, const pa_server_info *i, void *userdata)
{
    auto *p = static_cast<PulseAudioInput *>(userdata);
    if (!i || p->m_quit || p->m_stream) {
        return;
    }

    // Don't fallback to default monitor source - only use MPD streams
    Q_EMIT p->error(QStringLiteral("No MPD audio stream found"));
}

void PulseAudioInput::sink_input_info_callback(
    pa_context *c, const pa_sink_input_info *i, int eol, void *userdata)
{
    auto *p = static_cast<PulseAudioInput *>(userdata);
    if (p->m_quit)
        return;

    if (eol > 0) {
        // If we haven't found an MPD stream yet, fail gracefully
        if (!p->m_stream) {
            Q_EMIT p->error(QStringLiteral("No MPD audio stream found"));
        }
        return;
    }

    if (i) {
        const char *app_name = pa_proplist_gets(i->proplist, "application.name");
        const char *media_name = pa_proplist_gets(i->proplist, "media.name");

        if ((app_name && (strcmp(app_name, "Music Player Daemon") == 0 || strcmp(app_name, "mpd") == 0)) ||
            (media_name && (strcmp(media_name, "Music Player Daemon") == 0 || strcmp(media_name, "mpd") == 0))) {
            if (!p->m_stream) { // Check if we already created a stream
                pa_operation *o
                    = pa_context_get_sink_info_by_index(c, i->sink, sink_info_callback, p);
                if (o)
                    pa_operation_unref(o);
            }
        }
    }
}

void PulseAudioInput::sink_info_callback(
    pa_context * /*c*/, const pa_sink_info *i, int eol, void *userdata)
{
    auto *p = static_cast<PulseAudioInput *>(userdata);
    if (eol > 0 || !i || p->m_quit || p->m_stream) {
        return;
    }

    // Only create stream if it's an MPD monitor source
    QString monitor_source = QString::fromUtf8(i->monitor_source_name);
    if (monitor_source.contains("mpd") || monitor_source.contains("MPD")) {
        p->createStream(i->monitor_source_name);
    } else {
        Q_EMIT p->error(QStringLiteral("No MPD audio stream found"));
    }
}

void PulseAudioInput::createStream(const char *deviceName)
{
    if (m_stream || m_quit)
        return; // Don't create if already exists or if quitting

    pa_sample_spec ss;
    ss.format = PA_SAMPLE_S16LE;
    ss.rate = SAMPLE_RATE;
    ss.channels = 2;

    m_stream = pa_stream_new(m_context, "Quester Record", &ss, nullptr);
    if (!m_stream) {
        Q_EMIT error(QStringLiteral("pa_stream_new() failed"));
        if (m_mainloop)
            pa_threaded_mainloop_stop(m_mainloop);
        return;
    }

    pa_stream_set_state_callback(m_stream, stream_state_callback, this);
    pa_stream_set_read_callback(m_stream, stream_read_callback, this);

    pa_buffer_attr bufattr;
    bufattr.maxlength = (uint32_t) -1;
    bufattr.tlength = (uint32_t) -1;
    bufattr.prebuf = (uint32_t) -1;
    bufattr.minreq = (uint32_t) -1;
    // Low latency: 20ms fragments
    bufattr.fragsize = pa_usec_to_bytes(LATENCY_USEC, &ss);

    if (pa_stream_connect_record(
            m_stream,
            deviceName,
            &bufattr,
            static_cast<pa_stream_flags_t>(PA_STREAM_ADJUST_LATENCY | PA_STREAM_INTERPOLATE_TIMING | PA_STREAM_AUTO_TIMING_UPDATE | PA_STREAM_START_CORKED)) // NOLINT(clang-analyzer-optin.core.EnumCastOutOfRange, cppcoreguidelines-pro-type-cstyle-cast)
        < 0) {
        Q_EMIT error(QString::fromLatin1(pa_strerror(pa_context_errno(m_context))));
        if (m_mainloop)
            pa_threaded_mainloop_stop(m_mainloop);
    }
}

void PulseAudioInput::stream_state_callback(pa_stream *s, void *userdata)
{
    auto *p = static_cast<PulseAudioInput *>(userdata);
    if (p->m_quit)
        return;

    switch (pa_stream_get_state(s)) {
    case PA_STREAM_READY:
        pa_stream_cork(s, 0, nullptr, nullptr);
        break;
    case PA_STREAM_FAILED:
    case PA_STREAM_TERMINATED:
        if (p->m_mainloop)
            pa_threaded_mainloop_stop(p->m_mainloop);
        break;
    default:
        break;
    }
}

void PulseAudioInput::stream_read_callback(pa_stream *s, size_t, void *userdata)
{
    auto *p = static_cast<PulseAudioInput *>(userdata);
    if (p->m_quit)
        return;

    const void *data = nullptr;
    size_t peek_length = 0;

    while (pa_stream_readable_size(s) > 0) {
        if (pa_stream_peek(s, &data, &peek_length) < 0) {
            return;
        }
        if (peek_length == 0)
            break;

        if (data) {
            QByteArray buffer(static_cast<const char *>(data), static_cast<qsizetype>(peek_length));
            Q_EMIT p->dataReady(buffer);
        } else {
            // Hole in stream, fill with silence
            QByteArray buffer(static_cast<qsizetype>(peek_length), 0);
            Q_EMIT p->dataReady(buffer);
        }

        pa_stream_drop(s);
    }
}

// --- PipeWireInput Implementation ---

PipeWireInput::PipeWireInput(QObject *parent) : AudioInput(parent) {}

PipeWireInput::~PipeWireInput()
{
    stopImpl();
}

void PipeWireInput::stop()
{
    stopImpl();
}

void PipeWireInput::stopImpl()
{
    m_quit = true;
    if (m_loop) {
        pw_thread_loop_stop(m_loop);
    }
    cleanup();
}

void PipeWireInput::cleanup()
{
    if (m_stream) {
        pw_stream_destroy(m_stream);
        m_stream = nullptr;
    }
    if (m_registry) {
        pw_proxy_destroy(reinterpret_cast<pw_proxy*>(m_registry));
        m_registry = nullptr;
    }
    if (m_core) {
        pw_core_disconnect(m_core);
        m_core = nullptr;
    }
    if (m_context) {
        pw_context_destroy(m_context);
        m_context = nullptr;
    }
    if (m_loop) {
        pw_thread_loop_destroy(m_loop);
        m_loop = nullptr;
    }
}

void PipeWireInput::on_core_error(void *userdata, uint32_t, int, int res, const char *message)
{
    auto *p = static_cast<PipeWireInput *>(userdata);
    qWarning() << "PipeWire core error:" << message << "(code" << res << ")";
    if (p) {
        p->stopImpl();
        Q_EMIT p->error(QString::fromUtf8(message));
    }
}

void PipeWireInput::registry_event_global(void *userdata, uint32_t id, uint32_t, const char *type, uint32_t, const struct spa_dict *props)
{
    auto *p = static_cast<PipeWireInput *>(userdata);
    if (p->m_quit || p->m_target_id != PW_ID_ANY) return;

    if (props && strcmp(type, PW_TYPE_INTERFACE_Node) == 0) {
        const char *appName = spa_dict_lookup(props, PW_KEY_APP_NAME);
        const char *mediaName = spa_dict_lookup(props, PW_KEY_MEDIA_NAME);

        bool isMpd = (appName && (strcmp(appName, "Music Player Daemon") == 0 || strcmp(appName, "mpd") == 0)) ||
                     (mediaName && (strcmp(mediaName, "Music Player Daemon") == 0 || strcmp(mediaName, "mpd") == 0));

        if (isMpd) {
            p->m_target_id = id;
            p->createStream();
            // Don't need to listen anymore
            pw_thread_loop_signal(p->m_loop, false);
        }
    }
}

void PipeWireInput::createStream() {
    if (m_quit || m_stream) return;

    std::array<uint8_t, BUFFER_SIZE> buffer{};
    struct spa_pod_builder b = {};
    spa_pod_builder_init(&b, buffer.data(), buffer.size());
    std::array<const struct spa_pod *, 1> params{};

    struct spa_audio_info_raw info = {};
    info.format = SPA_AUDIO_FORMAT_S16_LE;
    info.rate = SAMPLE_RATE;
    info.channels = 2;
    params[0] = spa_format_audio_raw_build(&b, SPA_PARAM_EnumFormat, &info);

    static struct pw_stream_events stream_events = {};
    stream_events.version = PW_VERSION_STREAM_EVENTS;
    stream_events.process = on_process;

    m_stream = pw_stream_new_simple(
        pw_thread_loop_get_loop(m_loop),
        "Quester-capture",
        pw_properties_new(
            PW_KEY_MEDIA_TYPE, "Audio",
            PW_KEY_MEDIA_CATEGORY, "Capture",
            PW_KEY_MEDIA_ROLE, "Music",
            NULL),
        &stream_events,
        this
    );

    if (!m_stream) {
        Q_EMIT error(QStringLiteral("Failed to create PipeWire stream"));
        stopImpl();
        return;
    }

    if (pw_stream_connect(m_stream,
                      PW_DIRECTION_INPUT,
                      m_target_id, // Connect to the found MPD node
                      (pw_stream_flags)(PW_STREAM_FLAG_AUTOCONNECT | PW_STREAM_FLAG_MAP_BUFFERS | PW_STREAM_FLAG_RT_PROCESS),
                      params.data(), 1) != 0) {
        Q_EMIT error(QStringLiteral("Failed to connect PipeWire stream to MPD"));
        stopImpl();
    }
}


void PipeWireInput::start()
{
    m_quit = false;
    pw_init(nullptr, nullptr);
    m_loop = pw_thread_loop_new("quester-pipewire", nullptr);
    if (!m_loop) {
        Q_EMIT error(QStringLiteral("pw_thread_loop_new failed"));
        return;
    }

    m_context = pw_context_new(pw_thread_loop_get_loop(m_loop), nullptr, 0);
    if (!m_context) {
        Q_EMIT error(QStringLiteral("pw_context_new failed"));
        cleanup();
        return;
    }

    m_core = pw_context_connect(m_context, nullptr, 0);
    if (!m_core) {
        Q_EMIT error(QStringLiteral("pw_context_connect failed"));
        cleanup();
        return;
    }
    
    static const pw_core_events core_events = { .version = PW_VERSION_CORE_EVENTS, .error = on_core_error };
    pw_core_add_listener(m_core, &m_core_listener, &core_events, this);
    
    m_registry = pw_core_get_registry(m_core, PW_VERSION_REGISTRY, 0);
    if (!m_registry) {
        Q_EMIT error(QStringLiteral("pw_core_get_registry failed"));
        cleanup();
        return;
    }

    static const pw_registry_events registry_events = { .version = PW_VERSION_REGISTRY_EVENTS, .global = registry_event_global };
    pw_registry_add_listener(m_registry, &m_registry_listener, &registry_events, this);

    pw_thread_loop_start(m_loop);
    
    // Give some time for the registry to find MPD.
    // A better approach would be a timeout mechanism.
    pw_thread_loop_timed_wait(m_loop, 2 * 1000);

    if (m_target_id == PW_ID_ANY) {
        Q_EMIT error(QStringLiteral("Could not find MPD sink in PipeWire. Is MPD playing?"));
        stopImpl();
    }
}

void PipeWireInput::on_process(void *userdata)
{
    auto *p = static_cast<PipeWireInput *>(userdata);
    if (p->m_quit) return;

    struct pw_buffer *b = nullptr;
    b = pw_stream_dequeue_buffer(p->m_stream);
    if (b == nullptr) {
        qWarning() << "PipeWire: no buffer";
        return;
    }

    struct spa_buffer *buf = b->buffer;
    if (buf->datas[0].data == nullptr) {
        pw_stream_queue_buffer(p->m_stream, b);
        return;
    }

    uint32_t size = buf->datas[0].chunk->size;
    if (size > 0) {
        QByteArray bytes(reinterpret_cast<const char*>(buf->datas[0].data), static_cast<qsizetype>(size));
        Q_EMIT p->dataReady(bytes);
    }

    pw_stream_queue_buffer(p->m_stream, b);
}


// --- FifoInput Implementation ---

FifoInput::FifoInput(QString path, QObject *parent) 
    : AudioInput(parent), m_path(std::move(path)), m_running(false), m_fifoFd(-1), m_cancelFd(-1) {}

FifoInput::~FifoInput()
{
    stopImpl();
}

void FifoInput::start()
{
    m_running = true;
    
    // Create cancellation pipe for interrupting select()
    int cancelPipe[2];
    if (pipe(cancelPipe) == -1) {
        Q_EMIT error(QStringLiteral("Failed to create cancel pipe"));
        return;
    }
    m_cancelFd = cancelPipe[1];  // write end
    m_cancelReadFd = cancelPipe[0];  // read end
    
    m_thread = std::thread([this]() -> void {
        m_fifoFd = open(m_path.toUtf8().constData(), O_RDONLY); // NOLINT(cppcoreguidelines-pro-type-vararg)
        if (m_fifoFd < 0) {
            Q_EMIT error(QStringLiteral("Could not open FIFO: ") + m_path);
            close(m_cancelFd);
            close(m_cancelReadFd);
            return;
        }
        
        std::array<char, FIFO_BUFFER_SIZE> buffer{};
        fd_set readFds;
        
        while (m_running) {
            FD_ZERO(&readFds);
            FD_SET(m_fifoFd, &readFds);
            FD_SET(m_cancelReadFd, &readFds);
            
            int maxFd = std::max(m_fifoFd, m_cancelReadFd);
            struct timeval tv = {0, 100000};  // 100ms timeout
            
            int ret = select(maxFd + 1, &readFds, nullptr, nullptr, &tv);
            if (ret < 0) {
                break;  // Error
            }
            if (ret == 0) {
                continue;  // Timeout, check m_running again
            }
            
            // Check if we got data from the FIFO
            if (FD_ISSET(m_fifoFd, &readFds)) {
                ssize_t bytes = read(m_fifoFd, buffer.data(), buffer.size());
                if (bytes > 0) {
                    Q_EMIT dataReady(QByteArray(buffer.data(), static_cast<qsizetype>(bytes)));
                } else if (bytes < 0 && errno != EAGAIN) {
                    break;  // Actual error
                }
            }
            
            // Check if cancellation was signaled
            if (FD_ISSET(m_cancelReadFd, &readFds)) {
                char dummy;
                read(m_cancelReadFd, &dummy, 1);  // Clear the pipe
                break;
            }
        }
        
        if (m_fifoFd >= 0) {
            close(m_fifoFd);
            m_fifoFd = -1;
        }
        close(m_cancelFd);
        close(m_cancelReadFd);
        m_cancelFd = -1;
        m_cancelReadFd = -1;
    });
}

void FifoInput::stop()
{
    stopImpl();
}

void FifoInput::stopImpl()
{
    m_running = false;
    
    // Signal cancellation by writing to the pipe
    if (m_cancelFd >= 0) {
        char dummy = 1;
        write(m_cancelFd, &dummy, 1);
    }
    
    if (m_thread.joinable()) {
        m_thread.join();
    }
    
    // Reset file descriptors
    m_fifoFd = -1;
    m_cancelFd = -1;
    m_cancelReadFd = -1;
}

// --- AudioVisualizer Implementation ---

AudioVisualizer::AudioVisualizer(QObject *parent)
    : QObject(parent)
    , 
     m_decayTimer(new QTimer(this))
{
    
    connect(m_decayTimer, &QTimer::timeout, this, &AudioVisualizer::performDecay);
    m_magnitudes.fill(0.0, m_numBars);
    m_smoothBuffer.fill(0.0, m_numBars);
    loadPresets();

    // Ensure System preset exists
    if (!m_presets.contains(QStringLiteral("System"))) {
        Preset system;
        system.colors = {QColor(Qt::gray)};
        m_presets.insert(QStringLiteral("System"), system);
    }

    QSettings settings(QStringLiteral("Quester"), QStringLiteral("Quester"));
    m_topDown = settings.value(QStringLiteral("visualizerTopDown"), false).toBool();
#ifdef __APPLE__
    m_audioSource = settings.value(QStringLiteral("audioSource"), QStringLiteral("coreaudio")).toString();
#else
    m_audioSource = settings.value(QStringLiteral("audioSource"), QStringLiteral("pipewire")).toString();
#endif
    QString saved = settings.value(QStringLiteral("visualizerPreset"), QStringLiteral("System")).toString();
    if (m_presets.contains(saved)) {
        m_currentPresetName = saved;
    } else {
        m_currentPresetName = QStringLiteral("rainbow");
    }
    updateBarColors();
}

AudioVisualizer::~AudioVisualizer()
{
    stop();
}

auto AudioVisualizer::width() const -> int
{
    return m_width;
}

// Minimum change threshold to trigger bar count update (pixels)
constexpr int MIN_WIDTH_CHANGE_THRESHOLD = 20;

void AudioVisualizer::setWidth(int width, bool forceUpdate)
{
    // Apply minimum threshold to avoid excessive recalculations during small resize movements
    int widthDelta = std::abs(width - m_width);
    if (!forceUpdate && widthDelta < MIN_WIDTH_CHANGE_THRESHOLD && m_width > 0) {
        m_width = width;  // Still update the value, but don't recalculate bars
        Q_EMIT widthChanged();
        return;
    }

    if (m_width == width && !forceUpdate)
        return;

    m_width = width;
    
    int barPlusGap = m_visualizerBarSize + m_visualizerBarGap;
    if (barPlusGap <= 0) return; // Avoid division by zero
    int newNumBars = std::max(1, m_width / barPlusGap);
    
    bool barsChanged = (m_numBars != newNumBars);
    if (barsChanged) {
        m_numBars = newNumBars;

        // Preserve existing magnitude data when possible by resampling
        if (!m_magnitudes.isEmpty() && m_magnitudes.size() > 0) {
            // Keep old values for smooth transition - they will naturally fade
            m_smoothBuffer.fill(0.0, m_numBars);
        } else {
            m_magnitudes.fill(0.0, m_numBars);
            m_smoothBuffer.fill(0.0, m_numBars);
        }
        
        updateBarColors();
        computeBarRanges();
    }
    Q_EMIT widthChanged();
    if (barsChanged) {
        Q_EMIT magnitudesChanged();
    }
}

auto AudioVisualizer::height() const -> int
{
    return m_height;
}

void AudioVisualizer::setHeight(int height, bool forceUpdate)
{
    // Apply minimum threshold for height changes too
    int heightDelta = std::abs(height - m_height);
    if (!forceUpdate && heightDelta < MIN_WIDTH_CHANGE_THRESHOLD && m_height > 0) {
        m_height = height;
        Q_EMIT heightChanged();
        return;
    }

    if (m_height == height && !forceUpdate) return;
    m_height = height;
    Q_EMIT heightChanged();
}

auto AudioVisualizer::topDownMode() const -> bool
{
    return m_topDown;
}

void AudioVisualizer::setTopDownMode(bool topDownMode)
{
    if (m_topDown == topDownMode) return;
    m_topDown = topDownMode;
    QSettings settings(QStringLiteral("Quester"), QStringLiteral("Quester"));
    settings.setValue(QStringLiteral("visualizerTopDown"), m_topDown);
    Q_EMIT topDownModeChanged();
}

auto AudioVisualizer::audioSource() const -> QString
{
    return m_audioSource;
}

void AudioVisualizer::setAudioSource(const QString &source)
{
    if (m_audioSource == source) return;
    m_audioSource = source;
    if (m_active) {
        stop();
        start();
    }
    Q_EMIT audioSourceChanged();
}

auto AudioVisualizer::visualizerBarSize() const -> int
{
    return m_visualizerBarSize;
}

void AudioVisualizer::setVisualizerBarSize(int size)
{
    if (m_visualizerBarSize == size) return;
    m_visualizerBarSize = size;
    Q_EMIT visualizerBarSizeChanged();
    // Force a recalculation of bars
    setWidth(m_width, true);
}

auto AudioVisualizer::visualizerBarGap() const -> int
{
    return m_visualizerBarGap;
}

void AudioVisualizer::setVisualizerBarGap(int gap)
{
    if (m_visualizerBarGap == gap) return;
    m_visualizerBarGap = gap;
    Q_EMIT visualizerBarGapChanged();
    // Force a recalculation of bars
    setWidth(m_width, true);
}

void AudioVisualizer::start()
{
    if (m_active)
        return;

    m_maxPeak = 100.0;

    // Initialize Gist for audio analysis
    m_gist = std::make_unique<Gist<double>>(m_fft_size, SAMPLE_RATE);
    
    m_monoFrame.resize(m_fft_size);
    computeBarRanges();

    m_buffer.clear();
    m_smoothBuffer.fill(0.0, m_numBars);

    QSettings settings(QStringLiteral("Quester"), QStringLiteral("Quester"));

    // Prioritize MPD FIFO input
    QString fifoPath = settings.value("fifoPath", QStringLiteral("/tmp/mpd.fifo")).toString();
    if (QFile::exists(fifoPath)) {
        m_input = new FifoInput(fifoPath, this);
    } else if (m_audioSource == QStringLiteral("pipewire")) {
#ifndef __APPLE__
        m_input = new PipeWireInput(this);
#endif
    } else if (m_audioSource == QStringLiteral("fifo")) {
        m_input = new FifoInput(fifoPath, this);
    } else if (m_audioSource == QStringLiteral("coreaudio")) {
#ifdef __APPLE__
        m_input = new CoreAudioInput(this);
#endif
    } else {
#ifndef __APPLE__
        m_input = new PulseAudioInput(this);
#endif
    }

    if (!m_input) {
        qWarning() << "No suitable audio input created for this platform/configuration.";
        return;
    }

    connect(m_input,
            &AudioInput::dataReady,
            this,
            &AudioVisualizer::onDataReady,
            Qt::QueuedConnection);
    connect(m_input, &AudioInput::error, this, &AudioVisualizer::onPulseError);

    m_input->start();

    m_active = true;
    Q_EMIT activeChanged();
    m_decayTimer->start(DECAY_TIMER_MS);
}

void AudioVisualizer::stop()
{
    if (!m_active)
        return;
    m_active = false;
    Q_EMIT activeChanged();

    if (m_input) {
        m_input->stop();
        delete m_input;
        m_input = nullptr;
    }

    // Reset Gist
    m_gist.reset();
}

void AudioVisualizer::onDataReady(const QByteArray &data)
{
    if (!m_active || !m_gist) {
        return;
    }

    m_buffer.append(data);

    // Gist expects mono audio frames of m_fft_size samples
    int requiredBytes = m_fft_size * 2; // 2 bytes per sample (16-bit)

    if (m_buffer.size() < requiredBytes) {
        return;
    }

    // Keep only the latest requiredBytes
    if (m_buffer.size() > requiredBytes) {
        m_buffer = m_buffer.right(requiredBytes);
    }

    const auto *pcm = reinterpret_cast<const int16_t *>(m_buffer.constData());

    // Process both channels and combine them
    std::vector<double> monoFrame(m_fft_size);
    for (int i = 0; i < m_fft_size; ++i) {
        // Average left and right channels
        double left = static_cast<double>(pcm[i * 2]) / MAX_PCM_VALUE;
        double right = static_cast<double>(pcm[i * 2 + 1]) / MAX_PCM_VALUE;
        monoFrame[i] = (left + right) / 2.0;
    }

    // Process with Gist - this calculates the magnitude spectrum internally
    m_gist->processAudioFrame(monoFrame);

    // Get the magnitude spectrum from Gist
    const std::vector<double> &magnitudeSpectrum = m_gist->getMagnitudeSpectrum();

    if (m_bars.size() != m_numBars) {
        m_bars.resize(m_numBars);
    }

    double currentFrameMax = 0.0;

    int leftBarsCount = m_numBars / 2;
    int rightBarsCount = m_numBars - leftBarsCount;

    for (int channel = 0; channel < 2; ++channel) {
        int barCount = (channel == 0) ? leftBarsCount : rightBarsCount;
        int offset = (channel == 0) ? 0 : leftBarsCount;
        bool reverse = (channel == 0); // Reverse left channel to put bass in center

        for (int i = 0; i < barCount; i++) {
            const QList<BarRange> &ranges = (channel == 0) ? m_leftBarRanges : m_rightBarRanges;
            if (i >= ranges.size()) break;
            
            const BarRange &range = ranges[i];
            int startIndex = range.startIndex;
            int endIndex = range.endIndex;

            double maxMag = 0.0;
            for (int b = startIndex; b < endIndex && b < static_cast<int>(magnitudeSpectrum.size()); ++b) {
                double mag = magnitudeSpectrum[b];
                if (mag > maxMag)
                    maxMag = mag;
            }

            if (i < static_cast<int>(m_logScaleFactors.size())) {
                maxMag *= m_logScaleFactors[i];
            }

            if (maxMag > currentFrameMax) {
                currentFrameMax = maxMag;
            }

            int targetIdx = reverse ? (offset + barCount - 1 - i) : (offset + i);
            m_bars[targetIdx] = maxMag;
        }
    }

    // Dynamic scaling (AGC)
    m_maxPeak *= PEAK_DECAY_RATE; // Slow decay
    if (m_maxPeak < NOISE_FLOOR) m_maxPeak = NOISE_FLOOR; // Noise floor
    if (currentFrameMax > m_maxPeak) m_maxPeak = currentFrameMax;

    // Normalize
    for (int i = 0; i < m_numBars; i++) {
        m_bars[i] /= m_maxPeak;
    }

    auto h = (double)m_height;
    if (h < 1.0) h = 1.0;

    for (int i = 0; i < m_numBars; i++) {
        m_bars[i] *= h;
    }

    monstercat_filter(m_bars, {.waves=0, .monstercat=MONSTERCAT_FACTOR, .height=(int)h});

    for (int i = 0; i < m_numBars; i++) {
        m_bars[i] /= h;
    }

    m_magnitudes.clear();
    if (m_smoothBuffer.size() != m_numBars) {
        m_smoothBuffer.fill(0.0, m_numBars);
    }

    for (int i = 0; i < m_numBars; i++) {
        double val = m_bars[i];
        double &smoothVal = m_smoothBuffer[i];

        // Apply smoothing: Fast attack (0.4), Slow decay (0.85)
        double factor = (val > smoothVal) ? SMOOTHING_ATTACK : SMOOTHING_DECAY;
        smoothVal = smoothVal * factor + val * (1.0 - factor);

        val = std::min(1.0, std::max(0.0, smoothVal));
        m_magnitudes.append(val);
    }

    Q_EMIT magnitudesChanged();
    static qint64 lastEmit = 0;
    static qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (now - lastEmit > 30) {
        Q_EMIT magnitudesChanged();
        lastEmit = now;
    }
    m_decayTimer->start(DECAY_TIMER_MS);
}

void AudioVisualizer::onPulseError(const QString &errorString)
{
    qWarning() << "Audio Input Error:" << errorString;
    stop();
}

void AudioVisualizer::performDecay()
{
    bool hasSignal = false;
    m_magnitudes.clear();

    if (m_smoothBuffer.size() != m_numBars) {
        m_smoothBuffer.fill(0.0, m_numBars);
    }

    for (int i = 0; i < m_numBars; i++) {
        double &smoothVal = m_smoothBuffer[i];
        smoothVal *= DECAY_FACTOR;
        if (smoothVal < MIN_SIGNAL_THRESHOLD) smoothVal = 0.0;

        m_magnitudes.append(smoothVal);

        if (smoothVal > 0.0) hasSignal = true;
    }

    Q_EMIT magnitudesChanged();

    if (hasSignal) {
        m_decayTimer->start(DECAY_TIMER_FAST_MS);
    } else {
        m_decayTimer->stop();
    }
}

auto AudioVisualizer::magnitudes() const -> QList<qreal>
{
    return m_magnitudes;
}

auto AudioVisualizer::active() const -> bool
{
    return m_active;
}

void AudioVisualizer::loadPresets()
{
    m_presets.clear();

    auto parsePresets = [this](const QJsonObject &root) -> void {
        for (auto it = root.begin(); it != root.end(); ++it) {
            QString name = it.key();
            QJsonValue val = it.value();
            Preset preset;

            QJsonArray colorsArray;
            if (val.isArray()) {
                colorsArray = val.toArray();
            } else if (val.isObject()) {
                QJsonObject obj = val.toObject();
                colorsArray = obj[QStringLiteral("colors")].toArray();
                QJsonArray weightsArray = obj[QStringLiteral("weights")].toArray();
                for (const auto &w : weightsArray) {
                    preset.weights.append(w.toDouble());
                }
            }

            for (const auto &c : colorsArray) {
                preset.colors.append(QColor(c.toString()));
            }

            if (!preset.colors.isEmpty()) {
                m_presets.insert(name, preset);
            }
        }
    };

    // 1. Load bundled/system presets
    QStringList paths
        = {
#ifdef QT_QML_SOURCE_DIR
            QStringLiteral(QT_QML_SOURCE_DIR) + "/presets/visualizerGradients/presets.json",
#endif
            QCoreApplication::applicationDirPath() + QStringLiteral("/presets/visualizerGradients/presets.json"),
            QCoreApplication::applicationDirPath() + QStringLiteral("/../presets/visualizerGradients/presets.json"),
            QStringLiteral("presets/visualizerGradients/presets.json")};
    
    // macOS bundle resource path
#ifdef __APPLE__
    paths.prepend(QCoreApplication::applicationDirPath() + QStringLiteral("/../Resources/presets/presets.json"));
#endif


    for (const QString &path : paths) {
        if (QFile::exists(path)) {
            QFile file(path);
            if (file.open(QIODevice::ReadOnly)) {
                QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
                if (doc.isObject()) {
                    parsePresets(doc.object());
                }
                break;
            }
        }
    }

    // 2. Load presets from standard locations (System -> User)
    // We iterate in reverse order of priority so that User presets (loaded last)
    // override System presets.
    QStringList locations;

    // Explicitly requested path (Low priority)
    locations << QStringLiteral("/etc/config");

    // Generic Data (e.g. /usr/share)
    QStringList dataLocs = QStandardPaths::standardLocations(QStandardPaths::GenericDataLocation);
    std::ranges::reverse(dataLocs);
    locations << dataLocs;

    // Config (e.g. /etc/xdg, ~/.config)
    QStringList configLocs = QStandardPaths::standardLocations(QStandardPaths::ConfigLocation);
    std::ranges::reverse(configLocs);
    locations << configLocs;

    for (const QString &location : locations) {
        QDir presetDir(QStringLiteral("%1/Quester/presets").arg(location));
        if (presetDir.exists()) {
            const auto fileInfos = presetDir.entryInfoList(QStringList() << QStringLiteral("*.json"), QDir::Files);
            for (const QFileInfo &info : fileInfos) {
                QFile file(info.absoluteFilePath());
                if (file.open(QIODevice::ReadOnly)) {
                    QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
                    if (doc.isObject()) {
                        parsePresets(doc.object());
                    }
                }
            }
        }
    }

    Q_EMIT presetsChanged();
}

auto AudioVisualizer::presetNames() const -> QStringList
{
    return m_presets.keys();
}

auto AudioVisualizer::currentPreset() const -> QString
{
    return m_currentPresetName;
}

void AudioVisualizer::setCurrentPreset(const QString &name)
{
    if (m_currentPresetName == name || !m_presets.contains(name))
        return;

    m_currentPresetName = name;

    QSettings settings(QStringLiteral("Quester"), QStringLiteral("Quester"));
    settings.setValue(QStringLiteral("visualizerPreset"), name);

    updateBarColors();
    Q_EMIT currentPresetChanged();
}

auto AudioVisualizer::barColors() const -> QVariantList
{
    return m_barColors;
}

QString AudioVisualizer::loadVisualizerGradients()
{
    QStringList paths
        = {
#ifdef QT_QML_SOURCE_DIR
            QStringLiteral(QT_QML_SOURCE_DIR) + "/presets/visualizerGradients/presets.json",
#endif
            QCoreApplication::applicationDirPath() + QStringLiteral("/presets/visualizerGradients/presets.json"),
            QCoreApplication::applicationDirPath() + QStringLiteral("/../presets/visualizerGradients/presets.json"),
            QStringLiteral("presets/visualizerGradients/presets.json")};
    
    // macOS bundle resource path
#ifdef __APPLE__
    paths.prepend(QCoreApplication::applicationDirPath() + QStringLiteral("/../Resources/presets/visualizerGradients/presets.json"));
#endif

    for (const QString &path : paths) {
        if (QFile::exists(path)) {
            QFile file(path);
            if (file.open(QIODevice::ReadOnly)) {
                return file.readAll();
            }
        }
    }

    return QString();
}


void AudioVisualizer::updateBarColors()
{
    m_barColors.clear();
    if (!m_presets.contains(m_currentPresetName) || m_numBars <= 0) {
        return;
    }

    const Preset &preset = m_presets[m_currentPresetName];
    const QList<QColor> &colors = preset.colors;
    const QList<double> &weights = preset.weights;

    if (colors.isEmpty())
        return;
    if (colors.size() == 1) {
        for (int i = 0; i < m_numBars; ++i)
            m_barColors.append(colors.first());
        Q_EMIT barColorsChanged();
        return;
    }

    // Prepare stops
    QList<double> stops;
    if (weights.size() == colors.size()) {
        double totalWeight = 0;
        for (double w : weights)
            totalWeight += w;
        double current = 0;
        // Center the colors in their weighted regions? Or use edges?
        // Let's treat weights as intervals.
        stops.append(0.0);
        for (int i = 0; i < weights.size() - 1; ++i) {
            current += weights[i];
            stops.append(current / totalWeight);
        }
        stops.append(1.0);
    } else {
        // Uniform distribution
        for (int i = 0; i < colors.size(); ++i) {
            stops.append(i / double(colors.size() - 1));
        }
    }

    for (int i = 0; i < m_numBars; ++i) {
        double t = i / double(m_numBars > 1 ? m_numBars - 1 : 1);

        // Find segment
        int segment = 0;
        while (segment < stops.size() - 2 && t > stops[segment + 1]) {
            segment++;
        }

        double t0 = stops[segment];
        double t1 = stops[segment + 1];
        double localT = (t - t0) / (t1 - t0);

        QColor c1 = colors[segment];
        QColor c2 = colors[segment + 1];

        // Linear interpolation
        int r = static_cast<int>(c1.red() + localT * (c2.red() - c1.red()));
        int g = static_cast<int>(c1.green() + localT * (c2.green() - c1.green()));
        int b = static_cast<int>(c1.blue() + localT * (c2.blue() - c1.blue()));

        m_barColors.append(QColor(r, g, b));
    }
    Q_EMIT barColorsChanged();
}

void AudioVisualizer::updateSystemColors(const QColor &highlight, const QColor &text)
{
    Preset system;
    system.colors = {highlight, text};
    m_presets.insert(QStringLiteral("System"), system);

    if (m_currentPresetName == QStringLiteral("System")) {
        updateBarColors();
    }
}

void AudioVisualizer::computeBarRanges()
{
    int leftBarsCount = m_numBars / 2;
    int rightBarsCount = m_numBars - leftBarsCount;
    int minBin = MIN_BIN_INDEX;
    int maxBin = MAX_BIN_INDEX;
    int numBins = m_fft_size / 2 + 1;

    int maxBars = std::max(leftBarsCount, rightBarsCount);
    m_logScaleFactors.resize(maxBars);
    for (int i = 0; i < maxBars; ++i) {
        m_logScaleFactors[i] = std::log2(i + 2);
    }

    auto computeRanges = [&](QList<BarRange> &ranges, int barCount) -> void {
        ranges.clear();
        ranges.reserve(barCount);
        for (int i = 0; i < barCount; ++i) {
            double start = minBin * std::pow((double) maxBin / minBin, (double) i / barCount);
            double end = minBin * std::pow((double) maxBin / minBin, (double) (i + 1) / barCount);
            int startIndex = (int) start;
            int endIndex = (int) end;
            if (endIndex <= startIndex) endIndex = startIndex + 1;
            if (endIndex > numBins) endIndex = numBins;
            ranges.append({.startIndex=startIndex, .endIndex=endIndex});
        }
    };

    computeRanges(m_leftBarRanges, leftBarsCount);
    computeRanges(m_rightBarRanges, rightBarsCount);
}

#ifdef __APPLE__
CoreAudioInput::CoreAudioInput(QObject *parent) : AudioInput(parent)
{
    // Set up stream format (16-bit PCM, 44.1kHz, stereo)
    memset(&m_streamFormat, 0, sizeof(m_streamFormat));
    m_streamFormat.mSampleRate = SAMPLE_RATE;
    m_streamFormat.mFormatID = kAudioFormatLinearPCM;
    m_streamFormat.mFormatFlags = kAudioFormatFlagIsSignedInteger | kAudioFormatFlagIsPacked;
    m_streamFormat.mBitsPerChannel = 16;
    m_streamFormat.mChannelsPerFrame = 2;
    m_streamFormat.mBytesPerFrame = 4; // 2 channels * 2 bytes per channel
    m_streamFormat.mFramesPerPacket = 1;
    m_streamFormat.mBytesPerPacket = 4;
    m_streamFormat.mReserved = 0;
}

CoreAudioInput::~CoreAudioInput()
{
    if (m_isRunning) {
        stop();
    }
    cleanupAudioTap();
}

void CoreAudioInput::start()
{
    if (m_isRunning) {
        return;
    }

    if (setupAudioTap() != noErr) {
        qWarning() << "Failed to set up Core Audio Tap";
        return;
    }

    m_isRunning = true;
}

void CoreAudioInput::stop()
{
    if (!m_isRunning) {
        return;
    }

    m_isRunning = false;
    cleanupAudioTap();
}

OSStatus CoreAudioInput::audioTapCallback(void *inClientData, AudioUnitRenderActionFlags *ioActionFlags, const AudioTimeStamp *inTimeStamp, UInt32 inBusNumber, UInt32 inNumberFrames, AudioBufferList *ioData)
{
    CoreAudioInput *self = static_cast<CoreAudioInput *>(inClientData);
    
    if (!self->m_isRunning) {
        return noErr;
    }

    // Create a buffer to store the captured audio
    AudioBufferList bufferList;
    bufferList.mNumberBuffers = 1;
    bufferList.mBuffers[0].mNumberChannels = self->m_streamFormat.mChannelsPerFrame;
    bufferList.mBuffers[0].mDataByteSize = inNumberFrames * self->m_streamFormat.mBytesPerFrame;
    bufferList.mBuffers[0].mData = malloc(bufferList.mBuffers[0].mDataByteSize);

    // Render the audio into our buffer
    OSStatus status = AudioUnitRender(self->m_remoteIOUnit, ioActionFlags, inTimeStamp, inBusNumber, inNumberFrames, &bufferList);
    if (status == noErr && bufferList.mBuffers[0].mData) {
        // Convert to QByteArray and emit signal
        QByteArray data(static_cast<char *>(bufferList.mBuffers[0].mData), bufferList.mBuffers[0].mDataByteSize);
        emit self->dataReady(data);
    }

    // Clean up
    if (bufferList.mBuffers[0].mData) {
        free(bufferList.mBuffers[0].mData);
    }

    return noErr;
}

void CoreAudioInput::setupAudioTap()
{
    // Find the RemoteIO Audio Unit
    AudioComponentDescription desc;
    memset(&desc, 0, sizeof(desc));
    desc.componentType = kAudioUnitType_Output;
    desc.componentSubType = kAudioUnitSubType_RemoteIO;
    desc.componentManufacturer = kAudioUnitManufacturer_Apple;
    desc.componentFlags = 0;
    desc.componentFlagsMask = 0;

    m_remoteIOComponent = AudioComponentFindNext(nullptr, &desc);
    if (!m_remoteIOComponent) {
        qWarning() << "Failed to find RemoteIO Audio Unit";
        return;
    }

    // Create an instance of the RemoteIO unit
    OSStatus status = AudioComponentInstanceNew(m_remoteIOComponent, &m_remoteIOUnit);
    if (status != noErr) {
        qWarning() << "Failed to create RemoteIO Audio Unit instance";
        return;
    }

    // Enable input on the RemoteIO unit
    UInt32 one = 1;
    status = AudioUnitSetProperty(m_remoteIOUnit, kAudioOutputUnitProperty_EnableIO, kAudioUnitScope_Input, 1, &one, sizeof(one));
    if (status != noErr) {
        qWarning() << "Failed to enable input on RemoteIO unit";
        cleanupAudioTap();
        return;
    }

    // Set the stream format for input
    status = AudioUnitSetProperty(m_remoteIOUnit, kAudioUnitProperty_StreamFormat, kAudioUnitScope_Output, 1, &m_streamFormat, sizeof(m_streamFormat));
    if (status != noErr) {
        qWarning() << "Failed to set stream format on RemoteIO unit";
        cleanupAudioTap();
        return;
    }

    // Set the callback to tap into the audio
    AURenderCallbackStruct callbackStruct;
    callbackStruct.inputProc = audioTapCallback;
    callbackStruct.inputProcRefCon = this;
    status = AudioUnitSetProperty(m_remoteIOUnit, kAudioOutputUnitProperty_SetInputCallback, kAudioUnitScope_Global, 1, &callbackStruct, sizeof(callbackStruct));
    if (status != noErr) {
        qWarning() << "Failed to set input callback on RemoteIO unit";
        cleanupAudioTap();
        return;
    }

    // Initialize the RemoteIO unit
    status = AudioUnitInitialize(m_remoteIOUnit);
    if (status != noErr) {
        qWarning() << "Failed to initialize RemoteIO unit";
        cleanupAudioTap();
        return;
    }

    // Start the RemoteIO unit
    status = AudioOutputUnitStart(m_remoteIOUnit);
    if (status != noErr) {
        qWarning() << "Failed to start RemoteIO unit";
        cleanupAudioTap();
        return;
    }
}

void CoreAudioInput::cleanupAudioTap()
{
    if (m_remoteIOUnit) {
        AudioOutputUnitStop(m_remoteIOUnit);
        AudioUnitUninitialize(m_remoteIOUnit);
        AudioComponentInstanceDispose(m_remoteIOUnit);
        m_remoteIOUnit = nullptr;
    }
    m_remoteIOComponent = nullptr;
}
#endif
