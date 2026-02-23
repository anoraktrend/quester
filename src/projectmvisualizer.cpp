#include "projectmvisualizer.h"

#include <QDebug>
#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QMetaObject>
#include <QMutexLocker>
#include <QOpenGLFramebufferObject>
#include <QOpenGLFramebufferObjectFormat>
#include <QSettings>
#include <QStandardPaths>
#include <QTimer>

// projectM 4 C API
#include <projectM-4/projectM.h>
#include <projectM-4/audio.h>
#include <projectM-4/parameters.h>
#include <projectM-4/render_opengl.h>
#include <projectM-4/callbacks.h>
#include <projectM-4/playlist.h>

// ─────────────────────────────────────────────────────────────────────────────
// ProjectMRenderer – lives on the render thread
// ─────────────────────────────────────────────────────────────────────────────
class ProjectMRenderer : public QQuickFramebufferObject::Renderer
{
public:
    explicit ProjectMRenderer(ProjectMItem *item)
        : m_item(item)
    {}

    ~ProjectMRenderer() override
    {
        if (m_playlist) {
            projectm_playlist_destroy(m_playlist);
            m_playlist = nullptr;
        }
        if (m_pm) {
            projectm_destroy(m_pm);
            m_pm = nullptr;
        }
    }

    // ── Called on render thread with GL context current ──────────────────────
    QOpenGLFramebufferObject *createFramebufferObject(const QSize &size) override
    {
        QOpenGLFramebufferObjectFormat fmt;
        fmt.setAttachment(QOpenGLFramebufferObject::CombinedDepthStencil);
        fmt.setInternalTextureFormat(GL_RGBA8);

        auto *fbo = new QOpenGLFramebufferObject(size, fmt);

        if (!m_pm) {
            initProjectM(size);
        } else if (m_pm) {
            projectm_set_window_size(m_pm, static_cast<size_t>(size.width()),
                                          static_cast<size_t>(size.height()));
        }
        return fbo;
    }

    // ── Called on render thread, main thread BLOCKED ─────────────────────────
    void synchronize(QQuickFramebufferObject *fboItem) override
    {
        auto *item = static_cast<ProjectMItem *>(fboItem);
        ProjectMItem::SyncData sd = item->takeSyncData();

        if (!m_pm)
            return;

        // ── Apply settings when dirty ──
        if (sd.settingsDirty) {
            projectm_set_beat_sensitivity(m_pm, sd.beatSensitivity);
            projectm_set_soft_cut_duration(m_pm, sd.softCutDuration);
            projectm_set_preset_duration(m_pm, sd.presetDuration);
            projectm_set_hard_cut_enabled(m_pm, sd.hardCutEnabled);
            projectm_set_hard_cut_sensitivity(m_pm, sd.hardCutSensitivity);
            projectm_set_mesh_size(m_pm, static_cast<size_t>(sd.meshX),
                                         static_cast<size_t>(sd.meshY));
            projectm_set_aspect_correction(m_pm, sd.aspectCorrection);
            projectm_set_fps(m_pm, sd.targetFps);
            projectm_set_preset_locked(m_pm, sd.presetLocked);
            if (m_playlist)
                projectm_playlist_set_shuffle(m_playlist, sd.shuffleEnabled);
        }

        // ── Preset list changed (path rescan) ──
        if (sd.presetFiles != m_presetFiles) {
            m_presetFiles = sd.presetFiles;
            rebuildPlaylist();
            // If an explicit index was also requested, load it; otherwise
            // the playlist's auto-advance takes over.
            if (sd.loadIndex >= 0 && sd.loadIndex < m_presetFiles.size()) {
                loadPresetAt(sd.loadIndex, false);
            }
        } else if (sd.loadIndex >= 0 && sd.loadIndex < m_presetFiles.size()) {
            loadPresetAt(sd.loadIndex, true);
        }

        // ── Navigation ──
        if (sd.doNext && m_playlist)
            projectm_playlist_play_next(m_playlist, false);
        if (sd.doPrev && m_playlist)
            projectm_playlist_play_previous(m_playlist, false);

        // ── Feed PCM ──
        if (sd.hasPcm && !sd.pcmData.isEmpty()) {
            const auto *samples = reinterpret_cast<const int16_t *>(sd.pcmData.constData());
            unsigned int sampleCount =
                static_cast<unsigned int>(sd.pcmData.size()) / sizeof(int16_t);
            if (sampleCount > 0)
                projectm_pcm_add_int16(m_pm, samples, sampleCount, PROJECTM_STEREO);
        }
    }

