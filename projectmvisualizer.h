#ifndef PROJECTMVISUALIZER_H
#define PROJECTMVISUALIZER_H

#include <QQuickFramebufferObject>
#include <QMutex>
#include "audiovisualizer.h"

class ProjectMVisualizer : public QQuickFramebufferObject
{
    Q_OBJECT
    Q_PROPERTY(bool active READ active WRITE setActive NOTIFY activeChanged)
    Q_PROPERTY(bool shuffleEnabled READ shuffleEnabled WRITE setShuffleEnabled NOTIFY shuffleEnabledChanged)

public:
    explicit ProjectMVisualizer(QQuickItem *parent = nullptr);
    ~ProjectMVisualizer() override;

    Renderer *createRenderer() const override;

    bool active() const;
    void setActive(bool active);

    bool shuffleEnabled() const;
    void setShuffleEnabled(bool enabled);

    // Internal use for Renderer
    QByteArray takePcmData();
    QString takePresetRequest(bool &hardCut);
    bool takeShuffleRequest(bool &enabled);

    // Preset management
    Q_INVOKABLE QStringList getPresetList(const QString &presetPath) const;
    Q_INVOKABLE void selectPresetByName(const QString &presetName, bool hardCut = true);

signals:
    void activeChanged();
    void shuffleEnabledChanged();

private slots:
    void onDataReady(const QByteArray &data);
    void onError(const QString &msg);

private:
    bool m_active;
    AudioInput *m_input;
    
    mutable QMutex m_mutex;
    QByteArray m_pcmBuffer;
    bool m_presetRequested;
    QString m_requestedPreset;
    bool m_hardCut;
    bool m_shuffleEnabled;
    bool m_shuffleUpdateRequested;
    
    void startInput();
    void stopInput();
};

#endif // PROJECTMVISUALIZER_H