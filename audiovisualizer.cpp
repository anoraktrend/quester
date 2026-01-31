#include "audiovisualizer.h"
#include <QDebug>
#ifndef HAVE_CAVA
#include <QtMath>
#include <algorithm>
#endif

static const int NUM_BARS = 32;
#if !defined(HAVE_CAVA) || defined(HAVE_CAVACORE_H)
static const int FFT_SIZE = 512;
static const QString FIFO_PATH = "/tmp/mpd.fifo";
#endif

#ifdef HAVE_CAVACORE_H
static const int SAMPLE_RATE = 44100;
#endif

AudioVisualizer::AudioVisualizer(QObject *parent)
    : QObject(parent)
    , m_timer(new QTimer(this))
#if defined(HAVE_CAVA) && !defined(HAVE_CAVACORE_H)
    , m_cavaThread(nullptr)
    , m_plan(nullptr)
    , m_cava_out(nullptr)
#elif defined(HAVE_CAVACORE_H)
    , m_plan(nullptr)
    , m_cava_out(nullptr)
#endif
{
    m_magnitudes.fill(0.0, NUM_BARS);
    // Timer to sync CAVA output to QML property
#if !defined(HAVE_CAVA)
    m_fftBuffer.resize(FFT_SIZE);
#elif defined(HAVE_CAVACORE_H)
    m_cavaIn.resize(FFT_SIZE * 2); // Stereo
#endif
    connect(m_timer, &QTimer::timeout, this, &AudioVisualizer::updateMagnitudes);
}

AudioVisualizer::~AudioVisualizer()
{
    stop();
}

void AudioVisualizer::start()
{
#if defined(HAVE_CAVA) && !defined(HAVE_CAVACORE_H)
    if (m_cavaThread && m_cavaThread->isRunning())
        return;

    // Initialize CAVA plan
    // Note: API depends on libcava version. Assuming standard init.
    // We pass -1 or 0 for defaults where appropriate.
    
    // Since we can't easily pass pointers to literals in C++, we define variables.
    int bars = NUM_BARS;
    int framerate = 60;
    int sens = 100;
    int autosens = 1;
    double noise_reduction = 0.77;
    int low_cutoff = 50;
    int high_cutoff = 10000;
    int channels = 2;
    int stereo = 1; // stereo
    int monostereo = 0;
    
    // We use "fifo" input and "raw" output (or similar, effectively we just want the calculation)
    char input_method[] = "fifo";
    char input_source[] = "/tmp/mpd.fifo";
    char output_method[] = "raw"; // We don't want it to draw to terminal

    // This signature is hypothetical based on common CAVA usage. 
    // Adjust arguments as per specific libcava version installed.
    m_plan = cava_init(&bars, &framerate, &sens, &autosens, &noise_reduction, 
                       &low_cutoff, &high_cutoff, input_method, input_source, 
                       output_method, &channels, &stereo, &monostereo, NULL, NULL, NULL);

    if (m_plan) {
        m_cava_out = m_plan->out; // Assuming plan->out is the double array of magnitudes
        
        m_cavaThread = QThread::create([this]() {
            cava_execute(m_plan);
        });
        m_cavaThread->start();
        m_timer->start(16); // ~60 FPS update for UI
    }
#elif defined(HAVE_CAVACORE_H)
    // Initialize CAVACORE
    if (!m_plan) {
        m_plan = cava_init(NUM_BARS, SAMPLE_RATE, 2, 1, 0.77, 50, 10000);
        m_cava_out = (double*)malloc(sizeof(double) * NUM_BARS);
    }
    
    m_fifoFile.setFileName(FIFO_PATH);
    if (m_fifoFile.open(QIODevice::ReadOnly | QIODevice::Unbuffered)) {
        m_timer->start(16);
    } else {
        qWarning() << "Could not open MPD FIFO at" << FIFO_PATH;
    }
#else
    m_fifoFile.setFileName(FIFO_PATH);
    if (m_fifoFile.open(QIODevice::ReadOnly | QIODevice::Unbuffered)) {
        m_timer->start(33); // ~30 FPS
    } else {
        qWarning() << "Could not open MPD FIFO at" << FIFO_PATH;
    }
#endif
}

void AudioVisualizer::stop()
{
    m_timer->stop();
#if defined(HAVE_CAVA) && !defined(HAVE_CAVACORE_H)
    if (m_plan) {
        cava_destroy(m_plan);
        m_plan = nullptr;
    }
    if (m_cavaThread) {
        m_cavaThread->terminate(); // cava_execute is blocking
        m_cavaThread->wait();
        delete m_cavaThread;
        m_cavaThread = nullptr;
    }
#elif defined(HAVE_CAVACORE_H)
    if (m_plan) {
        cava_destroy(m_plan);
        m_plan = nullptr;
    }
    if(m_cava_out) {
        free(m_cava_out);
        m_cava_out = nullptr;
    }
    if (m_fifoFile.isOpen()) m_fifoFile.close();
#else
    if (m_fifoFile.isOpen()) m_fifoFile.close();
#endif
}

