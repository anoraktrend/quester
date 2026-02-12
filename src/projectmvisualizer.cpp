#include "projectmvisualizer.h"
#include <libprojectM/projectM.hpp>
#include <QOpenGLFramebufferObject>
#include <QSettings>
#include <QStandardPaths>
#include <QDir>
#include <QDebug>
#include <QFile>
#include <QTextStream>
#include <algorithm>
#include <QFileInfo>
#include <QCoreApplication>
#include <memory>
#include <QDirIterator>

constexpr int DEFAULT_PROJECTM_TEXTURE_SIZE = 2048;
constexpr int DEFAULT_PROJECTM_MESH_X = 64;
constexpr int DEFAULT_PROJECTM_MESH_Y = 48;
constexpr int DEFAULT_PROJECTM_FPS = 60;
constexpr int DEFAULT_PROJECTM_SMOOTH_PRESET_DURATION = 5;
constexpr int DEFAULT_PROJECTM_PRESET_DURATION = 15;
constexpr double DEFAULT_PROJECTM_BEAT_SENSITIVITY = 10.0;
constexpr double DEFAULT_PROJECTM_HARD_CUT_SENSITIVITY = 2.0;
constexpr int DEFAULT_PROJECTM_HARD_CUT_DURATION = 60;
constexpr int PCM_CHUNK_SIZE = 512;
constexpr int MAX_PCM_BUFFER_SIZE = 8192;

class ProjectMRenderer : public QQuickFramebufferObject::Renderer
{
public:
    ProjectMRenderer() {
        QSettings settings("Quester", "Quester");
        QString configDir = QStandardPaths::writableLocation(QStandardPaths::ConfigLocation) + "/Quester/projectm";
        QDir dir(configDir);
        if (!dir.exists()) dir.mkpath(".");
        QString configFilePath = configDir + "/config.inp";

        QString presetPath = settings.value("projectMPresetPath").toString();
        if (presetPath.isEmpty()) {
            QStringList candidates = {
#ifdef QT_QML_SOURCE_DIR
                QStringLiteral(QT_QML_SOURCE_DIR) + "/presets/presets-cream-of-the-crop",
#endif
                QCoreApplication::applicationDirPath() + "/projectM-presets",
                QStringLiteral(APP_DATADIR) + "/projectM-presets",
                "/usr/share/projectM/presets",
                "/usr/local/share/projectM/presets",
                QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + "/projectM/presets"
            };
            
            // macOS bundle resource path
#ifdef __APPLE__
            candidates.prepend(QCoreApplication::applicationDirPath() + QStringLiteral("/../Resources/projectM-presets"));
#endif

            for (const QString &p : candidates) {
                if (QDir(p).exists()) {
                    presetPath = p;
                    break;
                }
            }
        }
        
        QFile configFile(configFilePath);
        if (configFile.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
            QTextStream out(&configFile);
            out << "Texture Size = " << settings.value("projectMTextureSize", DEFAULT_PROJECTM_TEXTURE_SIZE).toString() << "\n";
            out << "Mesh X = " << settings.value("projectMMeshX", DEFAULT_PROJECTM_MESH_X).toString() << "\n";
            out << "Mesh Y = " << settings.value("projectMMeshY", DEFAULT_PROJECTM_MESH_Y).toString() << "\n";
            out << "FPS = " << settings.value("projectMFPS", DEFAULT_PROJECTM_FPS).toString() << "\n";
            out << "Preset Path = " << presetPath << "\n";
            out << "Title Font = Sans\n";
            out << "Menu Font = Sans\n";
            out << "Smooth Preset Duration = " << settings.value("projectMSmoothPresetDuration", DEFAULT_PROJECTM_SMOOTH_PRESET_DURATION).toString() << "\n";
            out << "Preset Duration = " << settings.value("projectMPresetDuration", DEFAULT_PROJECTM_PRESET_DURATION).toString() << "\n";
            out << "Beat Sensitivity = " << settings.value("projectMBeatSensitivity", DEFAULT_PROJECTM_BEAT_SENSITIVITY).toString() << "\n";
            out << "Aspect Correction = 1\n";
            out << "Shuffle Enabled = " << (settings.value("projectMShuffleEnabled", true).toBool() ? "1" : "0") << "\n";
            out << "Soft Cut Ratings Enabled = " << (settings.value("projectMSoftCutRatingsEnabled", false).toBool() ? "1" : "0") << "\n";
            out << "Hard Cut Enabled = " << (settings.value("projectMHardCutEnabled", false).toBool() ? "1" : "0") << "\n";
            out << "Hard Cut Sensitivity = " << settings.value("projectMHardCutSensitivity", DEFAULT_PROJECTM_HARD_CUT_SENSITIVITY).toString() << "\n";
            out << "Hard Cut Duration = " << settings.value("projectMHardCutDuration", DEFAULT_PROJECTM_HARD_CUT_DURATION).toString() << "\n";
            configFile.close();
        }

        std::string configPath = configFilePath.toStdString();

        // Attempt to initialize projectM. 
        // Note: Constructor signature might vary slightly between v3 and v4.
        try {
            m_projectM = std::make_unique<projectM>(configPath);
            qInfo() << "ProjectM initialized successfully";
        } catch (const std::exception &e) {
            qWarning() << "Failed to initialize projectM:" << e.what();
            m_projectM = nullptr;
        } catch (...) {
            qWarning() << "Failed to initialize projectM: Unknown exception";
            m_projectM = nullptr;
        }
    }

