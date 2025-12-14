#include "CarlaClient.h"

#include <QMetaObject>
#include <QTimer>
#include <QtGlobal>
#include <QtMath>
#include <QFileInfo>
#include <QByteArray>
#include <QString>
#include <QProcess>
#include <QElapsedTimer>
#include <QThread>
#include <QStringList>
#include <QRegularExpression>
#include <cstring>
#include <memory>
#include <array>
#include <string>
#include <algorithm>
#include <jack/jack.h>
#include <carla/includes/CarlaNativePlugin.h>

namespace {

constexpr int kMeterIntervalMs = 40; // ~25Hz UI update cadence
constexpr const char* kDefaultJackCommand = "JACK_NO_AUDIO_RESERVATION=1 jackd -R -P70 -d alsa -d hw:2,0 -p128 -n3 -r48000 -s";

float computeLevel(const jack_default_audio_sample_t* buffer, jack_nframes_t frames) {
    if (!buffer || frames == 0) return 0.0f;
    double sum = 0.0;
    for (jack_nframes_t i = 0; i < frames; ++i) {
        const double sample = buffer[i];
        sum += sample * sample;
    }
    const double rms = qSqrt(sum / static_cast<double>(frames));
    return static_cast<float>(qBound(0.0, rms, 1.0));
}

} // namespace

class CarlaClient::MeterPump {
public:
    explicit MeterPump(CarlaClient* owner) : m_owner(owner) {
        m_timer = std::make_unique<QTimer>();
        m_timer->setTimerType(Qt::CoarseTimer);
        m_timer->setInterval(kMeterIntervalMs);
        QObject::connect(m_timer.get(), &QTimer::timeout, owner, [owner]() { owner->emitMeters(); });
        m_timer->start();
    }

private:
    CarlaClient* m_owner;
    std::unique_ptr<QTimer> m_timer;
};

CarlaClient::CarlaClient(QObject* parent)
    : AudioEngine(parent) {
    m_pendingBufferSize.store(0);
    m_pendingSampleRate.store(0);
}

CarlaClient::~CarlaClient() {
    stop();
}

bool CarlaClient::start() {
    if (m_client) {
        return true;
    }

    if (!ensureJackServerRunning()) {
        qWarning("CarlaClient: JACK server unavailable; cannot start audio");
        return false;
    }

    jack_status_t status = static_cast<jack_status_t>(0);
    m_client = jack_client_open("guitarpi", JackNoStartServer, &status);
    if (!m_client) {
        logJackStatus(status);
        qWarning("CarlaClient: failed to open JACK client (status=%d)", static_cast<int>(status));
        return false;
    }

    if (status != 0) {
        logJackStatus(status);
    }

    jack_set_process_callback(m_client, &CarlaClient::processCallback, this);
    jack_set_buffer_size_callback(m_client, &CarlaClient::bufferSizeCallback, this);
    jack_set_sample_rate_callback(m_client, &CarlaClient::sampleRateCallback, this);
    jack_set_xrun_callback(m_client, &CarlaClient::xrunCallback, this);
    jack_on_shutdown(m_client, &CarlaClient::shutdownCallback, this);

    m_inputL = jack_port_register(m_client, "input_l", JACK_DEFAULT_AUDIO_TYPE, JackPortIsInput, 0);
    m_inputR = jack_port_register(m_client, "input_r", JACK_DEFAULT_AUDIO_TYPE, JackPortIsInput, 0);
    m_outputL = jack_port_register(m_client, "monitor_l", JACK_DEFAULT_AUDIO_TYPE, JackPortIsInput, 0);
    m_outputR = jack_port_register(m_client, "monitor_r", JACK_DEFAULT_AUDIO_TYPE, JackPortIsInput, 0);

    if (!m_inputL || !m_inputR || !m_outputL || !m_outputR) {
        qWarning("CarlaClient: failed to register JACK ports");
        stop();
        return false;
    }

    m_currentBufferSize.store(static_cast<int>(jack_get_buffer_size(m_client)));
    m_currentSampleRate.store(static_cast<int>(jack_get_sample_rate(m_client)));

    const int requestedFrames = m_pendingBufferSize.load();
    if (requestedFrames > 0 && requestedFrames != m_currentBufferSize.load()) {
        requestJackBufferSize(requestedFrames);
    }

    if (jack_activate(m_client) != 0) {
        qWarning("CarlaClient: failed to activate JACK client");
        stop();
        return false;
    }

    connectSystemPorts();

    m_meterPump = std::make_unique<MeterPump>(this);

    QMetaObject::invokeMethod(this, [this]() {
        emit bufferConfigChanged(sampleRate(), bufferSize());
        emit xrunsChanged(m_xruns.load());
    }, Qt::QueuedConnection);

    if (!configureAndStartCarlaHost()) {
        qWarning("CarlaClient: Carla host unavailable; running JACK passthrough only");
    } else if (!loadDefaultPluginChain()) {
        qWarning("CarlaClient: default plugin chain failed to load (check LV2 packages)");
    } else {
        connectRackToSystem();
    }

    return true;
}