QList<qreal> AudioVisualizer::magnitudes() const
{
    return m_magnitudes;
}

void AudioVisualizer::updateMagnitudes()
{
#if defined(HAVE_CAVA) && !defined(HAVE_CAVACORE_H)
    if (!m_cava_out) return;

    m_magnitudes.clear();
    for (int i = 0; i < NUM_BARS; ++i) {
        // CAVA output is usually 0.0 to 1.0 (or higher depending on sens)
        double val = m_cava_out[i];
        if (val > 1.0) val = 1.0;
        if (val < 0.0) val = 0.0;
        m_magnitudes.append(val);
    }
#elif defined(HAVE_CAVACORE_H)
    if (!m_fifoFile.isOpen() || !m_plan) return;

    qint64 bytesAvailable = m_fifoFile.bytesAvailable();
    int samplesToRead = FFT_SIZE; 
    int bytesNeeded = samplesToRead * sizeof(int16_t) * 2;

    if (bytesAvailable < bytesNeeded) return;
    if (bytesAvailable > bytesNeeded * 4) {
        m_fifoFile.read(bytesAvailable - bytesNeeded);
    }

    QByteArray data = m_fifoFile.read(bytesNeeded);
    const int16_t *pcm = reinterpret_cast<const int16_t*>(data.constData());

    // Convert to double for CAVACORE
    for (int i = 0; i < samplesToRead * 2; ++i) {
        m_cavaIn[i] = (double)pcm[i] / 32768.0;
    }

    cava_execute(m_cavaIn.data(), FFT_SIZE, m_cava_out, m_plan);

    m_magnitudes.clear();
    for (int i = 0; i < NUM_BARS; ++i) {
        double val = m_cava_out[i];
        if (val > 1.0) val = 1.0;
        if (val < 0.0) val = 0.0;
        m_magnitudes.append(val);
    }
#else
    if (!m_fifoFile.isOpen()) return;

    qint64 bytesAvailable = m_fifoFile.bytesAvailable();
    int bytesNeeded = FFT_SIZE * sizeof(int16_t) * 2;
    
    if (bytesAvailable < bytesNeeded) return;

    if (bytesAvailable > bytesNeeded * 4) {
        m_fifoFile.read(bytesAvailable - bytesNeeded);
    }

    QByteArray data = m_fifoFile.read(bytesNeeded);
    const int16_t *pcm = reinterpret_cast<const int16_t*>(data.constData());

    for (int i = 0; i < FFT_SIZE; ++i) {
        float sample = (float)(pcm[2 * i] + pcm[2 * i + 1]) / 65536.0f;
        float window = 0.5f * (1.0f - qCos(2.0f * M_PI * i / (FFT_SIZE - 1)));
        m_fftBuffer[i] = std::complex<float>(sample * window, 0.0f);
    }

    performFFT(m_fftBuffer);

    m_magnitudes.clear();
    int samplesPerBar = (FFT_SIZE / 2) / NUM_BARS;
    
    for (int i = 0; i < NUM_BARS; ++i) {
        float sum = 0.0f;
        for (int j = 0; j < samplesPerBar; ++j) {
            int idx = i * samplesPerBar + j;
            if (idx < FFT_SIZE / 2) {
                sum += std::abs(m_fftBuffer[idx]);
            }
        }
        float val = sum / samplesPerBar * 5.0f;
        val = std::min(1.0f, std::max(0.0f, val));
        m_magnitudes.append(val);
    }
#endif

    emit magnitudesChanged();
}

#ifndef HAVE_CAVA
void AudioVisualizer::performFFT(std::vector<std::complex<float>>& x)
{
    const int N = x.size();
    if (N <= 1) return;

    int j = 0;
    for (int i = 1; i < N; ++i) {
        int bit = N >> 1;
        while (j & bit) {
            j ^= bit;
            bit >>= 1;
        }
        j ^= bit;
        if (i < j) std::swap(x[i], x[j]);
    }

    for (int len = 2; len <= N; len <<= 1) {
        float ang = -2.0f * M_PI / len;
        std::complex<float> wlen(qCos(ang), qSin(ang));
        for (int i = 0; i < N; i += len) {
            std::complex<float> w(1.0f, 0.0f);
            for (int k = 0; k < len / 2; ++k) {
                std::complex<float> u = x[i + k];
                std::complex<float> v = x[i + k + len / 2] * w;
                x[i + k] = u + v;
                x[i + k + len / 2] = u - v;
                w *= wlen;
            }
        }
    }
}
#endif