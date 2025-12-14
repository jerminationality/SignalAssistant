#include "JackMonitorSink.h"

#include <QDebug>
#include <algorithm>
#include <cerrno>
#include <utility>

#include <jack/jack.h>
#include <jack/ringbuffer.h>

JackMonitorSink::JackMonitorSink(QString logTag)
    : m_logTag(std::move(logTag)) {}

JackMonitorSink::~JackMonitorSink() {
    stop();
}

bool JackMonitorSink::start(int sampleRate) {
    if (m_client)
        return true;

    jack_status_t status = static_cast<jack_status_t>(0);
    m_client = jack_client_open("guitarpi_hex_monitor", JackNoStartServer, &status);
    if (!m_client) {
        logJackStatus(status);
        qWarning() << m_logTag << "monitor" << "jack-open-failed" << static_cast<int>(status);
        return false;
    }

    jack_set_process_callback(m_client, &JackMonitorSink::processCallback, this);

    for (int i = 0; i < 2; ++i) {
        const std::string portName = (i == 0) ? "hex_monitor_L" : "hex_monitor_R";
        m_outputs[i] = jack_port_register(m_client,
                                          portName.c_str(),
                                          JACK_DEFAULT_AUDIO_TYPE,
                                          JackPortIsOutput,
                                          0);
        if (!m_outputs[i]) {
            qWarning() << m_logTag << "monitor" << "jack-port-failed" << QString::fromStdString(portName);
            stop();
            return false;
        }
    }

    const int sr = std::max(1, sampleRate);
    const std::size_t ringFrames = static_cast<std::size_t>(sr) * 2;
    const std::size_t ringBytes = ringFrames * 2 * sizeof(float);
    m_ring = jack_ringbuffer_create(ringBytes);
    if (!m_ring) {
        qWarning() << m_logTag << "monitor" << "jack-ringbuffer-failed";
        stop();
        return false;
    }
    jack_ringbuffer_mlock(m_ring);
    m_sampleRate = sampleRate;

    if (jack_activate(m_client) != 0) {
        qWarning() << m_logTag << "monitor" << "jack-activate-failed";
        stop();
        return false;
    }

    const int jackSr = static_cast<int>(jack_get_sample_rate(m_client));
    if (jackSr > 0 && sampleRate > 0 && jackSr != sampleRate && !m_warnedRateMismatch) {
        qWarning() << m_logTag << "monitor" << "jack-sample-rate-mismatch"
                   << "jack" << jackSr << "session" << sampleRate;
        m_warnedRateMismatch = true;
    }

    connectPlaybackPorts();
    qInfo() << m_logTag << "monitor" << "jack" << "active"
            << "sr" << jackSr << "buffer" << jack_get_buffer_size(m_client);
    return true;
}

void JackMonitorSink::stop() {
    if (m_client) {
        jack_client_t* client = m_client;
        m_client = nullptr;
        jack_deactivate(client);
        jack_client_close(client);
    }

    if (m_ring) {
        jack_ringbuffer_free(m_ring);
        m_ring = nullptr;
    }

    m_outputs[0] = nullptr;
    m_outputs[1] = nullptr;
    m_tempBuffer.clear();
    m_discardBuffer.clear();
    m_sampleRate = 0;
    m_warnedRateMismatch = false;
}

bool JackMonitorSink::push(const float* interleavedStereo, int frames) {
    if (!m_ring || !interleavedStereo || frames <= 0)
        return false;

    const std::size_t bytes = static_cast<std::size_t>(frames) * 2 * sizeof(float);
    if (jack_ringbuffer_write_space(m_ring) < bytes)
        return false;

    const std::size_t written = jack_ringbuffer_write(m_ring,
                                                      reinterpret_cast<const char*>(interleavedStereo),
                                                      bytes);
    return written == bytes;
}

int JackMonitorSink::processCallback(jack_nframes_t nframes, void* arg) {
    auto* self = static_cast<JackMonitorSink*>(arg);
    return self ? self->process(nframes) : 0;
}

int JackMonitorSink::process(jack_nframes_t nframes) {
    if (!m_ring)
        return 0;

    auto* left = static_cast<float*>(jack_port_get_buffer(m_outputs[0], nframes));
    auto* right = static_cast<float*>(jack_port_get_buffer(m_outputs[1], nframes));
    if (!left || !right)
        return 0;

    std::fill(left, left + nframes, 0.f);
    std::fill(right, right + nframes, 0.f);

    const std::size_t bytesNeeded = static_cast<std::size_t>(nframes) * 2 * sizeof(float);
    const std::size_t available = jack_ringbuffer_read_space(m_ring);
    if (available < bytesNeeded) {
        if (available > 0) {
            if (m_discardBuffer.size() < available)
                m_discardBuffer.resize(available);
            jack_ringbuffer_read(m_ring, m_discardBuffer.data(), available);
        }
        return 0;
    }

    if (m_tempBuffer.size() < static_cast<std::size_t>(nframes) * 2)
        m_tempBuffer.resize(static_cast<std::size_t>(nframes) * 2);

    jack_ringbuffer_read(m_ring,
                         reinterpret_cast<char*>(m_tempBuffer.data()),
                         bytesNeeded);

    for (jack_nframes_t i = 0; i < nframes; ++i) {
        const std::size_t index = static_cast<std::size_t>(i) * 2;
        left[i] = m_tempBuffer[index];
        right[i] = m_tempBuffer[index + 1];
    }
    return 0;
}

void JackMonitorSink::connectPlaybackPorts() {
    if (!m_client)
        return;

    constexpr unsigned long flags = JackPortIsPhysical | JackPortIsInput;
    const char** ports = jack_get_ports(m_client, nullptr, nullptr, flags);
    if (!ports)
        return;

    int assigned = 0;
    for (int i = 0; ports[i] && assigned < 2; ++i) {
        const char* dest = ports[i];
        if (!dest)
            continue;
        const QString name = QString::fromUtf8(dest);
        if (!name.contains(QStringLiteral("playback"), Qt::CaseInsensitive))
            continue;
        const char* src = jack_port_name(m_outputs[assigned]);
        if (!src)
            continue;
        const int rc = jack_connect(m_client, src, dest);
        if (rc == 0 || rc == EEXIST) {
            qInfo() << m_logTag << "monitor" << "jack-connect" << src << "->" << dest;
            ++assigned;
        } else {
            qWarning() << m_logTag << "monitor" << "jack-connect-failed" << src << dest << rc;
        }
    }

    jack_free(ports);
}

void JackMonitorSink::logJackStatus(jack_status_t status) const {
    if (status == 0)
        return;
    if (status & JackServerFailed)
        qWarning() << m_logTag << "monitor" << "jack-server-failed";
    if (status & JackNameNotUnique)
        qWarning() << m_logTag << "monitor" << "jack-name-not-unique";
    if (status & JackShmFailure)
        qWarning() << m_logTag << "monitor" << "jack-shm-failure";
    if (status & JackVersionError)
        qWarning() << m_logTag << "monitor" << "jack-version-error";
    if (status & JackLoadFailure)
        qWarning() << m_logTag << "monitor" << "jack-load-failure";
    if (status & JackInitFailure)
        qWarning() << m_logTag << "monitor" << "jack-init-failure";
    if (status & JackBackendError)
        qWarning() << m_logTag << "monitor" << "jack-backend-error";
    if (status & JackFailure)
        qWarning() << m_logTag << "monitor" << "jack-generic-failure";
}
