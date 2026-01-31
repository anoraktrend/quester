#ifndef AUDIOPROCESSOR_H
#define AUDIOPROCESSOR_H

#include <QObject>
#include <QAudioSource>
#include <QAudioFormat>
#include <QIODevice>
#include <QTimer>
#include <vector>
#include <complex>

class AudioProcessor : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QList<qreal> magnitudes READ magnitudes NOTIFY magnitudesChanged)

public:
    explicit AudioProcessor(QObject *parent = nullptr);
    ~AudioProcessor();

    QList<qreal> magnitudes() const;

public slots:
    void start();
    void stop();

signals:
    void magnitudesChanged();

private slots:
    void processAudio();

private:
    void performFFT(std::vector<std::complex<float>>& data);

    QAudioSource *m_audioSource;
    QIODevice *m_audioDevice;
    QAudioFormat m_format;
    QTimer *m_timer;

    std::vector<std::complex<float>> m_fftBuffer;
    QList<qreal> m_magnitudes;

    static const int NUM_BARS = 32;
    static const int FFT_SIZE = 1024;
    static const int SAMPLE_RATE = 44100;
};

#endif // AUDIOPROCESSOR_H
