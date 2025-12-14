#pragma once

#include "AudioEngine.h"
#include "HexAudioClient.h"

#include <QObject>
#include <QElapsedTimer>
#include <QMetaType>
#include <jack/types.h>
#include <array>
#include <atomic>
#include <memory>
#include <mutex>
#include <vector>

class JackMonitorSink;

class TabEngineBridge;

typedef struct _jack_client jack_client_t;
typedef struct _jack_port jack_port_t;

class QTimer;

using HexMeterArray = std::array<float, 6>;

class HexJackClient : public AudioEngine, public HexAudioClient {
    Q_OBJECT
public:
    explicit HexJackClient(QObject* parent=nullptr);
    ~HexJackClient() override;

    bool start() override;
    void stop() override;
    void setBufferSize(int frames) override;
    void setSampleRate(int sr) override;

    void setTabBridge(TabEngineBridge* bridge) override;
    void connectMeters(TabEngineBridge* bridge) override;
    void connectCalibration(TabEngineBridge* bridge) override;
    void requestCalibration(int stringIndex = -1) override;
    void setLiveMonitorEnabled(bool enabled);
    bool liveMonitorEnabled() const noexcept { return m_monitorRequested.load(std::memory_order_acquire); }

    int bufferSize() const { return m_currentBufferSize.load(std::memory_order_acquire); }
    int sampleRate() const { return m_currentSampleRate.load(std::memory_order_acquire); }

signals:
    void bufferConfigChanged(int sampleRate, int bufferSize);
    void xrunsChanged(int count);
    void hexMetersSnapshot(const HexMeterArray& meters);
    void calibrationStarted();
    void calibrationStepChanged(int stringIndex, bool capturing);
    void calibrationFinished(const std::array<float, 6>& averages,
                             const std::array<float, 6>& peaks);

private:
    static int processCallback(jack_nframes_t nframes, void* arg);
    static int bufferSizeCallback(jack_nframes_t nframes, void* arg);
    static int sampleRateCallback(jack_nframes_t nframes, void* arg);
    static int xrunCallback(void* arg);
    static void shutdownCallback(void* arg);

    void emitMeters();
    void handleClientShutdown();
    bool ensureJackServerRunning();
    void logJackStatus(jack_status_t status) const;
    bool launchJackServer(const QString& command) const;
    void connectSystemPorts();
    void handleCalibrationRequest(int targetString);
    void advanceCalibration(float levels[6], jack_nframes_t nframes);
    void announceCalibrationStep(int stringIndex, bool capturing);
    void pushMonitorBlock(const float* const channels[6], int frames);
    bool ensureMonitorSink();
    void destroyMonitorSink();

    jack_client_t* m_client {nullptr};
    std::array<jack_port_t*, 6> m_inputs {};
    std::atomic<int> m_currentBufferSize {0};
    std::atomic<int> m_currentSampleRate {0};
    std::atomic<int> m_pendingBufferSize {0};
    std::atomic<int> m_pendingSampleRate {0};
    std::atomic<int> m_xruns {0};

    std::array<std::atomic<float>, 6> m_detectionMeters {};
    QElapsedTimer m_meterLogTimer;
    bool m_meterLoggingEnabled {false};

    TabEngineBridge* m_bridge {nullptr};

    class MeterPump;
    std::unique_ptr<MeterPump> m_meterPump;

    std::atomic<bool> m_monitorRequested {false};
    std::atomic<std::shared_ptr<JackMonitorSink>> m_monitorSink;
    std::mutex m_monitorMutex;
    std::vector<float> m_monitorMixBuffer;
    float m_monitorGain {0.35f};
    
    // Calibrated audio buffers (per-string)
    std::array<std::vector<float>, 6> m_calibratedBuffers;

    struct CalibrationState {
        bool active {false};
        bool capturing {false};
        bool partial {false};
        int currentString {0};
        int sequenceIndex {0};
        int sequenceCount {0};
        int framesRemaining {0};
        int captureFramesPerString {0};
        std::array<int, 6> sequence {};
        std::array<bool, 6> updated {};
        std::array<double, 6> sumRms {};
        std::array<int, 6> samples {};
        std::array<float, 6> peakRms {};
    };

    std::atomic<int> m_pendingCalibrationTarget {-2};
    CalibrationState m_calibrationState;
};
