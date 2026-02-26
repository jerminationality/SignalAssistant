#include "AppController.h"
#include "SessionLogger.h"
#include "RecordedSessionPlayer.h"
#include "audio/CarlaClient.h"
#include "audio/HexJackClient.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <QProcess>
#include <QStandardPaths>
#include <QtGlobal>
#include <QTimer>
#include <algorithm>
#include <memory>

#ifdef __linux__
#include <arpa/inet.h>
#include <netdb.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

// ---------------------------------------------------------------------------
// Telemetry monitor helpers
// ---------------------------------------------------------------------------
namespace {

// Resolve monitor.py: check env override first, then walk up from the
// executable dir so it works both during development (build/bin/) and on
// the Pi (~/SignalAssistant/bin/).
static QString findMonitorScript() {
    const QByteArray override = qgetenv("GUITARPI_MONITOR_SCRIPT");
    if (!override.isEmpty())
        return QString::fromUtf8(override);

    const QDir appDir(QCoreApplication::applicationDirPath());
    for (int levels = 1; levels <= 3; ++levels) {
        QString candidate = appDir.absolutePath();
        for (int i = 0; i < levels; ++i)
            candidate = QFileInfo(candidate).path();
        candidate = QDir(candidate).filePath(QStringLiteral("monitor.py"));
        if (QFileInfo::exists(candidate))
            return candidate;
    }
    return {};
}

// Returns the machine hostname (falls back to "localhost" on failure).
static QString localHostname() {
#ifdef __linux__
    char buf[256] = {};
    if (gethostname(buf, sizeof(buf) - 1) == 0 && buf[0] != '\0')
        return QString::fromUtf8(buf);
#endif
    return QStringLiteral("localhost");
}

// Non-blocking TCP probe: returns true if something is listening on
// 127.0.0.1:<port>.  Used to confirm Streamlit actually started.
static bool tcpPortOpen(int port) {
#ifdef __linux__
    const int sock = ::socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) return false;
    struct sockaddr_in addr {};
    addr.sin_family      = AF_INET;
    addr.sin_port        = htons(static_cast<uint16_t>(port));
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    const bool open = (::connect(sock, reinterpret_cast<struct sockaddr*>(&addr),
                                 sizeof(addr)) == 0);
    ::close(sock);
    return open;
#else
    Q_UNUSED(port)
    return false;
#endif
}

} // namespace

// ---------------------------------------------------------------------------
// Streamlit monitor launcher (member – runs as a child of AppController so
// its stdout/stderr are forwarded to the parent terminal).
// ---------------------------------------------------------------------------
int AppController::tryLaunchMonitor()
{
    // --- Pre-flight 1: locate streamlit binary -----------------------------
    const QString streamlitBin = QStandardPaths::findExecutable(QStringLiteral("streamlit"));
    if (streamlitBin.isEmpty()) {
        qCritical("\n"
                  "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n"
                  " RMS MONITOR: streamlit not found in PATH               \n"
                  " Install it:  pip install streamlit                      \n"
                  "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━");
        return -1;
    }

    // --- Pre-flight 2: locate monitor.py -----------------------------------
    const QString script = findMonitorScript();
    if (script.isEmpty()) {
        qCritical("\n"
                  "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n"
                  " RMS MONITOR: monitor.py not found                      \n"
                  " Set override: GUITARPI_MONITOR_SCRIPT=/path/monitor.py \n"
                  "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━");
        return -1;
    }

    const QByteArray portEnv = qgetenv("PORT");
    const QString portStr = portEnv.isEmpty() ? QStringLiteral("8501") : QString::fromUtf8(portEnv);
    const int port = portStr.toInt();

    QStringList args {
        QStringLiteral("run"), script,
        QStringLiteral("--server.port"),     portStr,
        QStringLiteral("--server.address"),  QStringLiteral("0.0.0.0"),
        QStringLiteral("--server.headless"), QStringLiteral("true")
    };

    // Prefer taskset -c 0 to pin alongside network IRQs
    QString program;
    if (!QStandardPaths::findExecutable(QStringLiteral("taskset")).isEmpty()) {
        program = QStringLiteral("taskset");
        args.prepend(QStringLiteral("streamlit"));
        args.prepend(QStringLiteral("0"));
        args.prepend(QStringLiteral("-c"));
    } else {
        program = QStringLiteral("streamlit");
    }

    // Use a child QProcess with forwarded channels so Streamlit's startup
    // output (including the web UI URL) appears in this terminal.
    m_streamlitProcess = new QProcess(this);
    m_streamlitProcess->setProcessChannelMode(QProcess::ForwardedChannels);
    m_streamlitProcess->start(program, args);

    if (!m_streamlitProcess->waitForStarted(5000)) {
        qCritical("\n"
                  "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n"
                  " RMS MONITOR: process failed to start (OS error)        \n"
                  "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━");
        m_streamlitProcess->deleteLater();
        m_streamlitProcess = nullptr;
        return -1;
    }

    const QString url = QStringLiteral("http://%1:%2").arg(localHostname(), portStr);
    qInfo("\n"
          "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n"
          " RMS MONITOR: starting… (confirm in ~5 s)              \n"
          "   URL : %s                                            \n"
          " streamlit: %s                                         \n"
          " script   : %s                                         \n"
          "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━",
          url.toUtf8().constData(),
          streamlitBin.toUtf8().constData(),
          script.toUtf8().constData());

    return port;
}

