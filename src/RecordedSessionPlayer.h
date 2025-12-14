#pragma once

#include <QObject>
#include <QAudioFormat>
#include <QString>
#include <array>
#include <atomic>
#include <memory>
#include <thread>
#include <vector>

#include <QMutex>
#include <QWaitCondition>

#include <sndfile.h>

class JackMonitorSink;

class TabEngineBridge;
struct RunSessionOptions;
class QAudioSink;

class RecordedSessionPlayer : public QObject {
    Q_OBJECT
public:
    explicit RecordedSessionPlayer(TabEngineBridge* bridge, QObject* parent = nullptr);
    ~RecordedSessionPlayer() override;

    bool loadSession(const RunSessionOptions& options);
    bool isReady() const noexcept { return m_ready; }
    bool play();
    void pause();
    void stop();
    void setHexMonitorEnabled(bool enabled);
    bool hexMonitorEnabled() const noexcept { return m_monitorEnabled.load(std::memory_order_acquire); }

    double durationSec() const noexcept;
    double positionSec() const noexcept;
    bool seekToSeconds(double seconds);
    bool seekToProgress(double normalized);

signals:
    void playbackProgress(double positionSec, double durationSec);
    void playbackFinished();
    void playbackError(const QString& description);

private:
    class MonitorBuffer;

    enum class MonitorBackend {
        None,
        Jack,
        QtAudio
    };

    struct Track {
        QString filePath;
        SNDFILE* handle {nullptr};
        SF_INFO info {};
        std::vector<float> buffer;
        sf_count_t totalFrames {0};
        bool atEnd {false};
    };

    void closeTracks();
    bool openTrack(int stringIndex, const QString& filePath);
    QString resolveFileForString(int stringIndex,
                                 const QString& preferredName,
                                 const RunSessionOptions& options,
                                 const QString& sessionDir) const;
    void playbackLoop();
    void rewindAll();
    void destroyMonitorSink();
    bool ensureMonitorSink();
    void destroyMonitorSinkLocked();
    bool initJackMonitorLocked();
    bool initQtMonitorLocked();
    bool jackMonitorAllowed() const noexcept;
    void pushMonitorBlock(const float* const channels[6], int frames);
    void waitWhilePaused();

    TabEngineBridge* m_bridge {nullptr};
    std::array<Track, 6> m_tracks;
    std::thread m_thread;
    std::atomic<bool> m_abort {false};
    std::atomic<bool> m_paused {false};
    std::atomic<bool> m_running {false};
    mutable QMutex m_pauseMutex;
    mutable QMutex m_trackMutex;
    mutable QMutex m_monitorMutex;
    QWaitCondition m_pauseCond;
    int m_sampleRate {0};
    sf_count_t m_totalFrames {0};
    std::atomic<sf_count_t> m_positionFrames {0};
    bool m_ready {false};
    bool m_debugLogging {false};
    std::unique_ptr<QAudioSink> m_monitorSink;
    std::unique_ptr<MonitorBuffer> m_monitorBuffer;
    std::unique_ptr<JackMonitorSink> m_jackMonitor;
    QAudioFormat m_monitorFormat;
    std::vector<float> m_monitorMixBuffer;
    std::vector<char> m_monitorByteBuffer;
    MonitorBackend m_monitorBackend {MonitorBackend::None};
    std::atomic<bool> m_monitorEnabled {false};
    float m_monitorGain {0.35f};
    const bool m_disableJackMonitor;
};