    // ── Called on render thread, GL context current ───────────────────────────
    void render() override
    {
        if (m_pm)
            projectm_opengl_render_frame(m_pm);

        // Schedule the next frame
        update();
    }

private:
    void initProjectM(const QSize &size)
    {
        m_pm = projectm_create();
        if (!m_pm) {
            qWarning() << "[ProjectM] projectm_create() failed – OpenGL context not ready?";
            return;
        }

        // Apply default settings (will be overridden by synchronize on the next frame)
        projectm_set_window_size(m_pm, static_cast<size_t>(size.width()),
                                       static_cast<size_t>(size.height()));
        projectm_set_fps(m_pm, 60);
        projectm_set_beat_sensitivity(m_pm, 1.0f);
        projectm_set_soft_cut_duration(m_pm, 3.0);
        projectm_set_preset_duration(m_pm, 30.0);
        projectm_set_aspect_correction(m_pm, true);
        projectm_set_mesh_size(m_pm, 32, 24);

        // Create playlist and connect it to the projectM instance
        m_playlist = projectm_playlist_create(m_pm);
        if (!m_playlist)
            qWarning() << "[ProjectM] Could not create playlist";

        // Build the playlist from any preset files already scanned
        if (!m_presetFiles.isEmpty())
            rebuildPlaylist();

        qInfo() << "[ProjectM] Initialized successfully, viewport" << size;
    }

    void rebuildPlaylist()
    {
        if (!m_playlist)
            return;

        // Clear and re-add all presets
        projectm_playlist_clear(m_playlist);
        for (const QString &path : std::as_const(m_presetFiles))
            projectm_playlist_add_path(m_playlist, path.toUtf8().constData(),
                                       true /*allow_duplicates*/, false /*flush_async*/);

        if (projectm_playlist_size(m_playlist) > 0)
            projectm_playlist_set_position(m_playlist, 0, false);

        qDebug() << "[ProjectM] Playlist rebuilt with"
                 << projectm_playlist_size(m_playlist) << "presets";
    }

    void loadPresetAt(int index, bool smoothTransition)
    {
        if (index < 0 || index >= m_presetFiles.size())
            return;

        if (m_playlist) {
            projectm_playlist_set_position(m_playlist,
                                           static_cast<uint32_t>(index),
                                           !smoothTransition /*hard cut when not smooth*/);
        } else {
            projectm_load_preset_file(m_pm,
                                      m_presetFiles[index].toUtf8().constData(),
                                      smoothTransition);
        }

        // Notify the item which preset is now active (queued so it runs on main thread)
        int idx = index;
        ProjectMItem *item = m_item;
        QMetaObject::invokeMethod(item, [item, idx]() {
            item->notifyPresetChanged(idx);
        }, Qt::QueuedConnection);
    }

    ProjectMItem            *m_item     = nullptr;
    projectm_handle          m_pm       = nullptr;
    projectm_playlist_handle m_playlist = nullptr;
    QStringList              m_presetFiles;
};

// ─────────────────────────────────────────────────────────────────────────────
// ProjectMItem – main thread implementation
// ─────────────────────────────────────────────────────────────────────────────

ProjectMItem::ProjectMItem(QQuickItem *parent)
    : QQuickFramebufferObject(parent)
{
    // QQuickFramebufferObject renders upside-down relative to Qt Quick's Y axis
    setMirrorVertically(true);
    setFlag(QQuickItem::ItemHasContents, true);

    m_renderTimer = new QTimer(this);
    m_renderTimer->setInterval(16); // ~60 fps default
    connect(m_renderTimer, &QTimer::timeout, this, [this]() {
        if (m_active)
            update();
    });

    // Load persisted settings on construction
    loadSettings();
}

