#pragma once

#include <QQuickFramebufferObject>
#include <QMutex>
#include <QStringList>
#include <QByteArray>
#include <QTimer>
#include <QOpenGLFramebufferObject>

// Forward declarations of projectM opaque types (avoids including heavy headers in .h)
struct projectm;
typedef struct projectm* projectm_handle;
struct projectm_playlist;
typedef struct projectm_playlist* projectm_playlist_handle;

// ─────────────────────────────────────────────────────────────────────────────
// ProjectMItem – QQuickFramebufferObject that hosts a projectM visualizer.
//
// Lifecycle:
//   - Lives on the main (GUI) thread.
//   - createRenderer() is called on the render thread; the returned
//     ProjectMRenderer holds the projectm_handle and all GL resources.
//   - synchronize() bridges data between the two threads every frame.
//   - Audio PCM is queued via feedPcmData() (main thread) and consumed
//     inside synchronize() before projectm_opengl_render_frame() is called.
// ─────────────────────────────────────────────────────────────────────────────
class ProjectMItem : public QQuickFramebufferObject
{
    Q_OBJECT

    // ── Visualizer settings ──
    Q_PROPERTY(QString   presetPath          READ presetPath          WRITE setPresetPath          NOTIFY presetPathChanged)
    Q_PROPERTY(float     beatSensitivity     READ beatSensitivity     WRITE setBeatSensitivity     NOTIFY beatSensitivityChanged)
    Q_PROPERTY(double    softCutDuration     READ softCutDuration     WRITE setSoftCutDuration     NOTIFY softCutDurationChanged)
    Q_PROPERTY(double    presetDuration      READ presetDuration      WRITE setPresetDuration      NOTIFY presetDurationChanged)
    Q_PROPERTY(bool      hardCutEnabled      READ hardCutEnabled      WRITE setHardCutEnabled      NOTIFY hardCutEnabledChanged)
    Q_PROPERTY(float     hardCutSensitivity  READ hardCutSensitivity  WRITE setHardCutSensitivity  NOTIFY hardCutSensitivityChanged)
    Q_PROPERTY(int       meshX               READ meshX               WRITE setMeshX               NOTIFY meshXChanged)
    Q_PROPERTY(int       meshY               READ meshY               WRITE setMeshY               NOTIFY meshYChanged)
    Q_PROPERTY(bool      aspectCorrection    READ aspectCorrection    WRITE setAspectCorrection    NOTIFY aspectCorrectionChanged)
    Q_PROPERTY(int       targetFps           READ targetFps           WRITE setTargetFps           NOTIFY targetFpsChanged)
    Q_PROPERTY(bool      shuffleEnabled      READ shuffleEnabled      WRITE setShuffleEnabled      NOTIFY shuffleEnabledChanged)
    Q_PROPERTY(bool      presetLocked        READ presetLocked        WRITE setPresetLocked        NOTIFY presetLockedChanged)

    // ── Preset list / selection ──
    Q_PROPERTY(QStringList presetNames       READ presetNames         NOTIFY presetNamesChanged)
    Q_PROPERTY(int         currentPresetIndex READ currentPresetIndex WRITE setCurrentPresetIndex  NOTIFY currentPresetIndexChanged)

    // ── Runtime state ──
    Q_PROPERTY(bool      active              READ active              WRITE setActive              NOTIFY activeChanged)

public:
    explicit ProjectMItem(QQuickItem *parent = nullptr);
    ~ProjectMItem() override;

    // QQuickFramebufferObject
    Renderer *createRenderer() const override;

    // ── Property accessors ──
    QString  presetPath()          const { return m_presetPath; }
    float    beatSensitivity()     const { return m_beatSensitivity; }
    double   softCutDuration()     const { return m_softCutDuration; }
    double   presetDuration()      const { return m_presetDuration; }
    bool     hardCutEnabled()      const { return m_hardCutEnabled; }
    float    hardCutSensitivity()  const { return m_hardCutSensitivity; }
    int      meshX()               const { return m_meshX; }
    int      meshY()               const { return m_meshY; }
    bool     aspectCorrection()    const { return m_aspectCorrection; }
    int      targetFps()           const { return m_targetFps; }
    bool     shuffleEnabled()      const { return m_shuffleEnabled; }
    bool     presetLocked()        const { return m_presetLocked; }
    QStringList presetNames()      const { return m_presetNames; }
    int      currentPresetIndex()  const { return m_currentPresetIndex; }
    bool     active()              const { return m_active; }