AppController::AppController(const RunSessionOptions& options, QObject* parent)
    : QObject(parent)
    , m_tuningController(this)
    , m_runOptions(options) {
    // TODO(Copilot): Load persisted settings (sample rate, buffer size, last preset)
    qInfo() << "AppController" << "ctor" << (m_runOptions.isRecorded() ? "recorded" : "live")
            << QString::fromStdString(m_runOptions.sessionName);
    initializeTestPlayback();
}

AppController::~AppController() = default;

void AppController::setCurrentPreset(const QString& p) {
    if (p == m_currentPreset) return;
    m_currentPreset = p;
    emit currentPresetChanged();
    // TODO(Copilot): Push preset params to audio engine
}

QStringList AppController::availablePresets() const {
    // TODO(Copilot): scan preset directory; for now return mock list
    return {"Default", "Crunch", "Chime", "Lead"};
}

void AppController::savePreset(const QString& /*name*/) {
    // TODO(Copilot): Serialize chain + params + IR path to YAML/JSON
}

void AppController::loadPreset(const QString& name) {
    setCurrentPreset(name);
    // TODO(Copilot): Deserialize preset and push params to audio engine
}

void AppController::setBufferSize(int frames) {
    m_requestedBufferSize = qMax(0, frames);

    if (m_audioClient && m_audioRunning) {
        m_audioClient->setBufferSize(m_requestedBufferSize);
    }

    if (m_hexClient && m_hexRunning) {
        m_hexClient->setBufferSize(m_requestedBufferSize);
    }

    if (!m_audioRunning) {
        updateLatencyText(m_activeSampleRate, m_requestedBufferSize);
    }
}

void AppController::setSampleRate(int sr) {
    m_requestedSampleRate = qMax(0, sr);

    if (m_audioClient && m_audioRunning) {
        m_audioClient->setSampleRate(m_requestedSampleRate);
    }

    if (m_hexClient && m_hexRunning) {
        m_hexClient->setSampleRate(m_requestedSampleRate);
    }

    if (!m_audioRunning) {
        updateLatencyText(m_requestedSampleRate, m_activeBufferSize);
    }
}

