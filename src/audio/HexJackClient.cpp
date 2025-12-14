#include "HexJackClient.h"
#include "JackMonitorSink.h"
#include "../TabEngineBridge.h"
#include "../SessionLogger.h"

#include <QMetaObject>
#include <QProcess>
#include <QElapsedTimer>
#include <QThread>
#include <QTimer>
#include <QDebug>
#include <QStringList>

#include <jack/jack.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <string>

namespace {
constexpr int kTabCaptureBaseChannel = 3;
constexpr const char* kHexClientName = "guitarpi_hex";
constexpr const char* kDefaultJackCommand = "JACK_NO_AUDIO_RESERVATION=1 jackd -R -P70 -d alsa -d hw:2,0 -p128 -n3 -r48000 -s~";
constexpr float kCalibrationCaptureSecPerString = 1.25f;
constexpr float kCalibrationTriggerLevel = 0.008f;

float computeLevel(const jack_default_audio_sample_t* buffer, jack_nframes_t frames) {
    if (!buffer || frames == 0)
        return 0.0f;
    double sum = 0.0;
    for (jack_nframes_t i = 0; i < frames; ++i) {
        const double sample = buffer[i];
        sum += sample * sample;
    }
    const double rms = std::sqrt(sum / static_cast<double>(frames));
    return static_cast<float>(std::clamp(rms, 0.0, 1.0));
}

} // namespace

class HexJackClient::MeterPump {
public:
    explicit MeterPump(HexJackClient* owner) : m_owner(owner) {
        m_timer = std::make_unique<QTimer>();
        m_timer->setTimerType(Qt::CoarseTimer);
        m_timer->setInterval(40);
        QObject::connect(m_timer.get(), &QTimer::timeout, owner, [owner]() { owner->emitMeters(); });
        m_timer->start();
    }

private:
    HexJackClient* m_owner;
    std::unique_ptr<QTimer> m_timer;
};

HexJackClient::HexJackClient(QObject* parent)
    : AudioEngine(parent) {
    qRegisterMetaType<std::array<float, 6>>("FloatMeterArray");
    for (auto& meter : m_detectionMeters)
        meter.store(0.0f);
    m_meterLoggingEnabled = qEnvironmentVariableIntValue("GUITARPI_HEX_METER_LOGS") > 0;
}

HexJackClient::~HexJackClient() {
    stop();
}

bool HexJackClient::start() {
    if (m_client)
        return true;

    if (!ensureJackServerRunning()) {
        qWarning("HexJackClient: JACK server unavailable; cannot start hex capture");
        return false;
    }

    jack_status_t status = static_cast<jack_status_t>(0);
    m_client = jack_client_open(kHexClientName, JackNoStartServer, &status);
    if (!m_client) {
        logJackStatus(status);
        qWarning("HexJackClient: failed to open JACK client (status=%d)", static_cast<int>(status));
        return false;
    }

    jack_set_process_callback(m_client, &HexJackClient::processCallback, this);
    jack_set_buffer_size_callback(m_client, &HexJackClient::bufferSizeCallback, this);
    jack_set_sample_rate_callback(m_client, &HexJackClient::sampleRateCallback, this);
    jack_set_xrun_callback(m_client, &HexJackClient::xrunCallback, this);
    jack_on_shutdown(m_client, &HexJackClient::shutdownCallback, this);

    for (int s = 0; s < 6; ++s) {
        const std::string portName = "hex_in_" + std::to_string(s + 1);
        m_inputs[static_cast<std::size_t>(s)] = jack_port_register(m_client,
                                                                  portName.c_str(),
                                                                  JACK_DEFAULT_AUDIO_TYPE,
                                                                  JackPortIsInput,
                                                                  0);
        if (!m_inputs[static_cast<std::size_t>(s)]) {
            qWarning("HexJackClient: failed to register port %s", portName.c_str());
            stop();
            return false;
        }
    }

    m_currentBufferSize.store(static_cast<int>(jack_get_buffer_size(m_client)));
    m_currentSampleRate.store(static_cast<int>(jack_get_sample_rate(m_client)));

    const int pendingFrames = m_pendingBufferSize.load();
    if (pendingFrames > 0 && pendingFrames != m_currentBufferSize.load()) {
        jack_set_buffer_size(m_client, static_cast<jack_nframes_t>(pendingFrames));
    }

    if (jack_activate(m_client) != 0) {
        qWarning("HexJackClient: failed to activate JACK client");
        stop();
        return false;
    }

    connectSystemPorts();

    m_meterPump = std::make_unique<MeterPump>(this);

    if (m_monitorRequested.load(std::memory_order_acquire))
        ensureMonitorSink();

    QMetaObject::invokeMethod(this, [this]() {
        emit bufferConfigChanged(sampleRate(), bufferSize());
        emit xrunsChanged(m_xruns.load());
    }, Qt::QueuedConnection);

    return true;
}

