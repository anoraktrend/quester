#include "audiovisualizer.h"
#include <QDebug>
#include <QMutexLocker>
#include <QtMath>
#include <algorithm>
#include <cmath>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QCoreApplication>
#include <iostream>
#include <QSettings>
#include <QStandardPaths>
#include <QDir>



template <class T> const T& max(const T& a, const T& b) {
    return (a < b) ? b : a;
}

static double* monstercat_filter(double* bars, int number_of_bars, int waves, double monstercat,
                                int height) {
    int z;
    int m_y, de;
    double height_normalizer = 1.0;
    if (height > 1000) {
        height_normalizer = height / 912.76;
    }
    if (waves > 0) {
        for (z = 0; z < number_of_bars; z++) { // waves
            bars[z] = bars[z] / 1.25;
            for (m_y = z - 1; m_y >= 0; m_y--) {
                de = z - m_y;
                bars[m_y] = max(bars[z] - height_normalizer * pow(de, 2), bars[m_y]);
            }
            for (m_y = z + 1; m_y < number_of_bars; m_y++) {
                de = m_y - z;
                bars[m_y] = max(bars[z] - height_normalizer * pow(de, 2), bars[m_y]);
            }
        }
    } else if (monstercat > 0) {
        for (z = 0; z < number_of_bars; z++) {
            for (m_y = z - 1; m_y >= 0; m_y--) {
                de = z - m_y;
                bars[m_y] = max(bars[z] / pow(monstercat * 1.5, de), bars[m_y]);
            }
            for (m_y = z + 1; m_y < number_of_bars; m_y++) {
                de = m_y - z;
                bars[m_y] = max(bars[z] / pow(monstercat * 1.5, de), bars[m_y]);
            }
        }
    }
    return bars;
}

// --- PulseAudioThread Implementation ---

PulseAudioThread::PulseAudioThread(QObject *parent) : QObject(parent) {}

PulseAudioThread::~PulseAudioThread() {
    stop();
}

