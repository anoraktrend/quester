#include "audioprocessor.h"
#include <QDebug>
#include <QtMath>
#include <algorithm>
#include <QMediaDevices>

AudioProcessor::AudioProcessor(QObject *parent)
    : QObject(parent),
      m_audioSource(nullptr),
      m_audioDevice(nullptr),
      m_timer(new QTimer(this))
{
    m_magnitudes.fill(0.0, NUM_BARS);
    m_fftBuffer.resize(FFT_SIZE);

    m_format.setSampleRate(SAMPLE_RATE);
    m_format.setChannelCount(1);
    m_format.setSampleFormat(QAudioFormat::Int16);

    QAudioDevice info = QMediaDevices::defaultAudioInput();

    m_audioSource = new QAudioSource(info, m_format, this);
    connect(m_timer, &QTimer::timeout, this, &AudioProcessor::processAudio);
}

AudioProcessor::~AudioProcessor()
{
    stop();
}

void AudioProcessor::start()
{
    if (m_audioSource) {
        m_audioDevice = m_audioSource->start();
        m_timer->start(33); // ~30 FPS
    }
}

void AudioProcessor::stop()
{
    if (m_audioSource) {
        m_audioSource->stop();
    }
    m_timer->stop();
}

QList<qreal> AudioProcessor::magnitudes() const
{
    return m_magnitudes;
}

void AudioProcessor::processAudio()
{
    if (!m_audioDevice)
        return;

    const qint64 bytesToRead = FFT_SIZE * 2; // 16-bit samples
    QByteArray data = m_audioDevice->read(bytesToRead);

    if (data.size() < bytesToRead)
        return;

    const qint16 *pcm = reinterpret_cast<const qint16*>(data.constData());

    for (int i = 0; i < FFT_SIZE; ++i) {
        float sample = (float)(pcm[i]) / 32768.0f;
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
    emit magnitudesChanged();
}

void AudioProcessor::performFFT(std::vector<std::complex<float>>& x)
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