void AppController::startAudio() {
    qInfo() << "AppController" << "startAudio" << "requested"
            << "sr" << m_requestedSampleRate << "buffer" << m_requestedBufferSize;

    if (m_runOptions.isRecorded()) {
        qInfo() << "AppController" << "startAudio" << "recorded-mode-skip";
        return;
    }

    if (!ensureAudioClient()) {
        qWarning("AppController: unable to allocate audio client");
        return;
    }

    if (m_hexClient && !m_hexRunning) {
        if (m_requestedSampleRate > 0)
            m_hexClient->setSampleRate(m_requestedSampleRate);
        if (m_requestedBufferSize > 0)
            m_hexClient->setBufferSize(m_requestedBufferSize);

        if (m_hexClient->start()) {
            m_hexRunning = true;
            qInfo() << "AppController" << "hex" << "started"
                    << "sr" << m_requestedSampleRate << "buffer" << m_requestedBufferSize;
            const int monitorPort = tryLaunchMonitor();
            if (monitorPort > 0) {
                // Confirm the HTTP server actually came up after 5 s.
                // Runs on the Qt event loop — zero impact on the audio thread.
                QTimer::singleShot(5000, this, [monitorPort, this]() {
                    const QString host = localHostname();
                    const QString url  = QStringLiteral("http://%1:%2").arg(host).arg(monitorPort);
                    if (tcpPortOpen(monitorPort)) {
                        qInfo("\n"
                              "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n"
                              " RMS MONITOR: ✅ server confirmed listening             \n"
                              "   URL : %s                                             \n"
                              "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━",
                              url.toUtf8().constData());
                    } else {
                        qCritical("\n"
                                  "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n"
                                  " RMS MONITOR: ❌ server did NOT start on port %d       \n"
                                  " Check: journalctl -u streamlit  or run manually:      \n"
                                  "   streamlit run ~/SignalAssistant/monitor.py          \n"
                                  "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━",
                                  monitorPort);
                    }
                });
            }
        } else {
            qWarning("AppController: hex capture start failed");
        }
    }

    if (m_audioRunning && m_audioClient) {
        if (m_requestedSampleRate > 0) {
            m_audioClient->setSampleRate(m_requestedSampleRate);
        }
        if (m_requestedBufferSize > 0) {
            m_audioClient->setBufferSize(m_requestedBufferSize);
        }
        return;
    }

    if (m_requestedSampleRate > 0) {
        m_audioClient->setSampleRate(m_requestedSampleRate);
    }
    if (m_requestedBufferSize > 0) {
        m_audioClient->setBufferSize(m_requestedBufferSize);
    }

    if (m_audioClient->start()) {
        m_audioRunning = true;
        qInfo() << "AppController" << "audio" << "started"
                << "sr" << m_requestedSampleRate << "buffer" << m_requestedBufferSize;
    } else {
        qWarning("AppController: audio start failed");
    }
}

void AppController::stopAudio() {
    if (m_runOptions.isRecorded())
        return;

    if (m_audioClient && m_audioRunning) {
        m_audioClient->stop();
        m_audioRunning = false;
        qInfo() << "AppController" << "audio" << "stopped";
    }

    if (m_hexClient && m_hexRunning) {
        m_hexClient->stop();
        m_hexRunning = false;
        qInfo() << "AppController" << "hex" << "stopped";
    }

    updateLatencyText(m_requestedSampleRate, m_requestedBufferSize);
}

void AppController::toggleLiveRecording() {
    if (m_runOptions.isRecorded())
        return;

    if (!m_tabBridge.recording() && m_liveRecordingAwaitingLabel)
        cancelLiveRecordingLabel();

    const bool active = m_tabBridge.recording();
    if (!active) {
        m_tabBridge.setRecording(true);
        m_liveRecordingTimer.restart();
        m_lastLiveRecordingDuration = 0.0;
        m_liveRecordingAwaitingLabel = false;
        SessionLogger::instance().log("live-record", "start");
        return;
    }

    m_tabBridge.setRecording(false);
    m_lastLiveRecordingDuration = m_liveRecordingTimer.isValid()
        ? static_cast<qreal>(m_liveRecordingTimer.elapsed()) / 1000.0
        : 0.0;
    SessionLogger::instance().logf("live-record",
                                   "stop duration=%.2f",
                                   static_cast<double>(m_lastLiveRecordingDuration));
    m_liveRecordingAwaitingLabel = true;
    emit liveRecordingLabelRequested();
}