    void setPresetPath(const QString &path);
    void setBeatSensitivity(float v);
    void setSoftCutDuration(double v);
    void setPresetDuration(double v);
    void setHardCutEnabled(bool v);
    void setHardCutSensitivity(float v);
    void setMeshX(int v);
    void setMeshY(int v);
    void setAspectCorrection(bool v);
    void setTargetFps(int v);
    void setShuffleEnabled(bool v);
    void setPresetLocked(bool v);
    void setCurrentPresetIndex(int idx);
    void setActive(bool v);

    // ── Invokable actions ──
    Q_INVOKABLE void nextPreset();
    Q_INVOKABLE void previousPreset();
    Q_INVOKABLE void saveSettings();
    Q_INVOKABLE void loadSettings();

    // ── Called by AudioVisualizer signal connection ──
    Q_INVOKABLE void setAudioVisualizerSource(QObject *audioVisualizer);

    // ─────────────────────────────────────────────────────────────────────────
    // Data shared between main thread and render thread.
    // Access is guarded by m_syncMutex; taken atomically in synchronize().
    // ─────────────────────────────────────────────────────────────────────────
    struct SyncData {
        // Settings (copied on change)
        QString  presetPath;
        float    beatSensitivity     = 1.0f;
        double   softCutDuration     = 3.0;
        double   presetDuration      = 30.0;
        bool     hardCutEnabled      = false;
        float    hardCutSensitivity  = 0.1f;
        int      meshX               = 32;
        int      meshY               = 24;
        bool     aspectCorrection    = true;
        int      targetFps           = 60;
        bool     shuffleEnabled      = true;
        bool     presetLocked        = false;
        // Dirty flags
        bool     settingsDirty = true;
        // Navigation
        bool     doNext = false;
        bool     doPrev = false;
        int      loadIndex = -1; // -1 = no explicit load
        QStringList presetFiles;  // full paths
        int      currentIndex = -1;
        // PCM payload
        QByteArray pcmData;
        bool       hasPcm = false;
    };

    // Called by renderer's synchronize() – grabs a snapshot and resets flags
    SyncData takeSyncData();
    // Called by renderer to notify current preset changed (back to main thread)
    void notifyPresetChanged(int index);

public slots:
    void feedPcmData(const QByteArray &data);

signals:
    void presetPathChanged();
    void beatSensitivityChanged();
    void softCutDurationChanged();
    void presetDurationChanged();
    void hardCutEnabledChanged();
    void hardCutSensitivityChanged();
    void meshXChanged();
    void meshYChanged();
    void aspectCorrectionChanged();
    void targetFpsChanged();
    void shuffleEnabledChanged();
    void presetLockedChanged();
    void presetNamesChanged();
    void currentPresetIndexChanged();
    void activeChanged();

private:
    void scanPresets();
    void markSettingsDirty();

    // ── Settings ──
    QString  m_presetPath;
    float    m_beatSensitivity     = 1.0f;
    double   m_softCutDuration     = 3.0;
    double   m_presetDuration      = 30.0;
    bool     m_hardCutEnabled      = false;
    float    m_hardCutSensitivity  = 0.1f;
    int      m_meshX               = 32;
    int      m_meshY               = 24;
    bool     m_aspectCorrection    = true;
    int      m_targetFps           = 60;
    bool     m_shuffleEnabled      = true;
    bool     m_presetLocked        = false;
    bool     m_active              = false;

    // ── Preset list (main thread) ──
    QStringList m_presetFiles;  // full paths
    QStringList m_presetNames;  // display names
    int         m_currentPresetIndex = -1;

    // ── Thread-shared sync data ──
    mutable QMutex m_syncMutex;
    SyncData       m_syncData;

    // ── Render loop ──
    QTimer *m_renderTimer = nullptr;
};
