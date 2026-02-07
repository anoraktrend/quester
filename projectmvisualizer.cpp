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
            const QStringList candidates = {
                "/usr/share/projectM/presets",
                "/usr/local/share/projectM/presets",
                QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + "/projectM/presets"
            };

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
            out << "Texture Size = " << settings.value("projectMTextureSize", 2048).toString() << "\n";
            out << "Mesh X = " << settings.value("projectMMeshX", 64).toString() << "\n";
            out << "Mesh Y = " << settings.value("projectMMeshY", 48).toString() << "\n";
            out << "FPS = " << settings.value("projectMFPS", 60).toString() << "\n";
            out << "Preset Path = " << presetPath << "\n";
            out << "Title Font = Sans\n";
            out << "Menu Font = Sans\n";
            out << "Smooth Preset Duration = " << settings.value("projectMSmoothPresetDuration", 5).toString() << "\n";
            out << "Preset Duration = " << settings.value("projectMPresetDuration", 15).toString() << "\n";
            out << "Beat Sensitivity = " << settings.value("projectMBeatSensitivity", 10.0).toString() << "\n";
            out << "Aspect Correction = 1\n";
            out << "Shuffle Enabled = " << (settings.value("projectMShuffleEnabled", true).toBool() ? "1" : "0") << "\n";
            out << "Soft Cut Ratings Enabled = 0\n";
            configFile.close();
        }

        std::string configPath = configFilePath.toStdString();

        // Attempt to initialize projectM. 
        // Note: Constructor signature might vary slightly between v3 and v4.
        try {
            m_projectM = new projectM(configPath);
        } catch (const std::exception &e) {
            qWarning() << "Failed to initialize projectM:" << e.what();
            m_projectM = nullptr;
        } catch (...) {
            qWarning() << "Failed to initialize projectM: Unknown exception";
            m_projectM = nullptr;
        }
    }

    ~ProjectMRenderer() override {
        if (m_projectM) delete m_projectM;
    }

    void render() override {
        if (!m_projectM || m_width <= 0 || m_height <= 0 || !m_running) return;
        m_projectM->renderFrame();
        update(); // Request continuous rendering
    }

    QOpenGLFramebufferObject *createFramebufferObject(const QSize &size) override {
        QOpenGLFramebufferObjectFormat format;
        format.setAttachment(QOpenGLFramebufferObject::CombinedDepthStencil);
        return new QOpenGLFramebufferObject(size, format);
    }

    void synchronize(QQuickFramebufferObject *item) override {
        auto *viz = static_cast<ProjectMVisualizer *>(item);
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

        // Handle Preset Selection
        bool hardCut = false;
        QString requestedPreset = viz->takePresetRequest(hardCut);
        if (!requestedPreset.isEmpty()) {
            try {
                unsigned int count = m_projectM->getPlaylistSize();
                for (unsigned int i = 0; i < count; ++i) {
                    QString name = QString::fromStdString(m_projectM->getPresetName(i));
                    if (QFileInfo(name).completeBaseName() == requestedPreset) {
                        m_projectM->selectPreset(i, hardCut);
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
            const int16_t *pcm = reinterpret_cast<const int16_t*>(data.constData());
            int totalSamples = data.size() / 2; // Total int16 values
            int stereoFrames = totalSamples / 2;

            // projectM expects chunks of 512 stereo frames, deinterleaved (planar)
            short pcm_data[2][512];
            int processed = 0;

            while (processed < stereoFrames) {
                int chunk = std::min(512, stereoFrames - processed);
                for (int i = 0; i < chunk; ++i) {
                    pcm_data[0][i] = pcm[(processed + i) * 2];     // Left
                    pcm_data[1][i] = pcm[(processed + i) * 2 + 1]; // Right
                }
                // Fill remainder with 0
                for (int i = chunk; i < 512; ++i) {
                    pcm_data[0][i] = 0;
                    pcm_data[1][i] = 0;
                }
                m_projectM->pcm()->addPCM16(pcm_data);
                processed += chunk;
            }
        }
    }

private:
    projectM *m_projectM = nullptr;
    int m_width = -1;
    int m_height = -1;
    bool m_running = false;
};

ProjectMVisualizer::ProjectMVisualizer(QQuickItem *parent)
    : QQuickFramebufferObject(parent)
    , m_active(false)
    , m_input(nullptr)
    , m_presetRequested(false)
    , m_hardCut(false)
{
    setMirrorVertically(true); // FBOs are often flipped
}

ProjectMVisualizer::~ProjectMVisualizer()
{
    stopInput();
}

QQuickFramebufferObject::Renderer *ProjectMVisualizer::createRenderer() const
{
    return new ProjectMRenderer();
}

bool ProjectMVisualizer::active() const
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

void ProjectMVisualizer::startInput()
{
    if (m_input) return;

    QSettings settings("Quester", "Quester");
    QString source = settings.value("audioSource", "pulseaudio").toString();

    if (source == "pipewire") m_input = new PipeWireInput(this);
    else if (source == "fifo") m_input = new FifoInput(settings.value("fifoPath", "/tmp/mpd.fifo").toString(), this);
    else m_input = new PulseAudioInput(this);

    connect(m_input, &AudioInput::dataReady, this, &ProjectMVisualizer::onDataReady, Qt::QueuedConnection);
    connect(m_input, &AudioInput::error, this, &ProjectMVisualizer::onError);
    m_input->start();
}

void ProjectMVisualizer::stopInput()
{
    if (m_input) {
        m_input->stop();
        delete m_input;
        m_input = nullptr;
    }
}

void ProjectMVisualizer::onDataReady(const QByteArray &data)
{
    QMutexLocker locker(&m_mutex);
    m_pcmBuffer.append(data);
    // Limit buffer size to avoid latency buildup
    if (m_pcmBuffer.size() > 8192) m_pcmBuffer = m_pcmBuffer.right(8192);
}

void ProjectMVisualizer::onError(const QString &msg)
{
    qWarning() << "ProjectM Audio Error:" << msg;
}

QByteArray ProjectMVisualizer::takePcmData()
{
    QMutexLocker locker(&m_mutex);
    QByteArray data = m_pcmBuffer;
    m_pcmBuffer.clear();
    return data;
}

QString ProjectMVisualizer::takePresetRequest(bool &hardCut)
{
    QMutexLocker locker(&m_mutex);
    if (!m_presetRequested) return QString();
    m_presetRequested = false;
    hardCut = m_hardCut;
    return m_requestedPreset;
}

QStringList ProjectMVisualizer::getPresetList(const QString &presetPath) const
{
    QStringList presets;
    
    QDir dir(presetPath);
    if (!dir.exists()) {
        // Try default paths if the provided path doesn't exist
        const QStringList candidates = {
            "/usr/share/projectM/presets",
            "/usr/local/share/projectM/presets",
            QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + "/projectM/presets"
        };
        
        for (const QString &p : candidates) {
            if (QDir(p).exists()) {
                dir.setPath(p);
                break;
            }
        }
    }
    
    if (dir.exists()) {
        // Get all .milk and .prjm files
        QFileInfoList files = dir.entryInfoList(QStringList() << "*.milk" << "*.prjm", QDir::Files);
        
        for (const QFileInfo &file : files) {
            QString name = file.completeBaseName();
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
