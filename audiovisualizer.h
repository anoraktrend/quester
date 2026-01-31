#ifndef AUDIOVISUALIZER_H
#define AUDIOVISUALIZER_H

#include <QObject>
#include <QTimer>
#include <QFile>
#include <QList>
#include <fftw3.h>

class AudioVisualizer : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QList<qreal> magnitudes READ magnitudes NOTIFY magnitudesChanged)
    Q_PROPERTY(bool active READ active NOTIFY activeChanged)

public:
    explicit AudioVisualizer(QObject *parent = nullptr);
    ~AudioVisualizer();

    QList<qreal> magnitudes() const;
    bool active() const;

public slots:
    void start();
    void stop();

signals:
    void magnitudesChanged();
    void activeChanged();

private slots:
    void updateMagnitudes();

private:
    QFile m_fifoFile;
    fftw_complex *m_fftwIn;
    fftw_complex *m_fftwOut;
    fftw_plan m_fftwPlan;

    QTimer *m_timer;
    QList<qreal> m_magnitudes;
    QByteArray m_buffer;
    bool m_active;
};

#endif // AUDIOVISUALIZER_H