void AppController::submitLiveRecordingLabel(const QString& label) {
    if (!m_liveRecordingAwaitingLabel)
        return;

    const QString timestamp = QDateTime::currentDateTimeUtc().toString(QStringLiteral("yyyyMMdd-HHmmss"));
    QString trimmed = label.trimmed();
    QString finalLabel = timestamp;
    if (!trimmed.isEmpty())
        finalLabel = QStringLiteral("%1 %2").arg(timestamp, trimmed);

    SessionLogger::instance().logf("live-record",
                                   "label='%s' duration=%.2f",
                                   finalLabel.toUtf8().constData(),
                                   static_cast<double>(m_lastLiveRecordingDuration));
    if (!m_tabBridge.exportPendingCapture(finalLabel)) {
        qWarning() << "AppController: failed to persist live recording for label" << finalLabel;
    }
    m_liveRecordingAwaitingLabel = false;
}

void AppController::cancelLiveRecordingLabel() {
    if (!m_liveRecordingAwaitingLabel)
        return;
    SessionLogger::instance().logf("live-record",
                                   "label-cancelled duration=%.2f",
                                   static_cast<double>(m_lastLiveRecordingDuration));
    m_tabBridge.discardPendingCapture();
    m_liveRecordingAwaitingLabel = false;
}

bool AppController::ensureAudioClient() {
    if (!m_audioClient) {
        auto client = std::make_unique<CarlaClient>(this);
        qInfo() << "AppController" << "carla-client" << "created";

        connect(client.get(), &CarlaClient::bufferConfigChanged, this, [this](int sampleRate, int bufferSize) {
            m_activeSampleRate = sampleRate;
            m_activeBufferSize = bufferSize;
            updateLatencyText(sampleRate, bufferSize);
        });

        connect(client.get(), &CarlaClient::xrunsChanged, this, [](int count) {
            // qInfo("Carla xruns: %d", count);
            (void)count;
        });

        m_audioClient = std::move(client);
    }

    if (!m_hexClient) {
        auto client = std::make_unique<HexJackClient>(this);
        qInfo() << "AppController" << "hex-client" << "created";

        connect(client.get(), &HexJackClient::xrunsChanged, this, [](int count) {
            // qInfo("HexJack xruns: %d", count);
            (void)count;
        });

        m_hexClient = std::move(client);
    }

    if (m_hexClient) {
        m_tabBridge.setAudioClient(m_hexClient.get());
        m_hexClient->setLiveMonitorEnabled(m_liveHexMonitorEnabled);
    }

    return true;
}

void AppController::updateLatencyText(int sampleRate, int bufferSize) {
    QString text = QStringLiteral("—");

    if (sampleRate > 0 && bufferSize > 0) {
        const double latencyMs = (static_cast<double>(bufferSize) / static_cast<double>(sampleRate)) * 1000.0;
        text = QString::number(latencyMs, 'f', latencyMs >= 10.0 ? 1 : 2) + QStringLiteral(" ms");
    }

    if (text != m_latencyText) {
        m_latencyText = text;
        emit latencyTextChanged();
    }
}

