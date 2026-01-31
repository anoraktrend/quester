#include "audiovisualizer.h"
#include <QDebug>
#include <QtMath>
#include <algorithm>
#include <cmath>

static const int NUM_BARS = 32;
static const int FFT_SIZE = 2048;
static const QString FIFO_PATH = "/tmp/mpd.fifo";

AudioVisualizer::AudioVisualizer(QObject *parent)
    : QObject(parent)
    , m_timer(new QTimer(this))
    , m_active(false)
{
    m_magnitudes.fill(0.0, NUM_BARS);

    m_fftwIn = (fftw_complex*) fftw_malloc(sizeof(fftw_complex) * FFT_SIZE);
    m_fftwOut = (fftw_complex*) fftw_malloc(sizeof(fftw_complex) * FFT_SIZE);
    m_fftwPlan = fftw_plan_dft_1d(FFT_SIZE, m_fftwIn, m_fftwOut, FFTW_FORWARD, FFTW_ESTIMATE);

    for (int i = 0; i < FFT_SIZE; ++i) {
        m_fftwIn[i][0] = 0.0;
        m_fftwIn[i][1] = 0.0;
    }

    connect(m_timer, &QTimer::timeout, this, &AudioVisualizer::updateMagnitudes);
}

AudioVisualizer::~AudioVisualizer()
{
    stop();
    fftw_destroy_plan(m_fftwPlan);
    fftw_free(m_fftwIn);
    fftw_free(m_fftwOut);
}

void AudioVisualizer::start()
{
    m_buffer.clear();
    m_timer->start(33); // ~30 FPS
}

void AudioVisualizer::stop()
{
    m_timer->stop();
    if (m_fifoFile.isOpen()) m_fifoFile.close();
}

QList<qreal> AudioVisualizer::magnitudes() const
{
    return m_magnitudes;
}

bool AudioVisualizer::active() const
{
    return m_active;
}

void AudioVisualizer::updateMagnitudes()
{
    bool wasActive = m_active;
    if (!m_fifoFile.isOpen()) {
        m_fifoFile.setFileName(FIFO_PATH);
        if (m_fifoFile.exists()) {
            m_fifoFile.open(QIODevice::ReadOnly | QIODevice::Unbuffered);
        }
    }

    m_active = m_fifoFile.isOpen();
    if (m_active != wasActive) emit activeChanged();
    if (!m_active) return;

    int bytesNeeded = FFT_SIZE * sizeof(int16_t) * 2;

    if (m_fifoFile.bytesAvailable() > 0) {
        m_buffer.append(m_fifoFile.readAll());
    } else {
        // If no data is available (e.g. paused), shift the buffer with silence to decay the visualizer
        if (!m_buffer.isEmpty()) {
            int shift = FFT_SIZE * 2; // Shift a moderate amount
            if (m_buffer.size() > shift)
                m_buffer = m_buffer.mid(shift);
            m_buffer.append(std::min(shift, bytesNeeded), 0);
        }
    }

    if (m_buffer.size() < bytesNeeded)
        return;

    if (m_buffer.size() > bytesNeeded) {
        m_buffer = m_buffer.right(bytesNeeded);
    }

    const int16_t *pcm = reinterpret_cast<const int16_t*>(m_buffer.constData());

    for (int i = 0; i < FFT_SIZE; ++i) {
        double sample = (double)(pcm[2 * i] + pcm[2 * i + 1]) / 65536.0;
        double window = 0.5 * (1.0 - qCos(2.0 * M_PI * i / (FFT_SIZE - 1)));
        m_fftwIn[i][0] = sample * window;
        m_fftwIn[i][1] = 0.0;
    }

    fftw_execute(m_fftwPlan);

    m_magnitudes.clear();
    int samplesPerBar = (FFT_SIZE / 2) / NUM_BARS;
    
    for (int i = 0; i < NUM_BARS; ++i) {
        double sum = 0.0;
        for (int j = 0; j < samplesPerBar; ++j) {
            int idx = i * samplesPerBar + j;
            if (idx < FFT_SIZE / 2) {
                double re = m_fftwOut[idx][0];
                double im = m_fftwOut[idx][1];
                sum += std::sqrt(re * re + im * im);
            }
        }
        double val = sum / samplesPerBar * 20.0;
        val = std::min(1.0, std::max(0.0, val));
        m_magnitudes.append(val);
    }

    emit magnitudesChanged();
}