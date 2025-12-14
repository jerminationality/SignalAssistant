#include "TabEngineBridge.h"

#include "SessionLogger.h"
#include "NoteDetectionStore.h"
#include "audio/HexAudioClient.h"
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QVariantMap>
#include <QDebug>
#include <QStringList>
#include <QByteArray>
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>
#include <QMetaObject>
#include <QStandardPaths>
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QDateTime>
#include <limits>
#include <sndfile.h>
#include <system_error>
#include <cmath>

namespace {
constexpr float kSessionWaveTapSeconds = 8.0f;
QString calibrationStringName(int index) {
    static const std::array<const char*, 6> kNames{{"Low E", "A", "D", "G", "B", "High e"}};
    if (index < 0 || index >= static_cast<int>(kNames.size()))
        return QStringLiteral("string");
    return QString::fromLatin1(kNames[static_cast<std::size_t>(index)]);
}
}

TabEngineBridge::TabEngineBridge(QObject* parent)
    : QObject(parent)
    , m_engine(std::make_unique<TabEngine>(m_tuning, m_cfg))
{
    m_debugNoteLogging = qEnvironmentVariableIsSet("GUITARPI_TEST_LOG_NOTES");
    if (m_debugNoteLogging)
        qInfo() << "TabBridge" << "debug-note-logging" << "enabled";
    m_lastLiveTriggerSec.fill(-1.f);
    m_lastLiveFret.fill(-1);
    m_hexMeters.clear();
    for (int i = 0; i < 6; ++i)
        m_hexMeters.append(0.0);
    resetCalibrationSteps();
    loadPersistentCalibration();
    syncFromEngine();
    emit calibrationStatusChanged();
}

QVariantList TabEngineBridge::tuningDeviation() const {
    QVariantList list;
    list.reserve(6);
    for (float value : m_tuningDeviationCents)
        list.append(value);
    return list;
}

void TabEngineBridge::setTuningModeEnabled(bool enabled) {
    if (m_tuningModeEnabled == enabled)
        return;
    m_tuningModeEnabled = enabled;
    emit tuningModeEnabledChanged();
}

TabEngineBridge::~TabEngineBridge() {
    dumpSessionWaveSnapshot("shutdown");
}

int TabEngineBridge::liveBlockFramesHint() const {
    const int frames = m_lastProcessBlockFrames.load(std::memory_order_acquire);
    return frames > 0 ? frames : 128;
}

void TabEngineBridge::updateLiveMeters(const std::array<float, 6>& meters) {
    QVariantList list;
    list.reserve(6);
    for (float value : meters)
        list.append(value);
    m_hexMeters = list;
    emit hexMetersChanged();
}

void TabEngineBridge::handleCalibrationStarted() {
    m_calibrationRunning = true;
    if (m_partialCalibration && m_requestedCalibrationString >= 0) {
        const QString label = calibrationStringName(m_requestedCalibrationString);
        m_calibrationMessage = QStringLiteral("Pluck %1 (single string)").arg(label);
    } else if (m_activeCalibrationString < 0) {
        m_calibrationMessage = QStringLiteral("Calibrating... follow string prompts");
    }
    emit calibrationStatusChanged();
}

void TabEngineBridge::handleCalibrationStepChanged(int stringIndex, bool capturing) {
    if (!m_calibrationRunning)
        return;

    if (m_partialCalibration) {
        if (stringIndex < 0) {
            if (m_requestedCalibrationString >= 0)
                setCalibrationStepState(m_requestedCalibrationString, 3);
            m_activeCalibrationString = -1;
            m_activeCalibrationCapturing = false;
            m_calibrationMessage = QStringLiteral("Finalizing calibration...");
            emit calibrationStatusChanged();
            return;
        }
        if (stringIndex != m_requestedCalibrationString)
            return;

        m_activeCalibrationString = stringIndex;
        m_activeCalibrationCapturing = capturing;
        setCalibrationStepState(stringIndex, capturing ? 2 : 1);
        const QString label = calibrationStringName(stringIndex);
        m_calibrationMessage = capturing
            ? QStringLiteral("Recording %1").arg(label)
            : QStringLiteral("Pluck %1").arg(label);
        emit calibrationStatusChanged();
        return;
    }

    if (stringIndex < 0) {
        if (m_activeCalibrationString >= 0)
            setCalibrationStepState(m_activeCalibrationString, 3);
        for (int s = 0; s < 6; ++s)
            setCalibrationStepState(s, std::max(m_calibrationStepStates[static_cast<std::size_t>(s)], 3));
        m_activeCalibrationString = -1;
        m_activeCalibrationCapturing = false;
        m_calibrationMessage = QStringLiteral("Finalizing calibration...");
        emit calibrationStatusChanged();
        return;
    }

    if (stringIndex != m_activeCalibrationString) {
        if (m_activeCalibrationString >= 0)
            setCalibrationStepState(m_activeCalibrationString, 3);
        for (int s = 0; s < stringIndex; ++s)
            setCalibrationStepState(s, 3);
        for (int s = stringIndex + 1; s < 6; ++s) {
            const std::size_t slot = static_cast<std::size_t>(s);
            if (m_calibrationStepStates[slot] > 0 && m_calibrationStepStates[slot] < 3)
                setCalibrationStepState(s, 0);
        }
    }

    m_activeCalibrationString = stringIndex;
    m_activeCalibrationCapturing = capturing;
    setCalibrationStepState(stringIndex, capturing ? 2 : 1);

    const QString label = calibrationStringName(stringIndex);
    const QString step = QStringLiteral("%1/6").arg(stringIndex + 1);
    m_calibrationMessage = capturing
        ? QStringLiteral("Recording %1 (%2)").arg(label, step)
        : QStringLiteral("Pluck %1 (%2)").arg(label, step);

    emit calibrationStatusChanged();
}