    ~ProjectMRenderer() override = default;

    ProjectMRenderer(const ProjectMRenderer &) = delete;
    auto operator=(const ProjectMRenderer &) -> ProjectMRenderer & = delete;
    ProjectMRenderer(ProjectMRenderer &&) = delete;
    auto operator=(ProjectMRenderer &&) -> ProjectMRenderer & = delete;

    void render() override {
        if (!m_projectM || m_width <= 0 || m_height <= 0 || !m_running) return;
        m_projectM->renderFrame();
        update(); // Request continuous rendering
    }

    auto createFramebufferObject(const QSize &size) -> QOpenGLFramebufferObject * override {
        QOpenGLFramebufferObjectFormat format;
        format.setAttachment(QOpenGLFramebufferObject::CombinedDepthStencil);
        return new QOpenGLFramebufferObject(size, format); // NOLINT(cppcoreguidelines-owning-memory)
    }

    void synchronize(QQuickFramebufferObject *item) override {
        auto *viz = dynamic_cast<ProjectMVisualizer *>(item);
        m_running = viz->active();
        if (m_running) update();

        if (!m_projectM) return;

        // Handle resizing
        int newWidth = static_cast<int>(viz->width());
        int newHeight = static_cast<int>(viz->height());
        if (newWidth > 0 && newHeight > 0 && (m_width != newWidth || m_height != newHeight)) {
            m_width = newWidth;
            m_height = newHeight;
            m_projectM->projectM_resetGL(m_width, m_height);
        }

        if (m_width <= 0 || m_height <= 0) return;

        // Handle Shuffle Update
        bool shuffle = false;
        if (viz->takeShuffleRequest(shuffle)) {
            m_projectM->setShuffleEnabled(shuffle);
        }

        // Handle Preset Selection
        bool hardCut = false;
        QString requestedPreset = viz->takePresetRequest(hardCut);
        if (!requestedPreset.isEmpty()) {
            qInfo() << "Requested preset:" << requestedPreset;
            try {
                unsigned int count = m_projectM->getPlaylistSize();
                qInfo() << "Playlist size:" << count;
                for (unsigned int i = 0; i < count; ++i) {
                    QString name = QString::fromStdString(m_projectM->getPresetName(i));
                    QString baseName = QFileInfo(name).completeBaseName();
                    qInfo() << "Preset" << i << "name:" << name << "baseName:" << baseName;
                    if (baseName == requestedPreset) {
                        m_projectM->selectPreset(i, hardCut);
                        qInfo() << "ProjectM preset selected:" << requestedPreset << "at index" << i;
                        break;
                    }
                }
            } catch (...) {
                qWarning() << "Failed to select projectM preset:" << requestedPreset;
            }
        }

        // Feed Audio Data
        QByteArray data = viz->takePcmData();
        if (!data.isEmpty()) {
            const auto *pcm = reinterpret_cast<const int16_t*>(data.constData()); // NOLINT(cppcoreguidelines-pro-type-reinterpret-cast)
            int totalSamples = static_cast<int>(data.size() / 2); // Total int16 values
            int stereoFrames = totalSamples / 2;

            // projectM expects chunks of 512 stereo frames, deinterleaved (planar)
            short pcm_data[2][PCM_CHUNK_SIZE] = {}; // NOLINT(cppcoreguidelines-avoid-c-arrays)
            int processed = 0;

            while (processed < stereoFrames) {
                int chunk = std::min(PCM_CHUNK_SIZE, stereoFrames - processed);
                for (int i = 0; i < chunk; ++i) {
                    pcm_data[0][i] = pcm[static_cast<ptrdiff_t>((processed + i) * 2)];     // Left // NOLINT(cppcoreguidelines-pro-bounds-pointer-arithmetic,cppcoreguidelines-pro-bounds-constant-array-index)
                    pcm_data[1][i] = pcm[static_cast<ptrdiff_t>((processed + i) * 2 + 1)]; // Right // NOLINT(cppcoreguidelines-pro-bounds-pointer-arithmetic,cppcoreguidelines-pro-bounds-constant-array-index)
                }
                // Fill remainder with 0
                for (int i = chunk; i < PCM_CHUNK_SIZE; ++i) {
                    pcm_data[0][i] = 0; // NOLINT(cppcoreguidelines-pro-bounds-constant-array-index)
                    pcm_data[1][i] = 0; // NOLINT(cppcoreguidelines-pro-bounds-constant-array-index)
                }
                m_projectM->pcm()->addPCM16(pcm_data); // NOLINT(cppcoreguidelines-pro-bounds-array-to-pointer-decay)
                processed += chunk;
            }
        }
    }

private:
    std::unique_ptr<projectM> m_projectM;
    int m_width = -1;
    int m_height = -1;
    bool m_running = false;
};