void HexJackClient::stop() {
    if (m_meterPump)
        m_meterPump.reset();

    if (m_client) {
        jack_client_t* client = m_client;
        m_client = nullptr;
        jack_client_close(client);
    }

    m_inputs.fill(nullptr);
    m_currentBufferSize.store(0);
    m_currentSampleRate.store(0);
    destroyMonitorSink();
}

void HexJackClient::setBufferSize(int frames) {
    m_pendingBufferSize.store(frames);
    if (m_client && frames > 0) {
        jack_set_buffer_size(m_client, static_cast<jack_nframes_t>(frames));
    }
}

void HexJackClient::setSampleRate(int sr) {
    m_pendingSampleRate.store(sr);
    if (m_client && sr > 0 && sr != sampleRate()) {
        qWarning("HexJackClient: JACK running at %d Hz; restart server for %d Hz", sampleRate(), sr);
    }
}

void HexJackClient::setTabBridge(TabEngineBridge* bridge) {
    m_bridge = bridge;
}

void HexJackClient::connectMeters(TabEngineBridge* bridge) {
    if (!bridge)
        return;
    QObject::connect(this, &HexJackClient::hexMetersSnapshot, bridge, &TabEngineBridge::updateLiveMeters, Qt::QueuedConnection);
}

void HexJackClient::connectCalibration(TabEngineBridge* bridge) {
    if (!bridge)
        return;
    QObject::connect(this, &HexJackClient::calibrationStarted, bridge, &TabEngineBridge::handleCalibrationStarted, Qt::QueuedConnection);
    QObject::connect(this, &HexJackClient::calibrationStepChanged, bridge, &TabEngineBridge::handleCalibrationStepChanged, Qt::QueuedConnection);
    QObject::connect(this, &HexJackClient::calibrationFinished, bridge, &TabEngineBridge::handleCalibrationFinished, Qt::QueuedConnection);
}

void HexJackClient::requestCalibration(int stringIndex) {
    const int target = (stringIndex >= 0 && stringIndex < 6) ? stringIndex : -1;
    m_pendingCalibrationTarget.store(target, std::memory_order_release);
}

void HexJackClient::setLiveMonitorEnabled(bool enabled) {
    m_monitorRequested.store(enabled, std::memory_order_release);
    if (enabled) {
        ensureMonitorSink();
    } else {
        destroyMonitorSink();
    }
}

bool HexJackClient::ensureMonitorSink() {
    if (!m_monitorRequested.load(std::memory_order_acquire))
        return false;

    const int sr = m_currentSampleRate.load(std::memory_order_acquire);
    if (sr <= 0)
        return false;

    auto current = std::atomic_load(&m_monitorSink);
    if (current && current->isActive())
        return true;

    std::lock_guard<std::mutex> lock(m_monitorMutex);
    current = std::atomic_load(&m_monitorSink);
    if (current && current->isActive())
        return true;

    auto sink = std::make_shared<JackMonitorSink>(QStringLiteral("LiveHexMonitor"));
    if (!sink->start(sr))
        return false;

    std::atomic_store(&m_monitorSink, sink);
    return true;
}

void HexJackClient::destroyMonitorSink() {
    std::shared_ptr<JackMonitorSink> old;
    {
        std::lock_guard<std::mutex> lock(m_monitorMutex);
        old = std::atomic_exchange(&m_monitorSink, std::shared_ptr<JackMonitorSink>{});
    }
    if (old)
        old->stop();
}

void HexJackClient::pushMonitorBlock(const float* const channels[6], int frames) {
    if (!channels || frames <= 0)
        return;

    auto sink = std::atomic_load(&m_monitorSink);
    if (!sink || !sink->isActive())
        return;

    const std::size_t sampleCount = static_cast<std::size_t>(frames) * 2;
    if (m_monitorMixBuffer.size() < sampleCount)
        m_monitorMixBuffer.resize(sampleCount);

    for (int frame = 0; frame < frames; ++frame) {
        float sum = 0.f;
        for (int stringIndex = 0; stringIndex < 6; ++stringIndex) {
            const float sample = channels[stringIndex] ? channels[stringIndex][frame] : 0.f;
            sum += sample;
        }
        const float mono = (sum / 6.f) * m_monitorGain;
        const std::size_t base = static_cast<std::size_t>(frame) * 2;
        m_monitorMixBuffer[base] = mono;
        m_monitorMixBuffer[base + 1] = mono;
    }

    sink->push(m_monitorMixBuffer.data(), frames);
}