void TabEngineBridge::handleCalibrationFinished(const std::array<float, 6>& averages,
                                                const std::array<float, 6>& peaks) {
    for (int s = 0; s < 6; ++s)
        setCalibrationStepState(s, std::max(m_calibrationStepStates[static_cast<std::size_t>(s)], 3));
    m_activeCalibrationString = -1;
    m_activeCalibrationCapturing = false;
    m_calibrationRunning = false;
    bool anyUpdated = false;
    for (int s = 0; s < 6; ++s) {
        const float avg = averages[static_cast<std::size_t>(s)];
        const float peak = peaks[static_cast<std::size_t>(s)];
        if (avg >= 0.f && peak >= 0.f) {
            m_calibrationProfile.avgRms[static_cast<std::size_t>(s)] = avg;
            m_calibrationProfile.peakRms[static_cast<std::size_t>(s)] = peak;
            // Calculate multiplier: targetRMS / avgInputRMS
            const float targetRms = NoteDetectionStore::instance().currentValueFromKey("targetRms", s);
            const float multiplier = (avg > 0.f) ? (targetRms / avg) : 1.0f;
            m_calibrationProfile.multipliers[static_cast<std::size_t>(s)] = std::clamp(multiplier, 0.2f, 8.0f);
            anyUpdated = true;
        }
    }

    if (anyUpdated) {
        // Store the calculated multipliers in the calibrationGainMultiplier parameters
        for (int s = 0; s < 6; ++s) {
            NoteDetectionStore::instance().setValueFromKey("calibrationGainMultiplier", s, 
                                                          m_calibrationProfile.multipliers[static_cast<std::size_t>(s)]);
        }
        m_calibrationProfile.valid = true;
        if (m_engine)
            m_engine->applyCalibration(m_calibrationProfile);
        savePersistentCalibration();
        
        // Log calibration data
        SessionLogger::instance().log("calibration", "=== Calibration Complete ===");
        for (int s = 0; s < 6; ++s) {
            const float targetRms = NoteDetectionStore::instance().currentValueFromKey("targetRms", s);
            SessionLogger::instance().logf("calibration", 
                "String %d: avgRms=%.6f peakRms=%.6f targetRms=%.6f multiplier=%.3f",
                s + 1,
                m_calibrationProfile.avgRms[static_cast<std::size_t>(s)],
                m_calibrationProfile.peakRms[static_cast<std::size_t>(s)],
                targetRms,
                m_calibrationProfile.multipliers[static_cast<std::size_t>(s)]);
        }
    }

    const QString updatedLabel = (m_partialCalibration && m_requestedCalibrationString >= 0)
        ? QStringLiteral("%1 updated").arg(calibrationStringName(m_requestedCalibrationString))
        : QStringLiteral("Calibration updated");
    m_calibrationMessage = updatedLabel;
    m_partialCalibration = false;
    m_requestedCalibrationString = -1;
    emit calibrationStatusChanged();
}

void TabEngineBridge::requestRefresh() {
    syncFromEngine();
}

void TabEngineBridge::clear() {
    if (m_engine) {
        m_engine->importEvents({});
    }
    {
        std::lock_guard<std::mutex> guard(m_liveMutex);
        m_livePending.clear();
    }
    m_liveTimeSec = 0.f;
    m_liveSampleRate = 0.f;
    m_lastDispatchedEvent.store(0, std::memory_order_release);
    m_lastLiveTriggerSec.fill(-1.f);
    m_lastLiveFret.fill(-1);
    syncFromEngine();
}

void TabEngineBridge::seedMockSession() {
    if (!m_engine) {
        return;
    }

    std::vector<NoteEvent> mock;

    NoteEvent ev{};
    ev.stringIdx = 5;
    ev.fret = 0;
    ev.midi = m_tuning.stringMidi[5];
    ev.startSec = 0.0f;
    ev.endSec = 1.4f;
    ev.velocity = 0.78f;
    ev.articulation.clear();
    mock.push_back(ev);

    ev.stringIdx = 4;
    ev.fret = 2;
    ev.midi = m_tuning.stringMidi[4] + ev.fret;
    ev.startSec = 0.45f;
    ev.endSec = 1.2f;
    ev.velocity = 0.65f;
    ev.articulation = "hammer";
    mock.push_back(ev);

    ev.stringIdx = 3;
    ev.fret = 2;
    ev.midi = m_tuning.stringMidi[3] + ev.fret;
    ev.startSec = 1.0f;
    ev.endSec = 1.6f;
    ev.velocity = 0.62f;
    ev.articulation = "slide";
    mock.push_back(ev);

    ev.stringIdx = 3;
    ev.fret = 4;
    ev.midi = m_tuning.stringMidi[3] + ev.fret;
    ev.startSec = 1.62f;
    ev.endSec = 2.1f;
    ev.velocity = 0.72f;
    ev.articulation = "slide";
    mock.push_back(ev);

    ev.stringIdx = 2;
    ev.fret = 0;
    ev.midi = m_tuning.stringMidi[2];
    ev.startSec = 2.2f;
    ev.endSec = 2.8f;
    ev.velocity = 0.35f;
    ev.articulation = "pm";
    mock.push_back(ev);

    m_engine->importEvents(mock);
    syncFromEngine();
}