void AppController::initializeTestPlayback() {
    qInfo() << "AppController" << "initTestPlayback" << (m_runOptions.isRecorded() ? "recorded" : "live")
            << QString::fromStdString(m_runOptions.sessionName);
    m_testSessionName = QString::fromStdString(m_runOptions.sessionName);
    if (m_testSessionName.isEmpty())
        m_testSessionName = m_runOptions.isRecorded() ? QStringLiteral("Recorded Session") : QStringLiteral("Live Input");

    const bool autoPlayRequested = autoTestPlayEnabled();
    if (!m_runOptions.isRecorded()) {
        m_testPlaybackDuration = 0.0;
        m_testPlaybackPosition = 0.0;
        m_testPlaybackProgress = 0.0;
        m_testPlaybackState = QStringLiteral("Live");
        emit testSessionChanged();
        emitTestPlaybackChanged();
        return;
    }

    m_recordedPlayer = std::make_unique<RecordedSessionPlayer>(&m_tabBridge);
    connect(m_recordedPlayer.get(), &RecordedSessionPlayer::playbackProgress,
            this, &AppController::handleRecordedProgress);
    connect(m_recordedPlayer.get(), &RecordedSessionPlayer::playbackFinished,
            this, &AppController::handleRecordedFinished);
    connect(m_recordedPlayer.get(), &RecordedSessionPlayer::playbackError,
            this, &AppController::handleRecordedError);

    if (!m_recordedPlayer->loadSession(m_runOptions)) {
        m_testPlaybackDuration = 0.0;
        m_testPlaybackState = QStringLiteral("Error");
    } else {
        m_testPlaybackDuration = static_cast<qreal>(m_recordedPlayer->durationSec());
        m_testPlaybackState = QStringLiteral("Idle");
        m_recordedPlayer->setHexMonitorEnabled(m_testHexAudioEnabled);
        const bool reported = m_recordedPlayer->hexMonitorEnabled();
        if (reported != m_testHexAudioEnabled) {
            m_testHexAudioEnabled = reported;
            emit testPlaybackSettingsChanged();
        }
    }
    m_testPlaybackPosition = 0.0;
    m_testPlaybackProgress = 0.0;

    emit testSessionChanged();
    emitTestPlaybackChanged();

    if (autoPlayRequested && !m_autoTestPlaybackTriggered && m_recordedPlayer && m_recordedPlayer->isReady()) {
        m_autoTestPlaybackTriggered = true;
        QTimer::singleShot(0, this, [this]() {
            qInfo() << "AppController" << "auto-test-play" << "triggered";
            testPlay();
        });
    }
}

void AppController::emitTestPlaybackChanged() {
    emit testPlaybackChanged();
}

void AppController::logTestAction(const char* action) const {
    if (!m_runOptions.isRecorded() || !action)
        return;
    SessionLogger::instance().logf("test-mode",
                                   "%s session='%s' position=%.2f duration=%.2f",
                                   action,
                                   m_testSessionName.toUtf8().constData(),
                                   static_cast<double>(m_testPlaybackPosition),
                                   static_cast<double>(m_testPlaybackDuration));
}

void AppController::testPlay() {
    if (!m_runOptions.isRecorded())
        return;

    if (!m_recordedPlayer || !m_recordedPlayer->isReady()) {
        qWarning() << "AppController" << "testPlay" << "recorded player not ready";
        return;
    }

    if (m_testPlaybackDuration > 0.0 && m_testPlaybackPosition >= m_testPlaybackDuration) {
        m_testPlaybackPosition = 0.0;
        m_testPlaybackProgress = 0.0;
    }

    if (qFuzzyIsNull(m_testPlaybackPosition))
        m_tabBridge.clear();

    if (!m_recordedPlayer->play()) {
        qWarning() << "AppController" << "testPlay" << "failed to start playback";
        return;
    }

    m_testPlaying = true;
    m_testPlaybackState = QStringLiteral("Playing");
    logTestAction("play");
    emitTestPlaybackChanged();
}

void AppController::testPause() {
    if (!m_runOptions.isRecorded() || !m_testPlaying)
        return;

    if (m_recordedPlayer)
        m_recordedPlayer->pause();
    m_testPlaying = false;
    m_testPlaybackState = QStringLiteral("Paused");
    logTestAction("pause");
    emitTestPlaybackChanged();
}

void AppController::testStop() {
    if (!m_runOptions.isRecorded())
        return;

    if (m_recordedPlayer)
        m_recordedPlayer->stop();
    m_tabBridge.requestRefresh();
    m_testPlaying = false;
    m_testPlaybackState = QStringLiteral("Stopped");
    m_testPlaybackPosition = 0.0;
    m_testPlaybackProgress = 0.0;
    logTestAction("stop");
    emitTestPlaybackChanged();
}

void AppController::testTogglePlayPause() {
    if (!m_runOptions.isRecorded())
        return;
    if (m_testPlaying)
        testPause();
    else
        testPlay();
}

