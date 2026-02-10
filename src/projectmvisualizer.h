#ifndef PROJECTMVISUALIZER_H
#define PROJECTMVISUALIZER_H

#include <QQuickFramebufferObject>
#include <QMutex>
#include <memory>
#include "audiovisualizer.h"

class ProjectMVisualizer : public QQuickFramebufferObject
{
    Q_OBJECT
    Q_PROPERTY(bool active READ active WRITE setActive NOTIFY activeChanged)
    Q_PROPERTY(bool shuffleEnabled READ shuffleEnabled WRITE setShuffleEnabled NOTIFY shuffleEnabledChanged)
    Q_PROPERTY(QString audioSource READ audioSource WRITE setAudioSource NOTIFY audioSourceChanged)

public:
    explicit ProjectMVisualizer(QQuickItem *parent = nullptr);
    ~ProjectMVisualizer() override;

    ProjectMVisualizer(const ProjectMVisualizer &) = delete;
    ProjectMVisualizer &operator=(const ProjectMVisualizer &) = delete;
    ProjectMVisualizer(ProjectMVisualizer &&) = delete;
    ProjectMVisualizer &operator=(ProjectMVisualizer &&) = delete;

    auto createRenderer() const -> Renderer * override;

    auto active() const -> bool;
    void setActive(bool active);

    auto shuffleEnabled() const -> bool;
    void setShuffleEnabled(bool enabled);

    auto audioSource() const -> QString;
    void setAudioSource(const QString &source);

    // Internal use for Renderer
    auto takePcmData() -> QByteArray;
    auto takePresetRequest(bool &hardCut) -> QString;
    auto takeShuffleRequest(bool &enabled) -> bool;

    // Preset management
    Q_INVOKABLE auto getPresetList(const QString &presetPath) const -> QStringList;
    Q_INVOKABLE void selectPresetByName(const QString &presetName, bool hardCut = true);

signals:
    void activeChanged();
    void shuffleEnabledChanged();
    void audioSourceChanged();

private slots:
    void onDataReady(const QByteArray &data);
    void onError(const QString &msg);

private:
    bool m_active{false};
    std::unique_ptr<AudioInput> m_input;
    
    mutable QMutex m_mutex;
    QByteArray m_pcmBuffer;
    bool m_presetRequested{false};
    QString m_requestedPreset;
    bool m_hardCut{false};
    bool m_shuffleEnabled{true};
    bool m_shuffleUpdateRequested{false};
    QString m_audioSource;
    
    void startInput();
    void stopInput();
};

#endif // PROJECTMVISUALIZER_H