void TabEngineBridge::setRecording(bool value) {
    // Treat the exposed "recording" property as a capture gate only. Live note detection
    // keeps running regardless so the fret overlay never requires the toggle.
    const bool prev = m_captureEnabled.exchange(value, std::memory_order_acq_rel);
    if (prev == value)
        return;

    qInfo() << "TabBridge" << (value ? "recording-start" : "recording-stop");

    if (value) {
        // Starting a new capture should clear any accumulated timeline so taps begin fresh.
        m_resetRequested.store(true, std::memory_order_release);
        if (m_pendingCaptureValid) {
            SessionLogger::instance().log("live-record", "pending capture discarded (new recording started before labeling)");
            clearPendingCapture();
        }
        m_captureSampleRate = m_liveSampleRate;
        for (auto& buffer : m_captureBuffers)
            buffer.clear();
    } else {
        // Finalise the current capture snapshot but keep live detection running.
        syncFromEngine();
        finalizeCaptureBuffers();
    }

    emit recordingChanged();
}

void TabEngineBridge::startCalibration() {
    if (!m_audioClient) {
        m_calibrationMessage = QStringLiteral("Audio input unavailable");
        emit calibrationStatusChanged();
        return;
    }
    if (m_calibrationRunning)
        return;

    setTuningModeEnabled(false);

    resetCalibrationSteps();
    m_partialCalibration = false;
    m_requestedCalibrationString = -1;
    m_calibrationRunning = true;
    m_calibrationMessage = QStringLiteral("Arming calibration...");
    emit calibrationStatusChanged();
    m_audioClient->requestCalibration(-1);
}

void TabEngineBridge::recalibrateString(int stringIndex) {
    if (stringIndex < 0 || stringIndex >= 6)
        return;
    if (!m_audioClient) {
        m_calibrationMessage = QStringLiteral("Audio input unavailable");
        emit calibrationStatusChanged();
        return;
    }
    if (!m_calibrationProfile.valid) {
        m_calibrationMessage = QStringLiteral("Run full calibration before per-string tweaks");
        emit calibrationStatusChanged();
        return;
    }
    if (m_calibrationRunning)
        return;

    setTuningModeEnabled(false);

    m_partialCalibration = true;
    m_requestedCalibrationString = stringIndex;
    markSingleCalibrationPending(stringIndex);
    m_calibrationRunning = true;
    const QString label = calibrationStringName(stringIndex);
    m_calibrationMessage = QStringLiteral("Preparing %1...").arg(label);
    emit calibrationStatusChanged();
    m_audioClient->requestCalibration(stringIndex);
}

void TabEngineBridge::setAudioClient(HexAudioClient* client) {
    if (m_audioClient == client)
        return;

    if (m_audioClient) {
        m_audioClient->setTabBridge(nullptr);
    }

    m_audioClient = client;
    m_externalMetersActive = (m_audioClient != nullptr);

    if (m_audioClient) {
        m_audioClient->setTabBridge(this);
        m_audioClient->connectMeters(this);
        m_audioClient->connectCalibration(this);
    }
}