void CarlaClient::stop() {
    shutdownCarlaHost();

    if (m_meterPump) {
        m_meterPump.reset();
    }

    if (m_client) {
        jack_client_t* client = m_client;
        m_client = nullptr;
        jack_client_close(client);
    }

    m_inputL = nullptr;
    m_inputR = nullptr;
    m_outputL = nullptr;
    m_outputR = nullptr;
    m_currentBufferSize.store(0);
    m_currentSampleRate.store(0);
}

void CarlaClient::setBufferSize(int frames) {
    m_pendingBufferSize.store(frames);
    if (m_client) {
        requestJackBufferSize(frames);
    }

    if (m_carlaEngineRunning && frames > 0) {
        carla_set_engine_option(m_carlaHost, CarlaBackend::ENGINE_OPTION_AUDIO_BUFFER_SIZE, frames, nullptr);
        if (!carla_set_engine_buffer_size_and_sample_rate(m_carlaHost, static_cast<uint>(frames), static_cast<double>(sampleRate()))) {
            logCarlaError("carla_set_engine_buffer_size_and_sample_rate");
        }
    }
}

void CarlaClient::setSampleRate(int sr) {
    m_pendingSampleRate.store(sr);
    if (m_client && sr != sampleRate()) {
        qWarning("CarlaClient: JACK running at %d Hz; restart server for %d Hz", sampleRate(), sr);
    }

    if (m_carlaEngineRunning && sr > 0) {
        carla_set_engine_option(m_carlaHost, CarlaBackend::ENGINE_OPTION_AUDIO_SAMPLE_RATE, sr, nullptr);
        if (!carla_set_engine_buffer_size_and_sample_rate(m_carlaHost, static_cast<uint>(bufferSize()), static_cast<double>(sr))) {
            logCarlaError("carla_set_engine_buffer_size_and_sample_rate");
        }
    }
}

bool CarlaClient::ensureJackServerRunning() {
    jack_status_t status = static_cast<jack_status_t>(0);
    if (jack_client_t* probe = jack_client_open("guitarpi_probe", JackNoStartServer, &status)) {
        jack_client_close(probe);
        return true;
    }

    if (!(status & JackServerFailed)) {
        logJackStatus(status);
        return false;
    }

    const QByteArray custom = qgetenv("GUITARPI_JACK_COMMAND");
    const QString command = custom.isEmpty() ? QString::fromUtf8(kDefaultJackCommand) : QString::fromUtf8(custom);
    qInfo("CarlaClient: launching JACK via command: %s", command.toUtf8().constData());
    if (!launchJackServer(command)) {
        return false;
    }

    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < 8000) {
        QThread::msleep(200);
        status = static_cast<jack_status_t>(0);
        if (jack_client_t* retry = jack_client_open("guitarpi_probe", JackNoStartServer, &status)) {
            jack_client_close(retry);
            return true;
        }
    }

    qWarning("CarlaClient: jackd did not become ready in time");
    return false;
}

void CarlaClient::logJackStatus(jack_status_t status) const {
    if (status == 0)
        return;

    if (status & JackNameNotUnique)
        qWarning("CarlaClient: JACK client name not unique");
    if (status & JackServerStarted)
        qInfo("CarlaClient: JACK server started for this client");
    if (status & JackServerFailed)
        qWarning("CarlaClient: JACK server failed to launch");
    if (status & JackShmFailure)
        qWarning("CarlaClient: JACK shared memory setup failed");
    if (status & JackVersionError)
        qWarning("CarlaClient: JACK protocol version mismatch");
    if (status & JackLoadFailure)
        qWarning("CarlaClient: JACK requested driver failed to load");
    if (status & JackInitFailure)
        qWarning("CarlaClient: JACK driver failed to initialize");
    if (status & JackBackendError)
        qWarning("CarlaClient: JACK backend error reported");
    if (status & JackFailure)
        qWarning("CarlaClient: JACK operation reported failure");
}