int HexJackClient::processCallback(jack_nframes_t nframes, void* arg) {
    auto* self = static_cast<HexJackClient*>(arg);
    std::array<const float*, 6> channels {};

    // Get raw JACK input buffers
    for (int s = 0; s < 6; ++s) {
        jack_port_t* port = self->m_inputs[static_cast<std::size_t>(s)];
        const auto* buffer = port ? static_cast<const jack_default_audio_sample_t*>(jack_port_get_buffer(port, nframes)) : nullptr;
        channels[static_cast<std::size_t>(s)] = buffer ? reinterpret_cast<const float*>(buffer) : nullptr;
    }

    // Apply calibration multipliers to create calibrated buffers
    std::array<float, 6> multipliers {};
    if (self->m_bridge) {
        self->m_bridge->getCalibrationMultipliers(multipliers);
    } else {
        multipliers.fill(1.0f);
    }

    for (int s = 0; s < 6; ++s) {
        const float* src = channels[static_cast<std::size_t>(s)];
        if (!src) continue;

        auto& buf = self->m_calibratedBuffers[static_cast<std::size_t>(s)];
        buf.resize(nframes);
        
        const float mult = multipliers[static_cast<std::size_t>(s)];
        for (jack_nframes_t i = 0; i < nframes; ++i) {
            buf[i] = src[i] * mult;
        }
        
        channels[static_cast<std::size_t>(s)] = buf.data();
    }

    // Calculate meters from CALIBRATED audio (after multiplier applied)
    for (int s = 0; s < 6; ++s) {
        const float* calibratedBuffer = channels[static_cast<std::size_t>(s)];
        float level = calibratedBuffer ? computeLevel(calibratedBuffer, nframes) : 0.0f;
        const float prev = self->m_detectionMeters[static_cast<std::size_t>(s)].load(std::memory_order_relaxed);
        const float mix = (s == 0) ? 0.35f : (s == 1 ? 0.45f : 1.0f);
        if (mix < 1.0f) {
            level = prev * (1.0f - mix) + level * mix;
        }
        self->m_detectionMeters[static_cast<std::size_t>(s)].store(level, std::memory_order_relaxed);
    }

    const int pendingTarget = self->m_pendingCalibrationTarget.exchange(-2, std::memory_order_acq_rel);
    if (pendingTarget != -2)
        self->handleCalibrationRequest(pendingTarget);

    float levelSnapshot[6] {};
    for (int s = 0; s < 6; ++s)
        levelSnapshot[s] = self->m_detectionMeters[static_cast<std::size_t>(s)].load(std::memory_order_relaxed);

    if (self->m_calibrationState.active)
        self->advanceCalibration(levelSnapshot, nframes);

    const float sr = static_cast<float>(self->m_currentSampleRate.load(std::memory_order_acquire));
    
    // Now both processing and monitor see calibrated audio
    if (self->m_bridge) {
        self->m_bridge->processLiveAudioBlock(channels.data(), static_cast<int>(nframes), sr);
    }

    if (self->m_monitorRequested.load(std::memory_order_acquire)) {
        self->pushMonitorBlock(channels.data(), static_cast<int>(nframes));
    }

    return 0;
}

int HexJackClient::bufferSizeCallback(jack_nframes_t nframes, void* arg) {
    auto* self = static_cast<HexJackClient*>(arg);
    self->m_currentBufferSize.store(static_cast<int>(nframes));
    QMetaObject::invokeMethod(self, [self]() { emit self->bufferConfigChanged(self->sampleRate(), self->bufferSize()); }, Qt::QueuedConnection);
    return 0;
}

int HexJackClient::sampleRateCallback(jack_nframes_t nframes, void* arg) {
    auto* self = static_cast<HexJackClient*>(arg);
    self->m_currentSampleRate.store(static_cast<int>(nframes));
    QMetaObject::invokeMethod(self, [self]() { emit self->bufferConfigChanged(self->sampleRate(), self->bufferSize()); }, Qt::QueuedConnection);
    return 0;
}