void TabEngineBridge::processLiveAudioBlock(const float* const channels[6], int n, float sr) {
    if (!m_engine || n <= 0 || sr <= 0.f)
        return;

    m_lastProcessBlockFrames.store(n, std::memory_order_release);
    appendSessionWaveTap(channels, n, sr);

    const bool capturing = m_captureEnabled.load(std::memory_order_acquire);

    bool reset = m_resetRequested.exchange(false, std::memory_order_acq_rel);
    if (reset || std::fabs(m_liveSampleRate - sr) > 1e-4f) {
        m_engine->importEvents({});
        m_liveTimeSec = 0.f;
        m_liveSampleRate = sr;
        m_lastDispatchedEvent.store(0, std::memory_order_release);
        m_lastLiveTriggerSec.fill(-1.f);
        m_lastLiveFret.fill(-1);
        reset = true;
        if (m_debugNoteLogging)
            qInfo() << "TabBridge" << "engine-reset" << "sr" << sr << "capturing" << capturing;
    }

    if (!capturing && reset) {
        // Keep preview responsive when capture is off by avoiding stale time bases.
        m_liveTimeSec = 0.f;
    }

    if (capturing) {
        if (m_captureSampleRate <= 0.f || std::fabs(m_captureSampleRate - sr) > 1e-3f)
            m_captureSampleRate = sr;
        appendCaptureAudio(channels, n);
    }

    std::array<float, 6> blockRms {};
    if (n > 0) {
        for (int i = 0; i < 6; ++i) {
            const float* data = channels[static_cast<std::size_t>(i)];
            if (!data)
                continue;
            double sum = 0.0;
            for (int sample = 0; sample < n; ++sample) {
                const double value = static_cast<double>(data[sample]);
                sum += value * value;
            }
            blockRms[static_cast<std::size_t>(i)] = static_cast<float>(std::sqrt(sum / static_cast<double>(n)));
        }
    }

    if (!m_externalMetersActive)
        postMeterSnapshot(blockRms);

    if (m_debugNoteLogging) {
        QStringList rmsSummary;
        for (int i = 0; i < 6; ++i)
            rmsSummary << QStringLiteral("s%1=%2").arg(i + 1).arg(blockRms[static_cast<std::size_t>(i)], 0, 'f', 5);
        qInfo() << "TabBridge" << "block-rms" << rmsSummary.join(' ');
    }

    const float blockStart = m_liveTimeSec;
    m_engine->processBlock(channels, n, sr, blockStart);
    updateTuningDeviation();
    m_liveTimeSec += static_cast<float>(n) / sr;

    const auto& events = m_engine->events();
    const int total = static_cast<int>(events.size());
    int last = m_lastDispatchedEvent.load(std::memory_order_acquire);
    if (total <= last)
        return;

    std::vector<LiveEvent> newEvents;
    newEvents.reserve(static_cast<std::size_t>(total - last));
    for (int i = last; i < total; ++i) {
        const auto& ev = events[std::size_t(i)];
        if (ev.stringIdx < 0 || ev.stringIdx >= 6)
            continue;
        if (ev.fret < 0 || ev.fret > 24)
            continue;

        const float prevTrigger = m_lastLiveTriggerSec[std::size_t(ev.stringIdx)];
        const int prevFret = m_lastLiveFret[std::size_t(ev.stringIdx)];
        const float dt = (prevTrigger >= 0.f) ? ev.startSec - prevTrigger : std::numeric_limits<float>::infinity();
        if (prevTrigger >= 0.f && std::fabs(dt) < 0.06f && prevFret == ev.fret)
            continue;

        newEvents.push_back({ev.stringIdx, ev.fret, ev.velocity, ev.startSec});
        if (m_debugNoteLogging) {
            qInfo() << "TabBridge" << "note"
                    << "string" << ev.stringIdx
                    << "fret" << ev.fret
                    << "velocity" << QString::number(ev.velocity, 'f', 3)
                    << "start" << QString::number(ev.startSec, 'f', 3);
        }
    }

    m_lastDispatchedEvent.store(total, std::memory_order_release);

    if (newEvents.empty())
        return;

    {
        std::lock_guard<std::mutex> guard(m_liveMutex);
        m_livePending.insert(m_livePending.end(), newEvents.begin(), newEvents.end());
    }

    scheduleLiveDispatch();

    if (!capturing) {
        const int maxPreviewEvents = 256;
        if (total > maxPreviewEvents) {
            m_resetRequested.store(true, std::memory_order_release);
        }
    }
}

void TabEngineBridge::postMeterSnapshot(const std::array<float, 6>& meters) {
    QMetaObject::invokeMethod(this,
                              [this, meters]() { updateLiveMeters(meters); },
                              Qt::QueuedConnection);
}

void TabEngineBridge::syncFromEngine() {
    if (!m_engine) {
        if (!m_events.isEmpty()) {
            m_events.clear();
            m_eventsJson = "[]";
            emit eventsChanged();
        }
        return;
    }

    QVariantList list;
    list.reserve(static_cast<int>(m_engine->events().size()));
    for (const auto& ev : m_engine->events()) {
        QVariantMap map;
        map.insert(QStringLiteral("string"), ev.stringIdx);
        map.insert(QStringLiteral("fret"), ev.fret);
        map.insert(QStringLiteral("midi"), ev.midi);
        map.insert(QStringLiteral("start"), ev.startSec);
        map.insert(QStringLiteral("end"), ev.endSec);
        map.insert(QStringLiteral("velocity"), ev.velocity);
        map.insert(QStringLiteral("articulation"), QString());
        list.push_back(map);
    }

    m_events = list;
    const QJsonDocument doc = QJsonDocument::fromVariant(list);
    m_eventsJson = QString::fromUtf8(doc.toJson(QJsonDocument::Compact));
    emit eventsChanged();
}

void TabEngineBridge::scheduleLiveDispatch() {
    bool expected = false;
    if (m_dispatchQueued.compare_exchange_strong(expected, true, std::memory_order_acq_rel)) {
        QMetaObject::invokeMethod(this, [this]() { dispatchLiveEvents(); }, Qt::QueuedConnection);
    }
}

void TabEngineBridge::updateTuningDeviation() {
    if (!m_engine)
        return;
    const auto deviations = m_engine->tuningDeviationCents();
    if (deviations == m_tuningDeviationCents)
        return;
    m_tuningDeviationCents = deviations;
    emit tuningDeviationChanged();
}

void TabEngineBridge::dispatchLiveEvents() {
    std::vector<LiveEvent> batch;
    {
        std::lock_guard<std::mutex> guard(m_liveMutex);
        batch.swap(m_livePending);
        m_dispatchQueued.store(false, std::memory_order_release);
    }

    if (batch.empty())
        return;

    for (const auto& ev : batch) {
        m_lastLiveTriggerSec[std::size_t(ev.stringIndex)] = ev.startSec;
        m_lastLiveFret[std::size_t(ev.stringIndex)] = ev.fretIndex;
        emit liveNoteTriggered(ev.stringIndex, ev.fretIndex, ev.velocity);
    }
}