ProjectMItem::~ProjectMItem() = default;

QQuickFramebufferObject::Renderer *ProjectMItem::createRenderer() const
{
    return new ProjectMRenderer(const_cast<ProjectMItem *>(this));
}

// ─────────────────────────────────────────────────────────────────────────────
// Property setters
// ─────────────────────────────────────────────────────────────────────────────

void ProjectMItem::setPresetPath(const QString &path)
{
    if (m_presetPath == path) return;
    m_presetPath = path;
    scanPresets();
    markSettingsDirty();
    emit presetPathChanged();
    saveSettings();
}

void ProjectMItem::setBeatSensitivity(float v)
{
    if (qFuzzyCompare(m_beatSensitivity, v)) return;
    m_beatSensitivity = v;
    markSettingsDirty();
    emit beatSensitivityChanged();
}

void ProjectMItem::setSoftCutDuration(double v)
{
    if (qFuzzyCompare(m_softCutDuration, v)) return;
    m_softCutDuration = v;
    markSettingsDirty();
    emit softCutDurationChanged();
}

void ProjectMItem::setPresetDuration(double v)
{
    if (qFuzzyCompare(m_presetDuration, v)) return;
    m_presetDuration = v;
    markSettingsDirty();
    emit presetDurationChanged();
}

void ProjectMItem::setHardCutEnabled(bool v)
{
    if (m_hardCutEnabled == v) return;
    m_hardCutEnabled = v;
    markSettingsDirty();
    emit hardCutEnabledChanged();
}

void ProjectMItem::setHardCutSensitivity(float v)
{
    if (qFuzzyCompare(m_hardCutSensitivity, v)) return;
    m_hardCutSensitivity = v;
    markSettingsDirty();
    emit hardCutSensitivityChanged();
}

void ProjectMItem::setMeshX(int v)
{
    if (m_meshX == v) return;
    m_meshX = v;
    markSettingsDirty();
    emit meshXChanged();
}

void ProjectMItem::setMeshY(int v)
{
    if (m_meshY == v) return;
    m_meshY = v;
    markSettingsDirty();
    emit meshYChanged();
}

void ProjectMItem::setAspectCorrection(bool v)
{
    if (m_aspectCorrection == v) return;
    m_aspectCorrection = v;
    markSettingsDirty();
    emit aspectCorrectionChanged();
}

void ProjectMItem::setTargetFps(int v)
{
    if (m_targetFps == v) return;
    m_targetFps = v;
    if (m_renderTimer)
        m_renderTimer->setInterval(v > 0 ? 1000 / v : 16);
    markSettingsDirty();
    emit targetFpsChanged();
}

void ProjectMItem::setShuffleEnabled(bool v)
{
    if (m_shuffleEnabled == v) return;
    m_shuffleEnabled = v;
    markSettingsDirty();
    emit shuffleEnabledChanged();
}

void ProjectMItem::setPresetLocked(bool v)
{
    if (m_presetLocked == v) return;
    m_presetLocked = v;
    markSettingsDirty();
    emit presetLockedChanged();
}

void ProjectMItem::setCurrentPresetIndex(int idx)
{
    if (m_currentPresetIndex == idx) return;
    m_currentPresetIndex = idx;

    QMutexLocker lk(&m_syncMutex);
    m_syncData.loadIndex = idx;
    lk.unlock();

    update();
    emit currentPresetIndexChanged();
}

void ProjectMItem::setActive(bool v)
{
    if (m_active == v) return;
    m_active = v;
    if (v)
        m_renderTimer->start();
    else
        m_renderTimer->stop();
    emit activeChanged();
}

// ─────────────────────────────────────────────────────────────────────────────
// Invokable actions
// ─────────────────────────────────────────────────────────────────────────────

void ProjectMItem::nextPreset()
{
    QMutexLocker lk(&m_syncMutex);
    m_syncData.doNext = true;
    lk.unlock();
    update();
}

void ProjectMItem::previousPreset()
{
    QMutexLocker lk(&m_syncMutex);
    m_syncData.doPrev = true;
    lk.unlock();
    update();
}