CarlaClient::JackServerConfig CarlaClient::detectJackServerConfig() const {
    JackServerConfig cfg;

    const QByteArray override = qgetenv("GUITARPI_JACK_DEVICE");
    if (!override.isEmpty()) {
        cfg.deviceName = QString::fromUtf8(override);
        return cfg;
    }

    QProcess proc;
    proc.start(QStringLiteral("aplay"), {QStringLiteral("-l")});
    if (!proc.waitForFinished(1000) || proc.error() == QProcess::FailedToStart) {
        proc.kill();
        proc.waitForFinished();
        return cfg;
    }

    if (proc.exitStatus() != QProcess::NormalExit || proc.exitCode() != 0) {
        return cfg;
    }

    const QString output = QString::fromUtf8(proc.readAllStandardOutput());
    const QRegularExpression re(QStringLiteral(R"(^card\s+(\d+):\s+([^\[]+?)\s*\[.+?\],\s*device\s+(\d+):)"), QRegularExpression::MultilineOption);
    QRegularExpressionMatchIterator it = re.globalMatch(output);

    QRegularExpressionMatch fallback;
    QRegularExpressionMatch preferred;
    while (it.hasNext()) {
        const QRegularExpressionMatch match = it.next();
        const QString card = match.captured(1);
        const QString name = match.captured(2).trimmed();
        const QString device = match.captured(3);

        const QString lower = name.toLower();
        if (lower.contains(QStringLiteral("scarlett")) || lower.contains(QStringLiteral("focusrite"))) {
            cfg.deviceName = QStringLiteral("hw:%1,%2").arg(card, device);
            return cfg;
        }

        if (!preferred.hasMatch() && (lower.contains(QStringLiteral("usb")) || lower.contains(QStringLiteral("audio")))) {
            preferred = match;
        }

        if (!fallback.hasMatch())
            fallback = match;
    }

    if (preferred.hasMatch()) {
        const QString card = preferred.captured(1);
        const QString device = preferred.captured(3);
        cfg.deviceName = QStringLiteral("hw:%1,%2").arg(card, device);
        return cfg;
    }

    if (fallback.hasMatch()) {
        const QString card = fallback.captured(1);
        const QString device = fallback.captured(3);
        cfg.deviceName = QStringLiteral("hw:%1,%2").arg(card, device);
    }

    return cfg;
}

bool CarlaClient::launchJackServer(const QString& command) const {
    if (command.trimmed().isEmpty()) {
        qWarning("CarlaClient: empty JACK command");
        return false;
    }

    if (!QProcess::startDetached(QStringLiteral("/bin/sh"), {QStringLiteral("-c"), command})) {
        qWarning("CarlaClient: failed to start JACK via %s", command.toUtf8().constData());
        return false;
    }

    return true;
}

int CarlaClient::processCallback(jack_nframes_t nframes, void* arg) {
    auto* self = static_cast<CarlaClient*>(arg);

    const auto* inL = static_cast<const jack_default_audio_sample_t*>(jack_port_get_buffer(self->m_inputL, nframes));
    const auto* inR = static_cast<const jack_default_audio_sample_t*>(jack_port_get_buffer(self->m_inputR, nframes));
    const auto* monL = static_cast<const jack_default_audio_sample_t*>(jack_port_get_buffer(self->m_outputL, nframes));
    const auto* monR = static_cast<const jack_default_audio_sample_t*>(jack_port_get_buffer(self->m_outputR, nframes));

    if (inL) {
        self->m_inMeterL.store(computeLevel(inL, nframes));
    }
    if (inR) {
        self->m_inMeterR.store(computeLevel(inR, nframes));
    }
    if (monL) {
        self->m_outMeterL.store(computeLevel(monL, nframes));
    }
    if (monR) {
        self->m_outMeterR.store(computeLevel(monR, nframes));
    }

    return 0;
}

int CarlaClient::bufferSizeCallback(jack_nframes_t nframes, void* arg) {
    auto* self = static_cast<CarlaClient*>(arg);
    self->m_currentBufferSize.store(static_cast<int>(nframes));
    QMetaObject::invokeMethod(self, [self]() {
        emit self->bufferConfigChanged(self->sampleRate(), self->bufferSize());
    }, Qt::QueuedConnection);
    return 0;
}

int CarlaClient::sampleRateCallback(jack_nframes_t nframes, void* arg) {
    auto* self = static_cast<CarlaClient*>(arg);
    self->m_currentSampleRate.store(static_cast<int>(nframes));
    QMetaObject::invokeMethod(self, [self]() {
        emit self->bufferConfigChanged(self->sampleRate(), self->bufferSize());
    }, Qt::QueuedConnection);
    return 0;
}

int CarlaClient::xrunCallback(void* arg) {
    auto* self = static_cast<CarlaClient*>(arg);
    const int count = self->m_xruns.fetch_add(1) + 1;
    QMetaObject::invokeMethod(self, [self, count]() { emit self->xrunsChanged(count); }, Qt::QueuedConnection);
    return 0;
}

