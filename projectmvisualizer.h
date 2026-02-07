#ifndef PROJECTMVISUALIZER_H
#define PROJECTMVISUALIZER_H

#include <QQuickFramebufferObject>
#include <QMutex>
#include "audiovisualizer.h"

class ProjectMVisualizer : public QQuickFramebufferObject
{
    Q_OBJECT
    Q_PROPERTY(bool active READ active WRITE setActive NOTIFY activeChanged)

public:
    explicit ProjectMVisualizer(QQuickItem *parent = nullptr);
    ~ProjectMVisualizer() override;

    Renderer *createRenderer() const override;

    bool active() const;
    void setActive(bool active);

    // Internal use for Renderer
    QByteArray takePcmData();

signals:
    void activeChanged();

private slots:
    void onDataReady(const QByteArray &data);
    void onError(const QString &msg);

private:
    bool m_active;
    AudioInput *m_input;
    
    mutable QMutex m_mutex;
    QByteArray m_pcmBuffer;
    
    void startInput();
    void stopInput();
};

#endif // PROJECTMVISUALIZER_H