ProjectMVisualizer::ProjectMVisualizer(QQuickItem *parent)
    : QQuickFramebufferObject(parent)
     
{
    QSettings settings("Quester", "Quester");
#ifdef __APPLE__
    m_audioSource = settings.value("audioSource", "coreaudio").toString();
#else
    m_audioSource = settings.value("audioSource", "pipewire").toString();
#endif
    setMirrorVertically(true); // FBOs are often flipped
}

ProjectMVisualizer::~ProjectMVisualizer()
{
    stopInput();
}

auto ProjectMVisualizer::createRenderer() const -> QQuickFramebufferObject::Renderer *
{
    return new ProjectMRenderer(); // NOLINT(cppcoreguidelines-owning-memory)
}

auto ProjectMVisualizer::active() const -> bool
{
    return m_active;
}

void ProjectMVisualizer::setActive(bool active)
{
    if (m_active == active) return;
    m_active = active;
    
    if (m_active) startInput();
    else stopInput();
    
    emit activeChanged();
    update();
}

auto ProjectMVisualizer::shuffleEnabled() const -> bool
{
    return m_shuffleEnabled;
}

void ProjectMVisualizer::setShuffleEnabled(bool enabled)
{
    if (m_shuffleEnabled == enabled) return;
    m_shuffleEnabled = enabled;
    {
        QMutexLocker locker(&m_mutex);
        m_shuffleUpdateRequested = true;
    }
    emit shuffleEnabledChanged();
    update();
}

auto ProjectMVisualizer::audioSource() const -> QString
{
    return m_audioSource;
}

void ProjectMVisualizer::setAudioSource(const QString &source)
{
    if (m_audioSource == source) return;
    m_audioSource = source;
    if (m_active) {
        stopInput();
        startInput();
    }
    emit audioSourceChanged();
}