int HexJackClient::xrunCallback(void* arg) {
    auto* self = static_cast<HexJackClient*>(arg);
    const int count = self->m_xruns.fetch_add(1) + 1;
    QMetaObject::invokeMethod(self, [self, count]() { emit self->xrunsChanged(count); }, Qt::QueuedConnection);
    return 0;
}

void HexJackClient::shutdownCallback(void* arg) {
    auto* self = static_cast<HexJackClient*>(arg);
    QMetaObject::invokeMethod(self, [self]() { self->handleClientShutdown(); }, Qt::QueuedConnection);
}

void HexJackClient::emitMeters() {
    std::array<float, 6> snapshot {};
    for (int s = 0; s < 6; ++s)
        snapshot[static_cast<std::size_t>(s)] = m_detectionMeters[static_cast<std::size_t>(s)].load();
    emit hexMetersSnapshot(snapshot);

    if (!m_meterLoggingEnabled)
        return;

    if (!m_meterLogTimer.isValid())
        m_meterLogTimer.start();

    if (m_meterLogTimer.elapsed() >= 50) {
        m_meterLogTimer.restart();
        static const std::array<const char*, 6> kStringNames {"E", "A", "D", "G", "B", "e"};
        QStringList parts;
        parts.reserve(6);
        for (int s = 0; s < 6; ++s) {
            const char* name = kStringNames[static_cast<std::size_t>(s)];
            parts.push_back(QString::asprintf("%s | %.3f", name, snapshot[static_cast<std::size_t>(s)]));
        }
        const QString logLine = QStringLiteral("Hex input RMS -> %1").arg(parts.join(QStringLiteral("    ")));
        qInfo().noquote() << logLine;
        SessionLogger::instance().log("meters", logLine.toStdString());
    }
}

void HexJackClient::handleClientShutdown() {
    stop();
}

bool HexJackClient::ensureJackServerRunning() {
    jack_status_t status = static_cast<jack_status_t>(0);
    if (jack_client_t* probe = jack_client_open("guitarpi_hex_probe", JackNoStartServer, &status)) {
        jack_client_close(probe);
        return true;
    }

    if (!(status & JackServerFailed)) {
        logJackStatus(status);
        return false;
    }

    const QByteArray custom = qgetenv("GUITARPI_JACK_COMMAND");
    const QString command = custom.isEmpty() ? QString::fromUtf8(kDefaultJackCommand) : QString::fromUtf8(custom);
    qInfo("HexJackClient: launching JACK via command: %s", command.toUtf8().constData());
    if (!launchJackServer(command))
        return false;

    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < 8000) {
        QThread::msleep(200);
        status = static_cast<jack_status_t>(0);
        if (jack_client_t* retry = jack_client_open("guitarpi_hex_probe", JackNoStartServer, &status)) {
            jack_client_close(retry);
            return true;
        }
    }

    qWarning("HexJackClient: jackd did not become ready in time");
    return false;
}

void HexJackClient::logJackStatus(jack_status_t status) const {
    if (status == 0)
        return;

    if (status & JackNameNotUnique)
        qWarning("HexJackClient: JACK client name not unique");
    if (status & JackServerStarted)
        qInfo("HexJackClient: JACK server started for this client");
    if (status & JackServerFailed)
        qWarning("HexJackClient: JACK server failed to launch");
    if (status & JackShmFailure)
        qWarning("HexJackClient: JACK shared memory setup failed");
    if (status & JackVersionError)
        qWarning("HexJackClient: JACK protocol version mismatch");
    if (status & JackLoadFailure)
        qWarning("HexJackClient: JACK requested driver failed to load");
    if (status & JackInitFailure)
        qWarning("HexJackClient: JACK driver failed to initialize");
    if (status & JackBackendError)
        qWarning("HexJackClient: JACK backend error reported");
    if (status & JackFailure)
        qWarning("HexJackClient: JACK operation reported failure");
}

bool HexJackClient::launchJackServer(const QString& command) const {
    if (command.trimmed().isEmpty()) {
        qWarning("HexJackClient: empty JACK command");
        return false;
    }

    if (!QProcess::startDetached(QStringLiteral("/bin/sh"), {QStringLiteral("-c"), command})) {
        qWarning("HexJackClient: failed to start JACK via %s", command.toUtf8().constData());
        return false;
    }

    return true;
}