void PulseAudioThread::stop() {
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

void PulseAudioThread::start() {
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

void PulseAudioThread::createContext() {
    pa_context_set_state_callback(m_context, context_state_callback, this);
    if (pa_context_connect(m_context, nullptr, PA_CONTEXT_NOFLAGS, nullptr) < 0) {
        emit error("pa_context_connect() failed");
    }
}

void PulseAudioThread::context_state_callback(pa_context *c, void *userdata) {
    auto *p = static_cast<PulseAudioThread*>(userdata);
    if (p->m_quit) return;

    switch (pa_context_get_state(c)) {
        case PA_CONTEXT_READY: {
            pa_operation* o = pa_context_get_sink_input_info_list(c, sink_input_info_callback, p);
            if (o) pa_operation_unref(o);
            break;
        }
        case PA_CONTEXT_FAILED:
        case PA_CONTEXT_TERMINATED:
            if (p->m_mainloop) pa_threaded_mainloop_stop(p->m_mainloop);
            break;
        default:
            break;
    }
}

void PulseAudioThread::server_info_callback(pa_context *c, const pa_server_info *i, void *userdata) {
    auto *p = static_cast<PulseAudioThread*>(userdata);
    if (!i || p->m_quit || p->m_stream) {
        return;
    }

    QString monitor_source = QString(i->default_sink_name) + ".monitor";
    p->createStream(monitor_source.toUtf8().constData());
}

void PulseAudioThread::sink_input_info_callback(pa_context *c, const pa_sink_input_info *i, int eol, void *userdata) {
    auto *p = static_cast<PulseAudioThread*>(userdata);
    if (p->m_quit) return;

    if (eol > 0) {
        // If we haven't found a stream yet, fallback to default
        if (!p->m_stream) {
            pa_operation* o = pa_context_get_server_info(c, server_info_callback, p);
            if (o) pa_operation_unref(o);
        }
        return;
    }

    if (i) {
        const char* media_name = pa_proplist_gets(i->proplist, "media.name");
        if (media_name && strcmp(media_name, "mpd") == 0) {
            if (!p->m_stream) { // Check if we already created a stream
                pa_operation* o = pa_context_get_sink_info_by_index(c, i->sink, sink_info_callback, p);
                if (o) pa_operation_unref(o);
            }
        }
    }
}

void PulseAudioThread::sink_info_callback(pa_context *c, const pa_sink_info *i, int eol, void *userdata) {
    auto *p = static_cast<PulseAudioThread*>(userdata);
    if (eol > 0 || !i || p->m_quit || p->m_stream) {
        return;
    }
    
    p->createStream(i->monitor_source_name);
}

void PulseAudioThread::createStream(const char* deviceName) {
    if (m_stream || m_quit) return; // Don't create if already exists or if quitting

    pa_sample_spec ss;
    ss.format = PA_SAMPLE_S16LE;
    ss.rate = 44100;
    ss.channels = 2;

    m_stream = pa_stream_new(m_context, "Quester Record", &ss, nullptr);
    if (!m_stream) {
        emit error("pa_stream_new() failed");
        if (m_mainloop) pa_threaded_mainloop_stop(m_mainloop);
        return;
    }

    pa_stream_set_state_callback(m_stream, stream_state_callback, this);
    pa_stream_set_read_callback(m_stream, stream_read_callback, this);

    pa_buffer_attr bufattr;
    bufattr.maxlength = (uint32_t)-1;
    bufattr.tlength = (uint32_t)-1;
    bufattr.prebuf = (uint32_t)-1;
    bufattr.minreq = (uint32_t)-1;
    // Low latency: 20ms fragments
    bufattr.fragsize = pa_usec_to_bytes(20000, &ss);

    if (pa_stream_connect_record(m_stream, deviceName, &bufattr, (pa_stream_flags_t)(PA_STREAM_ADJUST_LATENCY | PA_STREAM_AUTO_TIMING_UPDATE | PA_STREAM_START_CORKED)) < 0) {
        emit error(pa_strerror(pa_context_errno(m_context)));
        if (m_mainloop) pa_threaded_mainloop_stop(m_mainloop);
    }
}

void PulseAudioThread::stream_state_callback(pa_stream *s, void *userdata) {
    auto *p = static_cast<PulseAudioThread*>(userdata);
    if (p->m_quit) return;
    
    switch (pa_stream_get_state(s)) {
        case PA_STREAM_READY:
             pa_stream_cork(s, 0, NULL, NULL);
            break;
        case PA_STREAM_FAILED:
        case PA_STREAM_TERMINATED:
            if (p->m_mainloop) pa_threaded_mainloop_stop(p->m_mainloop);
            break;
        default:
            break;
    }
}

void PulseAudioThread::stream_read_callback(pa_stream *s, size_t length, void *userdata) {
    auto *p = static_cast<PulseAudioThread*>(userdata);
    if (p->m_quit) return;

    const void *data;
    if (pa_stream_peek(s, &data, &length) < 0) {
        return;
    }

    if (length > 0) {
        QByteArray buffer(static_cast<const char*>(data), length);
        emit p->dataReady(buffer);
    }

    pa_stream_drop(s);
}

// --- AudioVisualizer Implementation ---

AudioVisualizer::AudioVisualizer(QObject *parent)
    : QObject(parent)
    , m_pulseThread(nullptr)
    , m_active(false)
    , m_fftw_plan(nullptr)
    , m_fftw_in(nullptr)
    , m_fftw_out(nullptr)
    , m_fft_size(4096)
{
    m_magnitudes.fill(0.0, m_numBars);
    m_smoothBuffer.fill(0.0, m_numBars);
    loadPresets();

    // Ensure System preset exists
    if (!m_presets.contains("System")) {
        Preset system;
        system.colors = { QColor(Qt::gray) };
        m_presets.insert("System", system);
    }
    
    QSettings settings("Quester", "Quester");
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

int AudioVisualizer::width() const
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

void AudioVisualizer::start()
{
    if(m_active) return;

    m_fft_size = 4096;
    m_fftw_in = (double*) fftw_malloc(sizeof(double) * m_fft_size);
    m_fftw_out = (fftw_complex*) fftw_malloc(sizeof(fftw_complex) * (m_fft_size / 2 + 1));
    
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
    m_pulseThread = new PulseAudioThread(this);
    connect(m_pulseThread, &PulseAudioThread::dataReady, this, &AudioVisualizer::onDataReady, Qt::QueuedConnection);
    connect(m_pulseThread, &PulseAudioThread::error, this, &AudioVisualizer::onPulseError);
    connect(m_pulseThread, &PulseAudioThread::finished, [this](){
        if(m_active) {
            m_active = false;
            emit activeChanged();
        }
    });

    m_pulseThread->start();

    m_active = true;
    emit activeChanged();
}

void AudioVisualizer::stop()
{
    if(!m_active) return;
    m_active = false;
    emit activeChanged();

    if (m_pulseThread) {
        m_pulseThread->stop();
        delete m_pulseThread;
        m_pulseThread = nullptr;
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

    const int16_t *pcm = reinterpret_cast<const int16_t*>(m_buffer.constData());

    for (int i = 0; i < m_fft_size; ++i) {
        // Average stereo channels to mono and normalize
        double sample = (double)(pcm[2 * i] + pcm[2 * i + 1]) / 2.0 / 32768.0;
        
        // Apply Hanning window
        double window = 0.5 * (1.0 - std::cos(2.0 * M_PI * i / (m_fft_size - 1)));
        m_fftw_in[i] = sample * window;
    }
    
    fftw_execute(m_fftw_plan);

    QList<double> bars;
    bars.fill(0.0, m_numBars);

    int numBins = m_fft_size / 2 + 1;
    // Skip DC and very low frequencies
    int minBin = 2;
    int maxBin = numBins;

    for (int i = 0; i < m_numBars; i++) {
        // Logarithmic interpolation
        double start = minBin * std::pow((double)maxBin / minBin, (double)i / m_numBars);
        double end = minBin * std::pow((double)maxBin / minBin, (double)(i + 1) / m_numBars);
        
        int startIndex = (int)start;
        int endIndex = (int)end;
        
        if (endIndex <= startIndex) endIndex = startIndex + 1;
        if (endIndex > numBins) endIndex = numBins;
        
        double maxMag = 0.0;
        for (int b = startIndex; b < endIndex; ++b) {
            double re = m_fftw_out[b][0];
            double im = m_fftw_out[b][1];
            double mag = std::sqrt(re * re + im * im);
            if (mag > maxMag) maxMag = mag;
        }
        
        bars[i] = maxMag / 512.0; // Scaling factor
    }

    monstercat_filter(bars.data(), m_numBars, 0, 1.5, 1.0);

    m_magnitudes.clear();
    if (m_smoothBuffer.size() != m_numBars) {
        m_smoothBuffer.fill(0.0, m_numBars);
    }

    for (int i = 0; i < m_numBars; i++) {
        double val = bars[i];
        double &smoothVal = m_smoothBuffer[i];

        // Apply smoothing: Fast attack (0.4), Slow decay (0.85)
        double factor = (val > smoothVal) ? 0.4 : 0.85;
        smoothVal = smoothVal * factor + val * (1.0 - factor);

        val = std::min(1.0, std::max(0.0, smoothVal));
        m_magnitudes.append(val);
    }

    emit magnitudesChanged();
}

void AudioVisualizer::onPulseError(const QString &errorString)
{
    qWarning() << "PulseAudio Error:" << errorString;
    stop();
}

QList<qreal> AudioVisualizer::magnitudes() const
{
    return m_magnitudes;
}

bool AudioVisualizer::active() const
{
    return m_active;
}

void AudioVisualizer::loadPresets()
{
    m_presets.clear();

    auto parsePresets = [this](const QJsonObject &root) {
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
    QStringList paths = {
        "/home/lucy/git/Quester/visualizerGradients/presets.json", // Hardcoded dev path
        QCoreApplication::applicationDirPath() + "/visualizerGradients/presets.json",
        QCoreApplication::applicationDirPath() + "/../visualizerGradients/presets.json",
        "visualizerGradients/presets.json"
    };

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

QStringList AudioVisualizer::presetNames() const
{
    return m_presets.keys();
}

QString AudioVisualizer::currentPreset() const
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

QVariantList AudioVisualizer::barColors() const
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

    if (colors.isEmpty()) return;
    if (colors.size() == 1) {
        for (int i = 0; i < m_numBars; ++i) m_barColors.append(colors.first());
        emit barColorsChanged();
        return;
    }

    // Prepare stops
    QList<double> stops;
    if (weights.size() == colors.size()) {
        double totalWeight = 0;
        for (double w : weights) totalWeight += w;
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
        int r = c1.red() + localT * (c2.red() - c1.red());
        int g = c1.green() + localT * (c2.green() - c1.green());
        int b = c1.blue() + localT * (c2.blue() - c1.blue());
        
        m_barColors.append(QColor(r, g, b));
    }
    emit barColorsChanged();
}

void AudioVisualizer::updateSystemColors(const QColor &highlight, const QColor &text)
{
    Preset system;
    system.colors = { highlight, text };
    m_presets.insert("System", system);

    if (m_currentPresetName == "System") {
        updateBarColors();
    }
}