void TabEngineBridge::resetCalibrationSteps() {
    m_calibrationStepStates.fill(0);
    m_calibrationSteps.clear();
    m_calibrationSteps.reserve(6);
    for (int i = 0; i < 6; ++i)
        m_calibrationSteps.append(0);
    m_activeCalibrationString = -1;
    m_activeCalibrationCapturing = false;
}

void TabEngineBridge::setCalibrationStepState(int stringIdx, int state) {
    if (stringIdx < 0 || stringIdx >= 6)
        return;
    const std::size_t slot = static_cast<std::size_t>(stringIdx);
    if (m_calibrationStepStates[slot] == state)
        return;
    m_calibrationStepStates[slot] = state;
    if (m_calibrationSteps.size() < 6)
        m_calibrationSteps.resize(6);
    m_calibrationSteps[slot] = QVariant(state);
}

void TabEngineBridge::markSingleCalibrationPending(int stringIdx) {
    resetCalibrationSteps();
    if (stringIdx < 0 || stringIdx >= 6)
        return;
    setCalibrationStepState(stringIdx, 1);
}

QString TabEngineBridge::calibrationStoragePath() const {
    const QString base = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    if (base.isEmpty())
        return QString();
    QDir dir(base);
    if (!dir.exists())
        dir.mkpath(QStringLiteral("."));
    return dir.filePath(QStringLiteral("calibration_profile.json"));
}

void TabEngineBridge::loadPersistentCalibration() {
    const QString path = calibrationStoragePath();
    if (path.isEmpty())
        return;

    QFile file(path);
    if (!file.exists())
        return;
    if (!file.open(QIODevice::ReadOnly))
        return;

    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    if (!doc.isObject())
        return;

    const QJsonObject obj = doc.object();
    const QJsonArray avgArr = obj.value(QStringLiteral("avg")).toArray();
    const QJsonArray peakArr = obj.value(QStringLiteral("peak")).toArray();
    if (avgArr.size() != 6 || peakArr.size() != 6)
        return;

    std::array<float, 6> avg {};
    std::array<float, 6> peak {};
    for (int i = 0; i < 6; ++i) {
        avg[static_cast<std::size_t>(i)] = static_cast<float>(avgArr[i].toDouble());
        peak[static_cast<std::size_t>(i)] = static_cast<float>(peakArr[i].toDouble());
    }

    const bool valid = obj.value(QStringLiteral("valid")).toBool(true);
    if (!valid)
        return;

    m_calibrationProfile.avgRms = avg;
    m_calibrationProfile.peakRms = peak;
    
    // Load multipliers if present, otherwise calculate them
    const QJsonArray multArr = obj.value(QStringLiteral("multipliers")).toArray();
    if (multArr.size() == 6) {
        for (int i = 0; i < 6; ++i) {
            m_calibrationProfile.multipliers[static_cast<std::size_t>(i)] = static_cast<float>(multArr[i].toDouble(1.0));
        }
    } else {
        // Legacy: calculate multipliers from avg and current targetRms
        for (int i = 0; i < 6; ++i) {
            const float targetRms = NoteDetectionStore::instance().currentValueFromKey("targetRms", i);
            const float avgRms = avg[static_cast<std::size_t>(i)];
            m_calibrationProfile.multipliers[static_cast<std::size_t>(i)] = (avgRms > 0.f) ? (targetRms / avgRms) : 1.0f;
        }
    }
    
    // Load multipliers into parameters
    SessionLogger::instance().log("calibration", "Loading calibration profile multipliers into parameters");
    for (int i = 0; i < 6; ++i) {
        const float mult = m_calibrationProfile.multipliers[static_cast<std::size_t>(i)];
        SessionLogger::instance().logf("calibration", "String %d: setting multiplier to %.3f", i + 1, mult);
        NoteDetectionStore::instance().setValueFromKey("calibrationGainMultiplier", i, mult);
    }
    
    m_calibrationProfile.valid = true;
    if (m_engine)
        m_engine->applyCalibration(m_calibrationProfile);

    m_calibrationLoaded = true;
    m_calibrationMessage = QStringLiteral("Calibration loaded");
}

void TabEngineBridge::savePersistentCalibration() const {
    if (!m_calibrationProfile.valid)
        return;

    const QString path = calibrationStoragePath();
    if (path.isEmpty())
        return;

    QFileInfo info(path);
    QDir dir = info.dir();
    if (!dir.exists() && !dir.mkpath(QStringLiteral(".")))
        return;

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return;

    QJsonArray avgArr;
    QJsonArray peakArr;
    QJsonArray multArr;
    for (int i = 0; i < 6; ++i) {
        avgArr.append(m_calibrationProfile.avgRms[static_cast<std::size_t>(i)]);
        peakArr.append(m_calibrationProfile.peakRms[static_cast<std::size_t>(i)]);
        multArr.append(m_calibrationProfile.multipliers[static_cast<std::size_t>(i)]);
    }

    QJsonObject obj;
    obj.insert(QStringLiteral("valid"), true);
    obj.insert(QStringLiteral("avg"), avgArr);
    obj.insert(QStringLiteral("peak"), peakArr);
    obj.insert(QStringLiteral("multipliers"), multArr);
    obj.insert(QStringLiteral("timestamp"), QDateTime::currentDateTimeUtc().toString(Qt::ISODate));

    const QJsonDocument doc(obj);
    file.write(doc.toJson(QJsonDocument::Compact));
}