void HexJackClient::connectSystemPorts() {
    if (!m_client)
        return;

    const auto connect = [this](const char* src, const char* dst) {
        const int rc = jack_connect(m_client, src, dst);
        if (rc != 0 && rc != EEXIST) {
            qWarning("HexJackClient: failed to connect %s -> %s (err=%d)", src, dst, rc);
        }
    };

    constexpr std::array<int, 6> kCapturePerString {{3, 4, 5, 6, 7, 8}}; // string 0 (low E) -> capture_3
    for (int s = 0; s < 6; ++s) {
        jack_port_t* port = m_inputs[static_cast<std::size_t>(s)];
        if (!port)
            continue;
        const char* dest = jack_port_name(port);
        if (!dest)
            continue;
        const std::string source = "system:capture_" + std::to_string(kCapturePerString[static_cast<std::size_t>(s)]);
        connect(source.c_str(), dest);
    }
}

void HexJackClient::announceCalibrationStep(int stringIndex, bool capturing) {
    QMetaObject::invokeMethod(this,
                              [this, stringIndex, capturing]() {
                                  emit calibrationStepChanged(stringIndex, capturing);
                              },
                              Qt::QueuedConnection);
}

void HexJackClient::handleCalibrationRequest(int targetString) {
    if (m_calibrationState.active)
        return;
    const int currentSr = std::max(1, m_currentSampleRate.load(std::memory_order_acquire));
    auto& state = m_calibrationState;
    state = CalibrationState{};
    state.active = true;
    state.capturing = false;
    state.partial = (targetString >= 0 && targetString < 6);
    state.sequenceCount = state.partial ? 1 : 6;
    for (int i = 0; i < state.sequenceCount; ++i)
        state.sequence[static_cast<std::size_t>(i)] = state.partial ? targetString : i;
    state.sequenceIndex = 0;
    state.currentString = state.sequence[0];
    state.framesRemaining = 0;
    state.captureFramesPerString = std::max(1, static_cast<int>(currentSr * kCalibrationCaptureSecPerString));
    state.updated.fill(false);
    state.sumRms.fill(0.0);
    state.samples.fill(0);
    state.peakRms.fill(0.0f);
    QMetaObject::invokeMethod(this, [this]() { emit calibrationStarted(); }, Qt::QueuedConnection);
    announceCalibrationStep(state.currentString, false);
}

void HexJackClient::advanceCalibration(float levels[6], jack_nframes_t nframes) {
    auto& state = m_calibrationState;
    if (!state.active)
        return;

    if (state.currentString < 0 || state.currentString >= 6) {
        state.active = false;
        announceCalibrationStep(-1, false);
        return;
    }

    const int idx = state.currentString;
    if (!state.capturing) {
        const float level = std::max(0.f, levels[idx]);
        if (level >= kCalibrationTriggerLevel) {
            state.capturing = true;
            state.framesRemaining = state.captureFramesPerString;
            state.sumRms[static_cast<std::size_t>(idx)] = 0.0;
            state.samples[static_cast<std::size_t>(idx)] = 0;
            state.peakRms[static_cast<std::size_t>(idx)] = 0.0f;
            announceCalibrationStep(idx, true);
        }
        return;
    }

    const std::size_t slot = static_cast<std::size_t>(idx);
    const float level = std::max(0.f, levels[idx]);
    state.sumRms[slot] += level;
    state.samples[slot] += 1;
    state.peakRms[slot] = std::max(state.peakRms[slot], level);
    state.framesRemaining -= static_cast<int>(nframes);

    if (state.framesRemaining > 0)
        return;

    state.capturing = false;
    state.framesRemaining = 0;
    state.updated[slot] = true;
    state.sequenceIndex += 1;

    if (state.sequenceIndex >= state.sequenceCount) {
        state.active = false;
        announceCalibrationStep(-1, false);

        std::array<float, 6> averages {};
        std::array<float, 6> peaks {};
        for (int s = 0; s < 6; ++s) {
            const std::size_t slotIdx = static_cast<std::size_t>(s);
            if (state.updated[slotIdx]) {
                const int count = state.samples[slotIdx];
                averages[slotIdx] = (count > 0)
                    ? static_cast<float>(state.sumRms[slotIdx] / static_cast<double>(count))
                    : 0.f;
                peaks[slotIdx] = state.peakRms[slotIdx];
            } else {
                averages[slotIdx] = -1.f;
                peaks[slotIdx] = -1.f;
            }
        }

        QMetaObject::invokeMethod(this, [this, averages, peaks]() {
            emit calibrationFinished(averages, peaks);
        }, Qt::QueuedConnection);
        state = CalibrationState{};
        return;
    }

    state.currentString = state.sequence[static_cast<std::size_t>(state.sequenceIndex)];
    announceCalibrationStep(state.currentString, false);
}