void CarlaClient::shutdownCallback(void* arg) {
    auto* self = static_cast<CarlaClient*>(arg);
    QMetaObject::invokeMethod(self, [self]() { self->handleClientShutdown(); }, Qt::QueuedConnection);
}

void CarlaClient::emitMeters() {
    emit metersSnapshot(m_inMeterL.load(), m_inMeterR.load(), m_outMeterL.load(), m_outMeterR.load());
}

void CarlaClient::handleClientShutdown() {
    stop();
}

void CarlaClient::connectSystemPorts() {
    if (!m_client) return;

    const auto connect = [this](const char* src, const char* dst) {
        const int rc = jack_connect(m_client, src, dst);
        if (rc != 0 && rc != EEXIST) {
            qWarning("CarlaClient: failed to connect %s -> %s (err=%d)", src, dst, rc);
        }
    };

    const char* inL = jack_port_name(m_inputL);
    const char* inR = jack_port_name(m_inputR);

    connect("system:capture_1", inL);
    connect("system:capture_2", inR);

}

void CarlaClient::connectRackToSystem() {
    if (!m_client || !m_carlaEngineRunning) {
        return;
    }

    auto connect = [this](const char* src, const char* dst) {
        const int rc = jack_connect(m_client, src, dst);
        if (rc != 0 && rc != EEXIST) {
            qWarning("CarlaClient: failed to connect %s -> %s (err=%d)", src, dst, rc);
        }
    };

    const char** rackInputs = jack_get_ports(m_client, "GuitarPiRack", JACK_DEFAULT_AUDIO_TYPE, JackPortIsInput);
    const char** rackOutputs = jack_get_ports(m_client, "GuitarPiRack", JACK_DEFAULT_AUDIO_TYPE, JackPortIsOutput);

    if (!rackInputs || !rackOutputs) {
        if (rackInputs) {
            jack_free(rackInputs);
        }
        if (rackOutputs) {
            jack_free(rackOutputs);
        }
        qWarning("CarlaClient: unable to locate Carla rack ports; audio routing skipped");
        return;
    }

    const std::array<const char*, 2> systemIn {{"system:capture_1", "system:capture_2"}};
    const std::array<const char*, 2> systemOut {{"system:playback_1", "system:playback_2"}};

    for (size_t i = 0; i < systemIn.size() && rackInputs[i]; ++i) {
        connect(systemIn[i], rackInputs[i]);
    }

    for (size_t i = 0; i < systemOut.size(); ++i) {
        if (!rackOutputs[0])
            break;
        connect(rackOutputs[0], systemOut[i]);
        if (rackOutputs[1])
            connect(rackOutputs[1], systemOut[i]);
    }

    const char* monitorL = jack_port_name(m_outputL);
    const char* monitorR = jack_port_name(m_outputR);

    if (monitorL && rackOutputs[0]) {
        connect(rackOutputs[0], monitorL);
    }
    if (monitorR && rackOutputs[1]) {
        connect(rackOutputs[1], monitorR);
    }

    jack_free(rackInputs);
    jack_free(rackOutputs);
}

void CarlaClient::requestJackBufferSize(int frames) {
    if (!m_client || frames <= 0) return;
#ifdef JACK_HAS_PORT_AUTOCONNECT_REQUEST
    Q_UNUSED(frames);
#else
    const int rc = jack_set_buffer_size(m_client, static_cast<jack_nframes_t>(frames));
    if (rc != 0) {
        qWarning("CarlaClient: jack_set_buffer_size(%d) failed (%d)", frames, rc);
    }
#endif
}

bool CarlaClient::ensureCarlaHost() {
    if (m_carlaHost) {
        return true;
    }

    m_carlaHost = carla_standalone_host_init();
    if (!m_carlaHost) {
        qWarning("CarlaClient: carla_standalone_host_init returned null");
        return false;
    }

    return true;
}

