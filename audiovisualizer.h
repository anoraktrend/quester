#ifndef AUDIOVISUALIZER_H
#define AUDIOVISUALIZER_H

#include <QObject>
#include <QTimer>
#include <QFile>
#include <QList>
#include <QThread>
#ifdef HAVE_CAVA
#ifdef HAVE_CAVACORE_H
#include <cava/cavacore.h>
#else
#include <cava/cava.h>
#endif
#else
#include <vector>
#include <complex>
#endif

class AudioVisualizer : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QList<qreal> magnitudes READ magnitudes NOTIFY magnitudesChanged)

public:
    explicit AudioVisualizer(QObject *parent = nullptr);
    ~AudioVisualizer();

    QList<qreal> magnitudes() const;

public slots:
    void start();
    void stop();

signals:
    void magnitudesChanged();

private slots:
    void updateMagnitudes();

private:
#if defined(HAVE_CAVA) && !defined(HAVE_CAVACORE_H)
    void runCava();
    QThread *m_cavaThread;
    struct cava_plan *m_plan;
    double *m_cava_out;
#elif defined(HAVE_CAVACORE_H)
    struct cava_plan *m_plan;
    double *m_cava_out;
    std::vector<double> m_cavaIn;
    QFile m_fifoFile;
#else
    void performFFT(std::vector<std::complex<float>>& x);
    QFile m_fifoFile;
    std::vector<std::complex<float>> m_fftBuffer;
#endif

    QTimer *m_timer;
    QList<qreal> m_magnitudes;
};

#endif // AUDIOVISUALIZER_H