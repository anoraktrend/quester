#include "audiovisualizer.h"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <QCoreApplication>
#include <QDebug>
#include <QDir>
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
constexpr int LATENCY_USEC = 20000;
constexpr int BUFFER_SIZE = 1024;
constexpr int FIFO_BUFFER_SIZE = 4096;
constexpr int FIFO_SLEEP_USEC = 10000;
constexpr int FFT_SIZE = 16384;
constexpr int DECAY_TIMER_MS = 50;
constexpr int DECAY_TIMER_FAST_MS = 20;
constexpr double MAX_PCM_VALUE = 32768.0;
constexpr double MONSTERCAT_FACTOR = 1.5;
constexpr double MONSTERCAT_SCALE = 1.25;
constexpr int MIN_BIN_INDEX = 16;
constexpr int MAX_BIN_INDEX = 3072;
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

static void monstercat_filter(
    QList<double> &bars, const MonstercatParams &params)
{
    int z = 0;
    int m_y = 0, de = 0;
    int number_of_bars = static_cast<int>(bars.size());
    if (params.waves > 0) {
        for (z = 0; z < number_of_bars; z++) { // waves
            bars[z] = bars[z] / MONSTERCAT_SCALE;
            for (m_y = z - 1; m_y >= 0; m_y--) {
                de = z - m_y;
                bars[m_y] = max(bars[z] - pow(de, 2), bars[m_y]);
            }
            for (m_y = z + 1; m_y < number_of_bars; m_y++) {
                de = m_y - z;
                bars[m_y] = max(bars[z] - pow(de, 2), bars[m_y]);
            }
        }
    } else if (params.monstercat > 0) {
        for (z = 0; z < number_of_bars; z++) {
            for (m_y = z - 1; m_y >= 0; m_y--) {
                de = z - m_y;
                bars[m_y] = max(bars[z] / pow(params.monstercat, de), bars[m_y]);
            }
            for (m_y = z + 1; m_y < number_of_bars; m_y++) {
                de = m_y - z;
                bars[m_y] = max(bars[z] / pow(params.monstercat, de), bars[m_y]);
            }
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
        emit error("pa_threaded_mainloop_new() failed");
        return;
    }
    pa_mainloop_api *api = pa_threaded_mainloop_get_api(m_mainloop);
    m_context = pa_context_new(api, "Quester");

    createContext();

    if (pa_threaded_mainloop_start(m_mainloop) < 0) {
        emit error("pa_threaded_mainloop_start() failed");
    }
}

void PulseAudioInput::createContext()
{
    pa_context_set_state_callback(m_context, context_state_callback, this);
    if (pa_context_connect(m_context, nullptr, PA_CONTEXT_NOFLAGS, nullptr) < 0) {
        emit error("pa_context_connect() failed");
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

void PulseAudioInput::server_info_callback(pa_context *c, const pa_server_info *i, void *userdata)
{
    auto *p = static_cast<PulseAudioInput *>(userdata);
    if (!i || p->m_quit || p->m_stream) {
        return;
    }

    QString monitor_source = QString(i->default_sink_name) + ".monitor";
    p->createStream(monitor_source.toUtf8().constData());
}

void PulseAudioInput::sink_input_info_callback(
    pa_context *c, const pa_sink_input_info *i, int eol, void *userdata)
{
    auto *p = static_cast<PulseAudioInput *>(userdata);
    if (p->m_quit)
        return;

    if (eol > 0) {
        // If we haven't found a stream yet, fallback to default
        if (!p->m_stream) {
            pa_operation *o = pa_context_get_server_info(c, server_info_callback, p);
            if (o)
                pa_operation_unref(o);
        }
        return;
    }

    if (i) {
        const char *media_name = pa_proplist_gets(i->proplist, "media.name");
        if (media_name && strcmp(media_name, "mpd") == 0) {
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
    pa_context *c, const pa_sink_info *i, int eol, void *userdata)
{
    auto *p = static_cast<PulseAudioInput *>(userdata);
    if (eol > 0 || !i || p->m_quit || p->m_stream) {
        return;
    }

    p->createStream(i->monitor_source_name);
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
        emit error("pa_stream_new() failed");
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
        emit error(pa_strerror(pa_context_errno(m_context)));
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

void PulseAudioInput::stream_read_callback(pa_stream *s, size_t length, void *userdata)
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
            emit p->dataReady(buffer);
        } else {
            // Hole in stream, fill with silence
            QByteArray buffer(static_cast<qsizetype>(peek_length), 0);
            emit p->dataReady(buffer);
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

void PipeWireInput::start()
{
    pw_init(nullptr, nullptr);
    m_loop = pw_thread_loop_new("quester-pipewire", nullptr);
    m_context = pw_context_new(pw_thread_loop_get_loop(m_loop), nullptr, 0);
    m_core = pw_context_connect(m_context, nullptr, 0);

    std::array<uint8_t, BUFFER_SIZE> buffer{};
    struct spa_pod_builder b = SPA_POD_BUILDER_INIT(buffer.data(), buffer.size());
    std::array<const struct spa_pod *, 1> params{};

    struct spa_audio_info_raw info = {};
    info.format = SPA_AUDIO_FORMAT_S16_LE;
    info.rate = SAMPLE_RATE;
    info.channels = 2;

    params[0] = spa_format_audio_raw_build(&b, SPA_PARAM_EnumFormat, &info);

    static const struct pw_stream_events stream_events = {
        .version = PW_VERSION_STREAM_EVENTS,
        .process = on_process
    };

    m_stream = pw_stream_new_simple(
        pw_thread_loop_get_loop(m_loop),
        "Quester",
        pw_properties_new(PW_KEY_MEDIA_TYPE, "Audio", // NOLINT(cppcoreguidelines-pro-type-vararg)
                          PW_KEY_MEDIA_CATEGORY, "Capture",
                          PW_KEY_MEDIA_ROLE, "Music",
                          NULL),
        &stream_events,
        this
    );

    pw_stream_connect(m_stream,
                      PW_DIRECTION_INPUT,
                      PW_ID_ANY,
                      (pw_stream_flags)(PW_STREAM_FLAG_AUTOCONNECT | PW_STREAM_FLAG_MAP_BUFFERS | PW_STREAM_FLAG_RT_PROCESS), // NOLINT(clang-analyzer-optin.core.EnumCastOutOfRange, cppcoreguidelines-pro-type-cstyle-cast)
                      params.data(), 1);

    pw_thread_loop_start(m_loop);
}

void PipeWireInput::stop()
{
    stopImpl();
}

void PipeWireInput::stopImpl()
{
    if (m_loop) {
        pw_thread_loop_stop(m_loop);
    }
    if (m_stream) {
        pw_stream_destroy(m_stream);
        m_stream = nullptr;
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

void PipeWireInput::on_process(void *userdata)
{
    auto *p = static_cast<PipeWireInput *>(userdata);
    struct pw_buffer *b = nullptr;
    struct spa_buffer *buf = nullptr;
    uint8_t *data = nullptr;
    uint32_t size = 0;

    b = pw_stream_dequeue_buffer(p->m_stream);
    if (b == nullptr) return;

    buf = b->buffer;
    data = static_cast<uint8_t*>(buf->datas[0].data); // NOLINT(cppcoreguidelines-pro-bounds-pointer-arithmetic)
    if (data == nullptr) return;
    size = buf->datas[0].chunk->size; // NOLINT(cppcoreguidelines-pro-bounds-pointer-arithmetic)

    if (size > 0) {
        QByteArray bytes(reinterpret_cast<const char*>(data), static_cast<qsizetype>(size)); // NOLINT(cppcoreguidelines-pro-type-reinterpret-cast)
        emit p->dataReady(bytes);
    }

    pw_stream_queue_buffer(p->m_stream, b);
}

// --- FifoInput Implementation ---

FifoInput::FifoInput(QString path, QObject *parent) : AudioInput(parent), m_path(std::move(path)), m_running(false) {}

FifoInput::~FifoInput()
{
    stopImpl();
}

void FifoInput::start()
{
    m_running = true;
    m_thread = std::thread([this]() -> void {
        int fd = open(m_path.toUtf8().constData(), O_RDONLY); // NOLINT(cppcoreguidelines-pro-type-vararg)
        if (fd < 0) {
            emit error("Could not open FIFO: " + m_path);
            return;
        }
        std::array<char, FIFO_BUFFER_SIZE> buffer{};
        while (m_running) {
            ssize_t bytes = read(fd, buffer.data(), buffer.size());
            if (bytes > 0) {
                emit dataReady(QByteArray(buffer.data(), static_cast<qsizetype>(bytes)));
            } else {
                usleep(FIFO_SLEEP_USEC);
            }
        }
        close(fd);
    });
}

void FifoInput::stop()
{
    stopImpl();
}

void FifoInput::stopImpl()
{
    m_running = false;
    if (m_thread.joinable()) {
        m_thread.join();
    }
}

// --- AudioVisualizer Implementation ---

AudioVisualizer::AudioVisualizer(QObject *parent)
    : QObject(parent)
    , m_input(nullptr)
    , m_active(false), m_decayTimer(new QTimer(this))
    , m_fftw_plan(nullptr)
    , m_fftw_in(nullptr)
    , m_fftw_out(nullptr)
    , m_fft_size(FFT_SIZE)
{
    
    connect(m_decayTimer, &QTimer::timeout, this, &AudioVisualizer::performDecay);
    m_magnitudes.fill(0.0, m_numBars);
    m_smoothBuffer.fill(0.0, m_numBars);
    loadPresets();

    // Ensure System preset exists
    if (!m_presets.contains("System")) {
        Preset system;
        system.colors = {QColor(Qt::gray)};
        m_presets.insert("System", system);
    }

    QSettings settings("Quester", "Quester");
    m_topDown = settings.value("visualizerTopDown", false).toBool();
    QString saved = settings.value("visualizerPreset", "System").toString();
    if (m_presets.contains(saved)) {
        m_currentPresetName = saved;
    } else {
        m_currentPresetName = "rainbow";
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

void AudioVisualizer::setWidth(int width)
{
    if (m_width == width)
        return;

    m_width = width;
    int newNumBars = std::max(1, m_width / 4); // 4 pixels per bar
    if (m_numBars != newNumBars) {
        m_numBars = newNumBars;

        m_magnitudes.fill(0.0, m_numBars);
        m_smoothBuffer.fill(0.0, m_numBars);
        updateBarColors();
        emit magnitudesChanged();
    }
    emit widthChanged();
}

auto AudioVisualizer::height() const -> int
{
    return m_height;
}

void AudioVisualizer::setHeight(int height)
{
    if (m_height == height) return;
    m_height = height;
    emit heightChanged();
}

auto AudioVisualizer::topDownMode() const -> bool
{
    return m_topDown;
}

void AudioVisualizer::setTopDownMode(bool topDownMode)
{
    if (m_topDown == topDownMode) return;
    m_topDown = topDownMode;
    QSettings settings("Quester", "Quester");
    settings.setValue("visualizerTopDown", m_topDown);
    emit topDownModeChanged();
}

void AudioVisualizer::start()
{
    if (m_active)
        return;

    m_maxPeak = 100.0;
    m_fft_size = FFT_SIZE;
    m_fftw_in = static_cast<double *>(fftw_malloc(sizeof(double) * m_fft_size));
    m_fftw_out = static_cast<fftw_complex *>(fftw_malloc(sizeof(fftw_complex) * (m_fft_size / 2 + 1)));

    if (!m_fftw_in || !m_fftw_out) {
        qWarning() << "Failed to allocate FFTW buffers";
        stop();
        return;
    }

    m_fftw_plan = fftw_plan_dft_r2c_1d(m_fft_size, m_fftw_in, m_fftw_out, FFTW_MEASURE);
    if (!m_fftw_plan) {
        qWarning() << "Failed to create FFTW plan";
        stop();
        return;
    }

    m_buffer.clear();
    m_smoothBuffer.fill(0.0, m_numBars);

    QSettings settings("Quester", "Quester");
    QString source = settings.value("audioSource", "pulseaudio").toString();

    if (source == "pipewire") {
        m_input = new PipeWireInput(this); // NOLINT(cppcoreguidelines-owning-memory)
    } else if (source == "fifo") {
        QString path = settings.value("fifoPath", "/tmp/mpd.fifo").toString();
        m_input = new FifoInput(path, this); // NOLINT(cppcoreguidelines-owning-memory)
    } else {
        m_input = new PulseAudioInput(this); // NOLINT(cppcoreguidelines-owning-memory)
    }

    connect(
        m_input,
        &AudioInput::dataReady,
        this,
        &AudioVisualizer::onDataReady,
        Qt::QueuedConnection);
    connect(m_input, &AudioInput::error, this, &AudioVisualizer::onPulseError);

    m_input->start();

    m_active = true;
    emit activeChanged();
    m_decayTimer->start(DECAY_TIMER_MS);
}

void AudioVisualizer::stop()
{
    if (!m_active)
        return;
    m_active = false;
    emit activeChanged();

    if (m_input) {
        m_input->stop();
        delete m_input;
        m_input = nullptr;
    }

    if (m_fftw_plan) {
        fftw_destroy_plan(m_fftw_plan);
        m_fftw_plan = nullptr;
    }
    if (m_fftw_in) {
        fftw_free(m_fftw_in);
        m_fftw_in = nullptr;
    }
    if (m_fftw_out) {
        fftw_free(m_fftw_out);
        m_fftw_out = nullptr;
    }
}

void AudioVisualizer::onDataReady(const QByteArray &data)
{
    if (!m_active || !m_fftw_plan) {
        return;
    }

    m_buffer.append(data);

    // We need m_fft_size samples. Stereo (2 channels), 16-bit (2 bytes) = 4 bytes per frame.
    int frameSize = 4;
    int requiredBytes = m_fft_size * frameSize;

    if (m_buffer.size() < requiredBytes) {
        return;
    }

    // Keep only the latest requiredBytes
    if (m_buffer.size() > requiredBytes) {
        m_buffer = m_buffer.right(requiredBytes);
    }

    const auto *pcm = reinterpret_cast<const int16_t *>(m_buffer.constData()); // NOLINT(cppcoreguidelines-pro-type-reinterpret-cast)

    QList<double> bars;
    bars.fill(0.0, m_numBars);

    int numBins = m_fft_size / 2 + 1;
    // Skip DC and very low frequencies
    int minBin = MIN_BIN_INDEX;
    int maxBin = MAX_BIN_INDEX;

    double currentFrameMax = 0.0;

    int leftBarsCount = m_numBars / 2;
    int rightBarsCount = m_numBars - leftBarsCount;

    for (int channel = 0; channel < 2; ++channel) {
        // 0 = Left, 1 = Right

        for (int i = 0; i < m_fft_size; ++i) {
            double sample = (double) pcm[2 * i + channel] / MAX_PCM_VALUE; // NOLINT(cppcoreguidelines-pro-bounds-pointer-arithmetic)
            double window = HANN_MULTIPLIER * (1.0 - std::cos(CIRCLE_RAD * M_PI * i / (m_fft_size - 1)));
            m_fftw_in[i] = sample * window; // NOLINT(cppcoreguidelines-pro-bounds-pointer-arithmetic)
        }

        fftw_execute(m_fftw_plan);

        int barCount = (channel == 0) ? leftBarsCount : rightBarsCount;
        int offset = (channel == 0) ? 0 : leftBarsCount;
        bool reverse = (channel == 0); // Reverse left channel to put bass in center

        for (int i = 0; i < barCount; i++) {
            // Logarithmic interpolation
            double start = minBin * std::pow((double) maxBin / minBin, (double) i / barCount);
            double end = minBin * std::pow((double) maxBin / minBin, (double) (i + 1) / barCount);

            int startIndex = (int) start;
            int endIndex = (int) end;

            if (endIndex <= startIndex)
                endIndex = startIndex + 1;
            if (endIndex > numBins)
                endIndex = numBins;

            double maxMag = 0.0;
            for (int b = startIndex; b < endIndex; ++b) {
                double re = m_fftw_out[b][0]; // NOLINT(cppcoreguidelines-pro-bounds-pointer-arithmetic)
                double im = m_fftw_out[b][1]; // NOLINT(cppcoreguidelines-pro-bounds-pointer-arithmetic)
                double mag = std::sqrt(re * re + im * im);
                if (mag > maxMag)
                    maxMag = mag;
            }

            // CAVA-style normalization: multiply by log2(index + 2) to boost higher frequencies
            maxMag *= std::log2(i + 2);

            if (maxMag > currentFrameMax) {
                currentFrameMax = maxMag;
            }

            int targetIdx = reverse ? (offset + barCount - 1 - i) : (offset + i);
            bars[targetIdx] = maxMag;
        }
    }

    // Dynamic scaling (AGC)
    m_maxPeak *= PEAK_DECAY_RATE; // Slow decay
    if (m_maxPeak < NOISE_FLOOR) m_maxPeak = NOISE_FLOOR; // Noise floor
    if (currentFrameMax > m_maxPeak) m_maxPeak = currentFrameMax;

    // Normalize
    for (int i = 0; i < m_numBars; i++) {
        bars[i] /= m_maxPeak;
    }

    auto h = (double)m_height;
    if (h < 1.0) h = 1.0;

    for (int i = 0; i < m_numBars; i++) {
        bars[i] *= h;
    }

    monstercat_filter(bars, {0, MONSTERCAT_FACTOR, (int)h});

    for (int i = 0; i < m_numBars; i++) {
        bars[i] /= h;
    }

    m_magnitudes.clear();
    if (m_smoothBuffer.size() != m_numBars) {
        m_smoothBuffer.fill(0.0, m_numBars);
    }

    for (int i = 0; i < m_numBars; i++) {
        double val = bars[i];
        double &smoothVal = m_smoothBuffer[i];

        // Apply smoothing: Fast attack (0.4), Slow decay (0.85)
        double factor = (val > smoothVal) ? SMOOTHING_ATTACK : SMOOTHING_DECAY;
        smoothVal = smoothVal * factor + val * (1.0 - factor);

        val = std::min(1.0, std::max(0.0, smoothVal));
        m_magnitudes.append(val);
    }

    emit magnitudesChanged();
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

    emit magnitudesChanged();

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
                colorsArray = obj["colors"].toArray();
                QJsonArray weightsArray = obj["weights"].toArray();
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
        = {"/home/lucy/git/Quester/visualizerGradients/presets.json", // Hardcoded dev path
           QCoreApplication::applicationDirPath() + "/visualizerGradients/presets.json",
           QCoreApplication::applicationDirPath() + "/../visualizerGradients/presets.json",
           "visualizerGradients/presets.json"};

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
    locations << "/etc/config";

    // Generic Data (e.g. /usr/share)
    QStringList dataLocs = QStandardPaths::standardLocations(QStandardPaths::GenericDataLocation);
    std::reverse(dataLocs.begin(), dataLocs.end());
    locations << dataLocs;

    // Config (e.g. /etc/xdg, ~/.config)
    QStringList configLocs = QStandardPaths::standardLocations(QStandardPaths::ConfigLocation);
    std::reverse(configLocs.begin(), configLocs.end());
    locations << configLocs;

    for (const QString &location : locations) {
        QDir presetDir(location + "/Quester/presets");
        if (presetDir.exists()) {
            const auto fileInfos = presetDir.entryInfoList(QStringList() << "*.json", QDir::Files);
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

    emit presetsChanged();
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

    QSettings settings("Quester", "Quester");
    settings.setValue("visualizerPreset", name);

    updateBarColors();
    emit currentPresetChanged();
}

auto AudioVisualizer::barColors() const -> QVariantList
{
    return m_barColors;
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
        emit barColorsChanged();
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
    emit barColorsChanged();
}

void AudioVisualizer::updateSystemColors(const QColor &highlight, const QColor &text)
{
    Preset system;
    system.colors = {highlight, text};
    m_presets.insert("System", system);

    if (m_currentPresetName == "System") {
        updateBarColors();
    }
}