void ProjectMVisualizer::startInput()
{
    if (m_input) return;

    QSettings settings("Quester", "Quester");
    if (m_audioSource == "pipewire") {
#ifndef __APPLE__
        m_input = std::make_unique<PipeWireInput>(this);
#endif
    } else if (m_audioSource == "fifo") {
        QString path = settings.value("fifoPath", "/tmp/mpd.fifo").toString();
        m_input = std::make_unique<FifoInput>(path, this);
    } else if (m_audioSource == "coreaudio") {
#ifdef __APPLE__
        m_input = std::make_unique<CoreAudioInput>(this);
#endif
    } else {
#ifndef __APPLE__
        m_input = std::make_unique<PulseAudioInput>(this);
#endif
    }
    connect(m_input.get(), &AudioInput::dataReady, this, &ProjectMVisualizer::onDataReady, Qt::QueuedConnection);
    connect(m_input.get(), &AudioInput::error, this, &ProjectMVisualizer::onError);
    m_input->start();
}

void ProjectMVisualizer::stopInput()
{
    if (m_input) {
        m_input->stop();
        m_input.reset();
    }
}

void ProjectMVisualizer::onDataReady(const QByteArray &data)
{
    QMutexLocker locker(&m_mutex);
    m_pcmBuffer.append(data);
    // Limit buffer size to avoid latency buildup
    if (m_pcmBuffer.size() > MAX_PCM_BUFFER_SIZE) m_pcmBuffer = m_pcmBuffer.right(MAX_PCM_BUFFER_SIZE);
}

void ProjectMVisualizer::onError(const QString &msg)
{
    qWarning() << "ProjectM Audio Error:" << msg;
}

auto ProjectMVisualizer::takePcmData() -> QByteArray
{
    QMutexLocker locker(&m_mutex);
    QByteArray data = m_pcmBuffer;
    m_pcmBuffer.clear();
    return data;
}

auto ProjectMVisualizer::takePresetRequest(bool &hardCut) -> QString
{
    QMutexLocker locker(&m_mutex);
    if (!m_presetRequested) return {};
    m_presetRequested = false;
    hardCut = m_hardCut;
    return m_requestedPreset;
}

auto ProjectMVisualizer::takeShuffleRequest(bool &enabled) -> bool
{
    QMutexLocker locker(&m_mutex);
    if (!m_shuffleUpdateRequested) return false;
    m_shuffleUpdateRequested = false;
    enabled = m_shuffleEnabled;
    return true;
}

auto ProjectMVisualizer::getPresetList(const QString &presetPath) const -> QStringList
{
    QStringList presets;
    
    QDir dir(presetPath);
    if (!dir.exists()) {
        // Try default paths if the provided path doesn't exist
        QStringList candidates = {
#ifdef QT_QML_SOURCE_DIR
            QStringLiteral(QT_QML_SOURCE_DIR) + "/presets/presets-cream-of-the-crop",
#endif
            QCoreApplication::applicationDirPath() + "/projectM-presets",
            QStringLiteral(APP_DATADIR) + "/projectM-presets",
            "/usr/share/projectM/presets",
            "/usr/local/share/projectM/presets",
            QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + "/projectM/presets"
        };
        
        // macOS bundle resource path
#ifdef __APPLE__
        candidates.prepend(QCoreApplication::applicationDirPath() + QStringLiteral("/../Resources/projectM-presets"));
#endif
        
        for (const QString &p : candidates) {
            if (QDir(p).exists()) {
                dir.setPath(p);
                break;
            }
        }
    }
    
    if (dir.exists()) {
        // Use QDirIterator to scan recursively
        QDirIterator it(dir.path(), QStringList() << "*.milk" << "*.prjm", QDir::Files, QDirIterator::Subdirectories);
        while (it.hasNext()) {
            it.next();
            QString name = it.fileInfo().completeBaseName();
            if (!name.isEmpty()) {
                presets << name;
            }
        }
        
        // Sort alphabetically
        presets.sort();
        presets.removeDuplicates();
    }
    
    return presets;
}

void ProjectMVisualizer::selectPresetByName(const QString &presetName, bool hardCut)
{
    QMutexLocker locker(&m_mutex);
    m_requestedPreset = presetName;
    m_hardCut = hardCut;
    m_presetRequested = true;
    update(); // Request synchronization
}
