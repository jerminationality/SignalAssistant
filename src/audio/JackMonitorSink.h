#pragma once

#include <QString>
#include <jack/types.h>
#include <jack/ringbuffer.h>
#include <vector>

struct _jack_client;
struct _jack_port;
struct _jack_ringbuffer;

class JackMonitorSink {
public:
    explicit JackMonitorSink(QString logTag = QStringLiteral("Monitor"));
    ~JackMonitorSink();

    bool start(int sampleRate);
    void stop();
    bool push(const float* interleavedStereo, int frames);
    bool isActive() const noexcept { return m_client != nullptr; }

private:
    static int processCallback(jack_nframes_t nframes, void* arg);
    int process(jack_nframes_t nframes);
    void connectPlaybackPorts();
    void logJackStatus(jack_status_t status) const;

    QString m_logTag;
    _jack_client* m_client {nullptr};
    _jack_port* m_outputs[2] {nullptr, nullptr};
    jack_ringbuffer_t* m_ring {nullptr};
    std::vector<float> m_tempBuffer;
    std::vector<char> m_discardBuffer;
    int m_sampleRate {0};
    bool m_warnedRateMismatch {false};
};