void TabEngineBridge::appendCaptureAudio(const float* const channels[6], int n) {
    if (n <= 0)
        return;
    for (int s = 0; s < 6; ++s) {
        auto& dest = m_captureBuffers[static_cast<std::size_t>(s)];
        const float* src = channels[s];
        if (src) {
            dest.insert(dest.end(), src, src + n);
        } else {
            dest.insert(dest.end(), static_cast<std::size_t>(n), 0.0f);
        }
    }
}

void TabEngineBridge::appendSessionWaveTap(const float* const channels[6], int n, float sr) {
    if (n <= 0 || sr <= 0.f)
        return;

    bool sampleRateChanged = false;
    if (m_sessionWaveTapSampleRate <= 0.f || std::fabs(m_sessionWaveTapSampleRate - sr) > 1.0e-3f) {
        m_sessionWaveTapSampleRate = sr;
        sampleRateChanged = true;
    }

    const std::size_t limitSamples = (m_sessionWaveTapSampleRate > 0.f)
        ? static_cast<std::size_t>(std::max(1.0f, m_sessionWaveTapSampleRate * kSessionWaveTapSeconds))
        : 0u;

    if (sampleRateChanged || limitSamples != m_sessionWaveTapCapacity) {
        m_sessionWaveTapCapacity = limitSamples;
        for (auto& buffer : m_sessionWaveTap)
            buffer.assign(m_sessionWaveTapCapacity, 0.f);
        m_sessionWaveTapWriteIndex.fill(0);
        m_sessionWaveTapCount.fill(0);
    }

    if (m_sessionWaveTapCapacity == 0)
        return;

    for (int s = 0; s < 6; ++s) {
        auto& tap = m_sessionWaveTap[static_cast<std::size_t>(s)];
        if (tap.size() != m_sessionWaveTapCapacity)
            tap.assign(m_sessionWaveTapCapacity, 0.f);

        const float* src = channels[s];
        std::size_t writeIndex = m_sessionWaveTapWriteIndex[static_cast<std::size_t>(s)];
        std::size_t count = m_sessionWaveTapCount[static_cast<std::size_t>(s)];

        int processed = 0;
        while (processed < n) {
            const std::size_t available = m_sessionWaveTapCapacity - writeIndex;
            const std::size_t chunk = std::min<std::size_t>(available, static_cast<std::size_t>(n - processed));
            if (chunk == 0)
                break;

            auto writeBegin = tap.begin() + static_cast<std::ptrdiff_t>(writeIndex);
            auto writeEnd = writeBegin + static_cast<std::ptrdiff_t>(chunk);
            if (src) {
                std::copy(src + processed, src + processed + static_cast<int>(chunk), writeBegin);
            } else {
                std::fill(writeBegin, writeEnd, 0.f);
            }

            writeIndex = (writeIndex + chunk) % m_sessionWaveTapCapacity;
            processed += static_cast<int>(chunk);
        }

        const std::size_t newSamples = static_cast<std::size_t>(n);
        count = std::min(m_sessionWaveTapCapacity, count + newSamples);
        m_sessionWaveTapWriteIndex[static_cast<std::size_t>(s)] = writeIndex;
        m_sessionWaveTapCount[static_cast<std::size_t>(s)] = count;
    }
    m_sessionWaveTapDirty = true;
}

void TabEngineBridge::finalizeCaptureBuffers() {
    bool hasSamples = false;
    for (int s = 0; s < 6; ++s) {
        auto& pending = m_pendingCaptureBuffers[static_cast<std::size_t>(s)];
        auto& active = m_captureBuffers[static_cast<std::size_t>(s)];
        if (!active.empty())
            hasSamples = true;
        pending.swap(active);
    }
    m_pendingSampleRate = m_captureSampleRate;
    m_pendingCaptureValid = hasSamples && m_pendingSampleRate > 0.f;
    m_pendingEventsJsonSnapshot = m_eventsJson;
    m_captureSampleRate = 0.f;
    if (!m_pendingCaptureValid)
        clearPendingCapture();
}

std::filesystem::path TabEngineBridge::sessionWaveDirectory() const {
    std::filesystem::path base = std::filesystem::current_path() / "logs";
    std::string sessionName;
    const auto& logger = SessionLogger::instance();
    const std::string logPath = logger.logFilePath();
    if (!logPath.empty()) {
        std::filesystem::path logFile(logPath);
        if (!logFile.parent_path().empty())
            base = logFile.parent_path();
        sessionName = logFile.stem().string();
    }
    if (sessionName.empty()) {
        sessionName = QStringLiteral("session-%1")
                          .arg(QDateTime::currentDateTimeUtc().toString(QStringLiteral("yyyyMMdd-HHmmss")))
                          .toStdString();
    }
    return base / "sessionwavs" / sessionName;
}

