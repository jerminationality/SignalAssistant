#pragma once
#include "AudioEngine.h"
#include <QObject>
#include <atomic>
#include <memory>
#include <vector>
#include <QString>
#include <jack/types.h>
#include <carla/CarlaHost.h>

typedef struct _jack_client jack_client_t;
typedef struct _jack_port jack_port_t;

// Lightweight JACK pass-through that primes the Carla graph hooks.
// This provides realtime-safe audio I/O, exposes meter snapshots, and
// counts xruns so the UI can visualise Phase 1 telemetry. Carla plugin
// loading can build on top of this connection in a follow-up change.
class CarlaClient : public AudioEngine {
    Q_OBJECT
public:
    explicit CarlaClient(QObject* parent=nullptr);
    ~CarlaClient() override;

    bool start() override;
    void stop() override;
    void setBufferSize(int frames) override;
    void setSampleRate(int sr) override;

    int bufferSize() const { return m_currentBufferSize.load(); }
    int sampleRate() const { return m_currentSampleRate.load(); }

signals:
    void xrunsChanged(int count);
    void metersSnapshot(float inL, float inR, float outL, float outR);
    void bufferConfigChanged(int sampleRate, int bufferSize);

private:
    static int processCallback(jack_nframes_t nframes, void* arg);
    static int bufferSizeCallback(jack_nframes_t nframes, void* arg);
    static int sampleRateCallback(jack_nframes_t nframes, void* arg);
    static int xrunCallback(void* arg);
    static void shutdownCallback(void* arg);

    void emitMeters();
    void handleClientShutdown();
    void connectSystemPorts();
    void connectRackToSystem();
    void requestJackBufferSize(int frames);
    bool ensureCarlaHost();
    bool configureAndStartCarlaHost();
    bool loadDefaultPluginChain();
    bool addPluginToChain(const char* name, const char* bundlePath, const char* uri);
    void shutdownCarlaHost();
    void logCarlaError(const char* context) const;
    bool ensureJackServerRunning();
    void logJackStatus(jack_status_t status) const;
    struct JackServerConfig {
        QString deviceName {QStringLiteral("hw:0")};
        unsigned int sampleRate {48000};
        unsigned int framesPerPeriod {256};
        unsigned int periods {3};
        unsigned int channels {2};
    };
    JackServerConfig detectJackServerConfig() const;
    bool launchJackServer(const QString& command) const;

    jack_client_t* m_client {nullptr};
    jack_port_t* m_inputL {nullptr};
    jack_port_t* m_inputR {nullptr};
    jack_port_t* m_outputL {nullptr};
    jack_port_t* m_outputR {nullptr};
    std::atomic<int> m_currentBufferSize {0};
    std::atomic<int> m_currentSampleRate {0};
    std::atomic<int> m_pendingBufferSize {0};
    std::atomic<int> m_pendingSampleRate {0};
    std::atomic<int> m_xruns {0};

    std::atomic<float> m_inMeterL {0.0f};
    std::atomic<float> m_inMeterR {0.0f};
    std::atomic<float> m_outMeterL {0.0f};
    std::atomic<float> m_outMeterR {0.0f};
    class MeterPump;
    std::unique_ptr<MeterPump> m_meterPump;

    CarlaHostHandle m_carlaHost {nullptr};
    bool m_carlaEngineRunning {false};
    std::vector<uint32_t> m_pluginIds;

};