bool CarlaClient::configureAndStartCarlaHost() {
    if (m_carlaEngineRunning) {
        return true;
    }

    if (!ensureCarlaHost()) {
        return false;
    }

    const int buf = bufferSize();
    const int sr = sampleRate();

    if (buf > 0) {
        carla_set_engine_option(m_carlaHost, CarlaBackend::ENGINE_OPTION_AUDIO_BUFFER_SIZE, buf, nullptr);
    }
    if (sr > 0) {
        carla_set_engine_option(m_carlaHost, CarlaBackend::ENGINE_OPTION_AUDIO_SAMPLE_RATE, sr, nullptr);
    }

    carla_set_engine_option(m_carlaHost, CarlaBackend::ENGINE_OPTION_PROCESS_MODE, CarlaBackend::ENGINE_PROCESS_MODE_CONTINUOUS_RACK, nullptr);
    carla_set_engine_option(m_carlaHost, CarlaBackend::ENGINE_OPTION_FORCE_STEREO, 1, nullptr);
    carla_set_engine_option(m_carlaHost, CarlaBackend::ENGINE_OPTION_TRANSPORT_MODE, CarlaBackend::ENGINE_TRANSPORT_MODE_JACK, nullptr);

    const QByteArray prefix("guitarpi.");
    carla_set_engine_option(m_carlaHost, CarlaBackend::ENGINE_OPTION_CLIENT_NAME_PREFIX, 0, prefix.constData());

    if (!carla_engine_init(m_carlaHost, "JACK", "GuitarPiRack")) {
        logCarlaError("carla_engine_init");
        carla_host_handle_free(m_carlaHost);
        m_carlaHost = nullptr;
        return false;
    }

    m_carlaEngineRunning = true;
    return true;
}

bool CarlaClient::loadDefaultPluginChain() {
    if (!configureAndStartCarlaHost()) {
        return false;
    }

    carla_remove_all_plugins(m_carlaHost);
    m_pluginIds.clear();

    struct PluginSpec {
        const char* name;
        const char* bundle;
        const char* uri;
    };

    const std::array<PluginSpec, 5> specs = {
        PluginSpec{"Gate", "/usr/lib/lv2/abGate.lv2", "http://hippie.lt/lv2/gate"},
        PluginSpec{"EQ", "/usr/lib/lv2/Luftikus.lv2", "https://code.google.com/p/lkjb-plugins/luftikus"},
        PluginSpec{"Drive", "/usr/lib/lv2/gx_scream.lv2", "http://guitarix.sourceforge.net/plugins/gx_scream_#_scream_"},
        PluginSpec{"Cab IR", "/usr/lib/lv2/gx_cabinet.lv2", "http://guitarix.sourceforge.net/plugins/gx_cabinet#CABINET"},
        PluginSpec{"Limiter", "/usr/lib/lv2/mda.lv2", "http://drobilla.net/plugins/mda/Limiter"}
    };

    bool allOk = true;
    for (const auto& spec : specs) {
        if (!addPluginToChain(spec.name, spec.bundle, spec.uri)) {
            allOk = false;
        }
        carla_engine_idle(m_carlaHost);
    }

    return allOk;
}

bool CarlaClient::addPluginToChain(const char* name, const char* bundlePath, const char* uri) {
    if (!m_carlaEngineRunning || !m_carlaHost) {
        return false;
    }

    if (!QFileInfo::exists(QString::fromUtf8(bundlePath))) {
        qWarning("CarlaClient: LV2 bundle missing: %s", bundlePath);
        return false;
    }

    const QByteArray bundleBytes(bundlePath);
    const QByteArray uriBytes(uri);
    const QByteArray nameBytes = name ? QByteArray(name) : QByteArray();

    if (!carla_add_plugin(m_carlaHost, CarlaBackend::BINARY_NATIVE, CarlaBackend::PLUGIN_LV2,
                          bundleBytes.constData(),
                          name ? nameBytes.constData() : nullptr,
                          uriBytes.constData(), 0, nullptr, 0)) {
        logCarlaError(name ? name : "carla_add_plugin");
        return false;
    }

    const uint32_t count = carla_get_current_plugin_count(m_carlaHost);
    if (count == 0) {
        return false;
    }

    const uint32_t pluginId = count - 1;
    m_pluginIds.push_back(pluginId);
    carla_set_active(m_carlaHost, pluginId, true);
    return true;
}

void CarlaClient::shutdownCarlaHost() {
    if (!m_carlaHost) {
        return;
    }

    if (m_carlaEngineRunning) {
        carla_remove_all_plugins(m_carlaHost);
        carla_engine_close(m_carlaHost);
        m_carlaEngineRunning = false;
    }

    m_pluginIds.clear();
    carla_host_handle_free(m_carlaHost);
    m_carlaHost = nullptr;
}

void CarlaClient::logCarlaError(const char* context) const {
    const char* errorMessage = nullptr;
    if (m_carlaHost) {
        errorMessage = carla_get_last_error(m_carlaHost);
    }

    if (errorMessage && *errorMessage) {
        qWarning("CarlaClient: %s failed: %s", context ? context : "Carla call", errorMessage);
    } else {
        qWarning("CarlaClient: %s failed", context ? context : "Carla call");
    }
}