void ProjectMItem::setAudioVisualizerSource(QObject *audioVisualizer)
{
    if (!audioVisualizer) return;
    // Connect AudioVisualizer's pcmDataReady signal directly to our slot.
    // Direct connection is safe – both objects live on the main thread.
    connect(audioVisualizer, SIGNAL(pcmDataReady(QByteArray)),
            this,             SLOT(feedPcmData(QByteArray)),
            Qt::UniqueConnection);
}

// ─────────────────────────────────────────────────────────────────────────────
// PCM feeding
// ─────────────────────────────────────────────────────────────────────────────

void ProjectMItem::feedPcmData(const QByteArray &data)
{
    if (!m_active || data.isEmpty()) return;
    QMutexLocker lk(&m_syncMutex);
    // Replace; we only need the latest chunk since projectM has its own ring buffer
    m_syncData.pcmData = data;
    m_syncData.hasPcm  = true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Sync data exchange (called on render thread, main thread blocked)
// ─────────────────────────────────────────────────────────────────────────────

ProjectMItem::SyncData ProjectMItem::takeSyncData()
{
    QMutexLocker lk(&m_syncMutex);
    SyncData snapshot = m_syncData;

    // Reset one-shot flags
    m_syncData.settingsDirty = false;
    m_syncData.doNext        = false;
    m_syncData.doPrev        = false;
    m_syncData.loadIndex     = -1;
    m_syncData.hasPcm        = false;
    m_syncData.pcmData.clear();

    return snapshot;
}

// ─────────────────────────────────────────────────────────────────────────────
// Called from render thread via QMetaObject::invokeMethod (queued)
// ─────────────────────────────────────────────────────────────────────────────

void ProjectMItem::notifyPresetChanged(int index)
{
    if (m_currentPresetIndex == index) return;
    m_currentPresetIndex = index;
    emit currentPresetIndexChanged();
}

// ─────────────────────────────────────────────────────────────────────────────
// Settings persistence
// ─────────────────────────────────────────────────────────────────────────────

void ProjectMItem::saveSettings()
{
    QSettings s(QStringLiteral("Quester"), QStringLiteral("Quester"));
    s.setValue(QStringLiteral("projectMPresetPath"),       m_presetPath);
    s.setValue(QStringLiteral("projectMBeatSensitivity"),  static_cast<double>(m_beatSensitivity));
    s.setValue(QStringLiteral("projectMSoftCutDuration"),  m_softCutDuration);
    s.setValue(QStringLiteral("projectMPresetDuration"),   m_presetDuration);
    s.setValue(QStringLiteral("projectMHardCutEnabled"),   m_hardCutEnabled);
    s.setValue(QStringLiteral("projectMHardCutSensitivity"), static_cast<double>(m_hardCutSensitivity));
    s.setValue(QStringLiteral("projectMMeshX"),            m_meshX);
    s.setValue(QStringLiteral("projectMMeshY"),            m_meshY);
    s.setValue(QStringLiteral("projectMAspectCorrection"), m_aspectCorrection);
    s.setValue(QStringLiteral("projectMFPS"),              m_targetFps);
    s.setValue(QStringLiteral("projectMShuffleEnabled"),   m_shuffleEnabled);
    s.setValue(QStringLiteral("projectMPresetIndex"),      m_currentPresetIndex);
}

void ProjectMItem::loadSettings()
{
    QSettings s(QStringLiteral("Quester"), QStringLiteral("Quester"));

    // Determine a sensible default preset path
    QString defaultPath;
    const QStringList candidates = {
        QStringLiteral("/usr/share/projectM/presets"),
        QStringLiteral("/usr/share/projectM-4/presets"),
        QStringLiteral("/usr/local/share/projectM/presets"),
    };
    for (const QString &c : candidates) {
        if (QDir(c).exists()) { defaultPath = c; break; }
    }

    // Read all settings (setters call markSettingsDirty automatically)
    QString path = s.value(QStringLiteral("projectMPresetPath"), defaultPath).toString();

    m_beatSensitivity    = static_cast<float>(s.value(QStringLiteral("projectMBeatSensitivity"),  1.0).toDouble());
    m_softCutDuration    = s.value(QStringLiteral("projectMSoftCutDuration"),  3.0).toDouble();
    m_presetDuration     = s.value(QStringLiteral("projectMPresetDuration"),   30.0).toDouble();
    m_hardCutEnabled     = s.value(QStringLiteral("projectMHardCutEnabled"),   false).toBool();
    m_hardCutSensitivity = static_cast<float>(s.value(QStringLiteral("projectMHardCutSensitivity"), 0.1).toDouble());
    m_meshX              = s.value(QStringLiteral("projectMMeshX"),            32).toInt();
    m_meshY              = s.value(QStringLiteral("projectMMeshY"),            24).toInt();
    m_aspectCorrection   = s.value(QStringLiteral("projectMAspectCorrection"), true).toBool();
    m_targetFps          = s.value(QStringLiteral("projectMFPS"),              60).toInt();
    m_shuffleEnabled     = s.value(QStringLiteral("projectMShuffleEnabled"),   true).toBool();
    m_currentPresetIndex = s.value(QStringLiteral("projectMPresetIndex"),      0).toInt();

    if (m_renderTimer)
        m_renderTimer->setInterval(m_targetFps > 0 ? 1000 / m_targetFps : 16);

    // Set the preset path last so scanPresets() can pick up all fields
    if (m_presetPath != path) {
        m_presetPath = path;
        scanPresets();
        emit presetPathChanged();
    }

    markSettingsDirty();
}

// ─────────────────────────────────────────────────────────────────────────────
// Preset directory scanning
// ─────────────────────────────────────────────────────────────────────────────

void ProjectMItem::scanPresets()
{
    m_presetFiles.clear();
    m_presetNames.clear();

    QDir dir(m_presetPath);
    if (!dir.exists()) {
        qWarning() << "[ProjectM] Preset path does not exist:" << m_presetPath;
        emit presetNamesChanged();
        return;
    }

    // Scan recursively for preset files
    const QStringList filters = {QStringLiteral("*.milk"), QStringLiteral("*.prjm")};
    QDirIterator it(m_presetPath, filters, QDir::Files, QDirIterator::Subdirectories);
    
    while (it.hasNext()) {
        it.next();
        const QFileInfo &fi = it.fileInfo();
        m_presetFiles.append(fi.absoluteFilePath());
        m_presetNames.append(fi.completeBaseName());
    }

    // Clamp saved index
    if (m_currentPresetIndex >= m_presetFiles.size())
        m_currentPresetIndex = m_presetFiles.isEmpty() ? -1 : 0;

    // Push new list to sync data
    {
        QMutexLocker lk(&m_syncMutex);
        m_syncData.presetFiles   = m_presetFiles;
        m_syncData.currentIndex  = m_currentPresetIndex;
    }

    emit presetNamesChanged();
    qInfo() << "[ProjectM] Scanned" << m_presetFiles.size()
            << "presets from" << m_presetPath;
}

// ─────────────────────────────────────────────────────────────────────────────
// Helpers
// ─────────────────────────────────────────────────────────────────────────────

void ProjectMItem::markSettingsDirty()
{
    QMutexLocker lk(&m_syncMutex);
    m_syncData.presetPath        = m_presetPath;
    m_syncData.beatSensitivity   = m_beatSensitivity;
    m_syncData.softCutDuration   = m_softCutDuration;
    m_syncData.presetDuration    = m_presetDuration;
    m_syncData.hardCutEnabled    = m_hardCutEnabled;
    m_syncData.hardCutSensitivity = m_hardCutSensitivity;
    m_syncData.meshX             = m_meshX;
    m_syncData.meshY             = m_meshY;
    m_syncData.aspectCorrection  = m_aspectCorrection;
    m_syncData.targetFps         = m_targetFps;
    m_syncData.shuffleEnabled    = m_shuffleEnabled;
    m_syncData.presetLocked      = m_presetLocked;
    m_syncData.settingsDirty     = true;
}
