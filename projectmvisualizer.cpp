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

class ProjectMRenderer : public QQuickFramebufferObject::Renderer
{
public:
    ProjectMRenderer() {
        // Basic ProjectM initialization. 
        // Paths may vary by distribution; these are common defaults for Linux.
        QString configDir = QStandardPaths::writableLocation(QStandardPaths::ConfigLocation) + "/Quester/projectm";
        QDir dir(configDir);
        if (!dir.exists()) dir.mkpath(".");

        QString configFilePath = configDir + "/config.inp";
        QFile configFile(configFilePath);
        if (!configFile.exists()) {
            QString presetPath = "/usr/share/projectM/presets";
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

            if (configFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
                QTextStream out(&configFile);
                out << "Texture Size = 2048\n";
                out << "Mesh X = 64\n";
                out << "Mesh Y = 48\n";
                out << "FPS = 60\n";
                out << "Preset Path = " << presetPath << "\n";
                out << "Title Font = Sans\n";
                out << "Menu Font = Sans\n";
                out << "Smooth Preset Duration = 5\n";
                out << "Preset Duration = 15\n";
                out << "Beat Sensitivity = 10\n";
                out << "Aspect Correction = 1\n";
                out << "Shuffle Enabled = 1\n";
                out << "Soft Cut Ratings Enabled = 0\n";
                configFile.close();
            }
        }
        std::string configPath = configFilePath.toStdString();

        // Check for custom preset path in settings and update config.inp if needed
        QSettings settings("Quester", "Quester");
        QString customPresetPath = settings.value("projectMPresetPath").toString();

        if (!customPresetPath.isEmpty()) {
            if (configFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
                QStringList lines;
                QTextStream in(&configFile);
                bool changed = false;
                while (!in.atEnd()) {
                    QString line = in.readLine();
                    if (line.startsWith("Preset Path =")) {
                        QString currentPath = line.section('=', 1).trimmed();
                        if (currentPath != customPresetPath) {
                            line = "Preset Path = " + customPresetPath;
                            changed = true;
                        }
                    }
                    lines.append(line);
                }
                configFile.close();
                
                if (changed) {
                    if (configFile.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
                        QTextStream out(&configFile);
                        for (const QString &l : lines) {
                            out << l << "\n";
                        }
                        configFile.close();
                    }
                }
            }
        }

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
        if (!m_projectM) return;
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
        if (!m_projectM) return;

        // Handle resizing
        if (m_width != viz->width() || m_height != viz->height()) {
            m_width = viz->width();
            m_height = viz->height();
            // m_projectM->projectM_reset(); // Private in this version
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
};

ProjectMVisualizer::ProjectMVisualizer(QQuickItem *parent)
    : QQuickFramebufferObject(parent)
    , m_active(false)
    , m_input(nullptr)
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