void TabEngineBridge::dumpSessionWaveSnapshot(const char* reason) {
    if (!m_sessionWaveTapDirty || m_sessionWaveTapSampleRate <= 0.f)
        return;

    const std::filesystem::path targetDir = sessionWaveDirectory();
    std::error_code ec;
    std::filesystem::create_directories(targetDir, ec);
    if (ec) {
        SessionLogger::instance().logf("sessionwavs", "failed to create %s (%d)", targetDir.string().c_str(), ec.value());
        return;
    }

    int written = 0;
    std::vector<float> scratch;
    for (int s = 0; s < 6; ++s) {
        const auto& buffer = m_sessionWaveTap[static_cast<std::size_t>(s)];
        const std::size_t capacity = buffer.size();
        const std::size_t count = (capacity > 0)
            ? std::min<std::size_t>(capacity, m_sessionWaveTapCount[static_cast<std::size_t>(s)])
            : 0u;
        if (buffer.empty() || count == 0 || capacity == 0)
            continue;

        scratch.clear();
        if (scratch.capacity() < count)
            scratch.reserve(count);

        const std::size_t writeIndex = m_sessionWaveTapWriteIndex[static_cast<std::size_t>(s)] % capacity;
        const std::size_t start = (writeIndex + capacity - count) % capacity;
        const std::size_t firstChunk = std::min<std::size_t>(count, capacity - start);
        scratch.insert(scratch.end(),
                       buffer.begin() + static_cast<std::ptrdiff_t>(start),
                       buffer.begin() + static_cast<std::ptrdiff_t>(start + firstChunk));
        if (firstChunk < count) {
            const std::size_t secondChunk = count - firstChunk;
            scratch.insert(scratch.end(),
                           buffer.begin(),
                           buffer.begin() + static_cast<std::ptrdiff_t>(secondChunk));
        }

        QString baseName = stringNoteToken(s);
        if (baseName.isEmpty())
            baseName = QStringLiteral("string%1").arg(s + 1);
        const std::filesystem::path filePath = targetDir / (baseName + QStringLiteral(".wav")).toStdString();
        if (writeWavFile(filePath, scratch, m_sessionWaveTapSampleRate))
            ++written;
    }

    if (written > 0) {
        std::string extra;
        if (reason && *reason) {
            extra = " (";
            extra += reason;
            extra += ')';
        }
        SessionLogger::instance().logf("sessionwavs",
                                       "wrote %d wav files to %s%s",
                                       written,
                                       targetDir.string().c_str(),
                                       extra.c_str());
    }

    m_sessionWaveTapDirty = false;
    m_sessionWaveTapWriteIndex.fill(0);
    m_sessionWaveTapCount.fill(0);
    for (auto& buffer : m_sessionWaveTap)
        std::fill(buffer.begin(), buffer.end(), 0.f);
}

void TabEngineBridge::clearPendingCapture() {
    for (auto& buffer : m_pendingCaptureBuffers)
        buffer.clear();
    m_pendingSampleRate = 0.f;
    m_pendingCaptureValid = false;
    m_pendingEventsJsonSnapshot.clear();
}

QString TabEngineBridge::stringNoteToken(int stringIdx) const {
    if (stringIdx < 0 || stringIdx >= static_cast<int>(m_tuning.stringMidi.size()))
        return QStringLiteral("string%1").arg(stringIdx + 1);
    static const std::array<const char*, 12> kNotes{{"C","C#","D","D#","E","F","F#","G","G#","A","A#","B"}};
    const int midi = m_tuning.stringMidi[static_cast<std::size_t>(stringIdx)];
    const int note = ((midi % 12) + 12) % 12;
    const int octave = midi / 12 - 1;
    QString base = QString::fromLatin1(kNotes[static_cast<std::size_t>(note)]);
    base.replace('#', 's');
    bool duplicate = false;
    for (int i = 0; i < static_cast<int>(m_tuning.stringMidi.size()); ++i) {
        if (i == stringIdx)
            continue;
        const int otherMidi = m_tuning.stringMidi[static_cast<std::size_t>(i)];
        const int otherNote = ((otherMidi % 12) + 12) % 12;
        if (otherNote == note) {
            duplicate = true;
            break;
        }
    }
    if (duplicate)
        return QStringLiteral("%1%2").arg(base).arg(octave);
    return base;
}

QString TabEngineBridge::sanitizeLabel(const QString& label) {
    QString trimmed = label;
    trimmed = trimmed.trimmed();
    if (trimmed.isEmpty())
        trimmed = QDateTime::currentDateTimeUtc().toString(QStringLiteral("yyyyMMdd-HHmmss"));

    QString safe;
    safe.reserve(trimmed.size());
    for (const QChar& ch : trimmed) {
        if (ch.isLetterOrNumber()) {
            safe.append(ch);
        } else if (ch.isSpace()) {
            safe.append(QLatin1Char(' '));
        } else if (ch == QLatin1Char('-') || ch == QLatin1Char('_')) {
            safe.append(ch);
        } else {
            safe.append(QLatin1Char('_'));
        }
    }

    while (safe.startsWith(QLatin1Char('_')))
        safe.remove(0, 1);
    if (safe.isEmpty())
        safe = QStringLiteral("session");
    return safe;
}

std::filesystem::path TabEngineBridge::captureRootDirectory() const {
    const QByteArray custom = qgetenv("SIGNALASSISTANT_CAPTURE_DIR");
    if (!custom.isEmpty())
        return std::filesystem::path(QString::fromUtf8(custom).toStdString());
    return std::filesystem::current_path() / "sessions" / "live";
}