void AppController::setTestHexAudioEnabled(bool enabled) {
    if (!m_runOptions.isRecorded())
        return;
    bool effective = enabled;
    if (m_recordedPlayer) {
        m_recordedPlayer->setHexMonitorEnabled(enabled);
        effective = m_recordedPlayer->hexMonitorEnabled();
    }

    const bool changed = (m_testHexAudioEnabled != effective);
    if (changed)
        m_testHexAudioEnabled = effective;
    if (changed || enabled != effective)
        emit testPlaybackSettingsChanged();
}

void AppController::setTestLoopEnabled(bool enabled) {
    if (!m_runOptions.isRecorded())
        return;
    const bool normalized = enabled;
    if (m_testLoopEnabled == normalized)
        return;
    m_testLoopEnabled = normalized;
    emit testPlaybackSettingsChanged();
}

void AppController::setLiveHexMonitorEnabled(bool enabled) {
    if (m_runOptions.isRecorded())
        return;

    const bool normalized = enabled;
    if (!ensureAudioClient())
        return;

    if (m_hexClient)
        m_hexClient->setLiveMonitorEnabled(normalized);

    if (m_liveHexMonitorEnabled == normalized)
        return;

    m_liveHexMonitorEnabled = normalized;
    emit liveHexMonitorChanged();
}

void AppController::testSeekToProgress(qreal normalized) {
    if (!m_runOptions.isRecorded())
        return;
    if (!m_recordedPlayer || !m_recordedPlayer->isReady())
        return;

    const double ratio = std::clamp(static_cast<double>(normalized), 0.0, 1.0);
    if (!m_recordedPlayer->seekToProgress(ratio))
        return;

    m_tabBridge.clear();
    const double duration = m_recordedPlayer->durationSec();
    m_testPlaybackDuration = static_cast<qreal>(duration);
    m_testPlaybackPosition = static_cast<qreal>(m_recordedPlayer->positionSec());
    if (duration > 0.0)
        m_testPlaybackProgress = std::clamp(m_testPlaybackPosition / static_cast<qreal>(duration), 0.0, 1.0);
    else
        m_testPlaybackProgress = 0.0;
    emitTestPlaybackChanged();
}

void AppController::handleRecordedProgress(double positionSec, double durationSec) {
    if (!m_runOptions.isRecorded())
        return;

    if (durationSec > 0.0)
        m_testPlaybackDuration = static_cast<qreal>(durationSec);

    m_testPlaybackPosition = qMax<qreal>(0.0, static_cast<qreal>(positionSec));
    if (m_testPlaybackDuration > 0.0)
        m_testPlaybackProgress = std::clamp(m_testPlaybackPosition / m_testPlaybackDuration, 0.0, 1.0);
    else
        m_testPlaybackProgress = 0.0;

    emitTestPlaybackChanged();
}

void AppController::handleRecordedFinished() {
    if (!m_runOptions.isRecorded())
        return;

    const bool shouldLoop = m_testLoopEnabled;
    m_tabBridge.requestRefresh();
    m_testPlaying = false;
    m_testPlaybackState = shouldLoop ? QStringLiteral("Looping") : QStringLiteral("Complete");
    emitTestPlaybackChanged();

    if (!shouldLoop)
        return;

    QTimer::singleShot(0, this, [this]() {
        if (!m_testLoopEnabled || !m_recordedPlayer || !m_recordedPlayer->isReady())
            return;
        m_testPlaybackPosition = 0.0;
        m_testPlaybackProgress = 0.0;
        emitTestPlaybackChanged();
        testPlay();
    });
}

void AppController::handleRecordedError(const QString& description) {
    qWarning() << "AppController" << "recorded-playback-error" << description;
    if (!m_runOptions.isRecorded())
        return;

    m_testPlaying = false;
    m_testPlaybackState = QStringLiteral("Error");
    emitTestPlaybackChanged();
}

bool AppController::autoTestPlayEnabled() const {
    static constexpr const char* kEnv = "GUITARPI_AUTO_TEST_PLAY";
    return qEnvironmentVariableIsSet(kEnv);
}