bool TabEngineBridge::writeWavFile(const std::filesystem::path& path,
                                   const std::vector<float>& samples,
                                   float sampleRate) const {
    if (samples.empty() || sampleRate <= 0.f)
        return false;

    SF_INFO info {};
    info.channels = 1;
    info.samplerate = static_cast<int>(std::lround(sampleRate));
    info.format = SF_FORMAT_WAV | SF_FORMAT_FLOAT;

    SNDFILE* file = sf_open(path.string().c_str(), SFM_WRITE, &info);
    if (!file)
        return false;

    const sf_count_t written = sf_write_float(file, samples.data(), static_cast<sf_count_t>(samples.size()));
    sf_write_sync(file);
    sf_close(file);
    return written == static_cast<sf_count_t>(samples.size());
}

double TabEngineBridge::pendingCaptureDurationSec() const {
    if (m_pendingSampleRate <= 0.f)
        return 0.0;
    std::size_t maxSamples = 0;
    for (const auto& buffer : m_pendingCaptureBuffers)
        maxSamples = std::max(maxSamples, buffer.size());
    return (m_pendingSampleRate > 0.f)
        ? static_cast<double>(maxSamples) / static_cast<double>(m_pendingSampleRate)
        : 0.0;
}

void TabEngineBridge::discardPendingCapture() {
    if (!m_pendingCaptureValid)
        return;
    SessionLogger::instance().log("live-record", "pending capture discarded (user cancelled)");
    clearPendingCapture();
}

void TabEngineBridge::getCalibrationMultipliers(std::array<float, 6>& multipliers) const {
    auto& store = NoteDetectionStore::instance();
    for (int s = 0; s < 6; ++s) {
        multipliers[static_cast<std::size_t>(s)] = 
            store.activeValue(NoteParameter::CalibrationGainMultiplier, s);
    }
}

QVariantList TabEngineBridge::calibrationGains() const {
    QVariantList list;
    for (int s = 0; s < 6; ++s) {
        const float value = NoteDetectionStore::instance().currentValueFromKey("calibrationGainMultiplier", s);
        list.append(value);
    }
    return list;
}

void TabEngineBridge::setCalibrationGain(int stringIndex, double gain) {
    // Legacy method - calibration gains should only be set by calibration profile
    // This method is kept for API compatibility but does nothing
    Q_UNUSED(stringIndex);
    Q_UNUSED(gain);
}

bool TabEngineBridge::exportPendingCapture(const QString& rawLabel) {
    if (!m_pendingCaptureValid)
        return false;

    const QString safeLabel = sanitizeLabel(rawLabel);
    const QString timestamp = QDateTime::currentDateTimeUtc().toString(QStringLiteral("yyyyMMdd-HHmmss"));

    std::filesystem::path root = captureRootDirectory();
    std::error_code ec;
    std::filesystem::create_directories(root, ec);
    if (ec)
        return false;

    QString folderName = safeLabel;
    std::filesystem::path sessionDir = root / folderName.toStdString();
    int suffix = 1;
    ec.clear();
    while (std::filesystem::exists(sessionDir, ec)) {
        if (ec)
            return false;
        folderName = QStringLiteral("%1_%2").arg(safeLabel).arg(++suffix);
        sessionDir = root / folderName.toStdString();
    }

    std::filesystem::create_directories(sessionDir, ec);
    if (ec)
        return false;

    bool success = true;
    for (int s = 0; s < 6; ++s) {
        const auto& buffer = m_pendingCaptureBuffers[static_cast<std::size_t>(s)];
        if (buffer.empty())
            continue;
        QString baseName = stringNoteToken(s);
        if (baseName.isEmpty())
            baseName = QStringLiteral("string%1").arg(s + 1);
        const std::filesystem::path filePath = sessionDir / (baseName + QStringLiteral(".wav")).toStdString();
        if (!writeWavFile(filePath, buffer, m_pendingSampleRate)) {
            success = false;
            break;
        }
    }

    if (!success) {
        SessionLogger::instance().log("live-record", "failed to write WAV files");
        return false;
    }

    const QString metaPath = QString::fromStdString((sessionDir / "metadata.json").string());
    QFile metaFile(metaPath);
    if (metaFile.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        QJsonObject meta;
        meta.insert(QStringLiteral("label"), rawLabel);
        meta.insert(QStringLiteral("folder"), folderName);
        meta.insert(QStringLiteral("timestamp"), timestamp);
        meta.insert(QStringLiteral("sampleRate"), m_pendingSampleRate);
        meta.insert(QStringLiteral("durationSec"), pendingCaptureDurationSec());
        QJsonArray midiArr;
        for (int midi : m_tuning.stringMidi)
            midiArr.append(midi);
        meta.insert(QStringLiteral("stringMidi"), midiArr);
        QJsonArray stringNames;
        for (int s = 0; s < 6; ++s)
            stringNames.append(stringNoteToken(s));
        meta.insert(QStringLiteral("stringNames"), stringNames);
        metaFile.write(QJsonDocument(meta).toJson(QJsonDocument::Indented));
        metaFile.close();
    }

    const QString eventsPath = QString::fromStdString((sessionDir / "events.json").string());
    QFile eventsFile(eventsPath);
    if (eventsFile.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        eventsFile.write(m_pendingEventsJsonSnapshot.toUtf8());
        eventsFile.close();
    }

    SessionLogger::instance().logf("live-record",
                                   "saved session folder='%s' duration=%.2f",
                                   folderName.toUtf8().constData(),
                                   pendingCaptureDurationSec());

    clearPendingCapture();
    return true;
}
