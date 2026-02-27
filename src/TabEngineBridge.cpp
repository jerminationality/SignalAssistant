#include "TabEngineBridge.h"

#include "SessionLogger.h"
#include "NoteDetectionStore.h"
#include "StringTrackerParams.h"
#include "audio/HexAudioClient.h"
#include "audio/HexJackClient.h"
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
    m_rawMeters.clear();
    for (int i = 0; i < 6; ++i)
        m_rawMeters.append(0.0);
    m_thresholds.clear();
    for (int i = 0; i < 6; ++i) {
        QVariantMap stringThresholds;
        stringThresholds["onsetPeakThreshold"] = 0.0;
        stringThresholds["noiseFloor"] = 0.0;
        stringThresholds["retriggerGate"] = 0.0;
        stringThresholds["envelopePeak"] = 0.0;
        stringThresholds["exitRmsThreshold"] = 0.0;
        m_thresholds.append(stringThresholds);
    }
    
    // Register callback for direct note events from StringTracker
    m_engine->setNoteEventCallback([this](bool isNoteOn, int stringIdx, int fret, float velocity) {
        if (stringIdx < 0 || stringIdx >= 6 || fret < 0 || fret > 24)
            return;
        
        if (isNoteOn) {
            QMetaObject::invokeMethod(this,
                [this, stringIdx, fret, velocity]() {
                    emit liveNoteTriggered(stringIdx, fret, velocity);
                },
                Qt::QueuedConnection);
        } else {
            QMetaObject::invokeMethod(this,
                [this, stringIdx, fret]() {
                    emit liveNoteEnded(stringIdx, fret);
                },
                Qt::QueuedConnection);
        }
    });
    
    resetCalibrationSteps();
    m_calibrationFadeTimer = new QTimer(this);
    m_calibrationFadeTimer->setSingleShot(true);
    connect(m_calibrationFadeTimer, &QTimer::timeout, this, &TabEngineBridge::handleCalibrationFadeComplete);
    loadPersistentCalibration();
    syncFromEngine();
    updateThresholdsDisplay();  // Initialize threshold display
    emit calibrationStatusChanged();
}

void TabEngineBridge::updateThresholdsDisplay() {
    if (!m_engine)
        return;
    
    auto thresholds = m_engine->getThresholds();
    for (int i = 0; i < 6; ++i) {
        QVariantMap stringThresholds;
        stringThresholds["onsetPeakThreshold"] = thresholds[i].onsetPeakThreshold;
        stringThresholds["noiseFloor"] = thresholds[i].noiseFloor;
        stringThresholds["retriggerGate"] = thresholds[i].retriggerGate;
        stringThresholds["envelopePeak"] = thresholds[i].envelopePeak;
        stringThresholds["exitRmsThreshold"] = thresholds[i].exitRmsThreshold;
        stringThresholds["envelopeRms"] = thresholds[i].envelopeRms;
        m_thresholds[i] = stringThresholds;
    }
    emit thresholdsChanged();
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
    // Always update hex meters for both live (JACK) and recorded session playback
    QVariantList list;
    list.reserve(6);
    for (float value : meters)
        list.append(value);
    m_hexMeters = list;
    emit hexMetersChanged();

    // Check for hardware clip risk: any peak meter > 0.9
    bool clipRisk = false;
    for (float value : meters) {
        if (value > 0.9f) {
            clipRisk = true;
            break;
        }
    }
    if (clipRisk != m_hardwareClipRisk) {
        m_hardwareClipRisk = clipRisk;
        emit hardwareClipRiskChanged();
    }

    // Update raw meters (only available during live JACK sessions)
    if (!m_audioClient) {
        return;
    }
    auto* jackClient = dynamic_cast<HexJackClient*>(m_audioClient);
    if (jackClient) {
        auto rawMeters = jackClient->getRawMeters();
        QVariantList rawList;
        rawList.reserve(6);
        for (float value : rawMeters)
            rawList.append(value);
        m_rawMeters = rawList;
        emit rawMetersChanged();
    }
}

void TabEngineBridge::handleCalibrationStarted() {
    m_calibrationRunning = true;
    if (m_partialCalibration && m_requestedCalibrationString >= 0) {
        const QString label = calibrationStringName(m_requestedCalibrationString);
        m_calibrationMessage = QStringLiteral("Pluck %1 (single string)").arg(label);
    } else {
        // Full calibration: show dark-grey circles during noise capture
        m_calibrationMessage = QStringLiteral("Capturing noise floor...");
        for (int s = 0; s < 6; ++s)
            setCalibrationStepState(s, 4);
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

    // stringIndex == -1 means either:
    //   (a) the noise-floor phase just started/completed (m_activeCalibrationString still -1), or
    //   (b) the full sequence has finished (comes just before calibrationFinished signal).
    // In both cases we only need to tidy up the last active string — handleCalibrationFinished
    // will do the final state promotion for all strings.
    if (stringIndex < 0) {
        if (m_activeCalibrationString >= 0)
            setCalibrationStepState(m_activeCalibrationString, 3);
        m_activeCalibrationCapturing = false;
        // Don't touch strings that haven't been reached yet (state 0 stays dark).
        m_calibrationMessage = QStringLiteral("Finalizing calibration...");
        emit calibrationStatusChanged();
        return;
    }

    // Transitioning to a new string.
    if (stringIndex != m_activeCalibrationString) {
        if (m_activeCalibrationString < 0) {
            // Coming from noise phase — reset all indicators to default
            for (int s = 0; s < 6; ++s)
                setCalibrationStepState(s, 0);
        } else {
            // Mark the previously active string as done.
            setCalibrationStepState(m_activeCalibrationString, 3);
            // Also ensure all strings before this one are marked done in case of gaps.
            for (int s = 0; s < stringIndex; ++s) {
                if (m_calibrationStepStates[static_cast<std::size_t>(s)] > 0)
                    setCalibrationStepState(s, 3);
            }
            // Strings after the current one should be reset to pending (0 / dark)
            for (int s = stringIndex + 1; s < 6; ++s) {
                const std::size_t slot = static_cast<std::size_t>(s);
                if (m_calibrationStepStates[slot] > 0 && m_calibrationStepStates[slot] < 3)
                    setCalibrationStepState(s, 0);
            }
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
    // Only mark strings that were actually captured (avg >= 0) as done (green).
    // Strings that were never reached keep their current state (dark/0).
    for (int s = 0; s < 6; ++s) {
        const float avg = averages[static_cast<std::size_t>(s)];
        if (avg >= 0.f)
            setCalibrationStepState(s, 3);
    }
    m_activeCalibrationString = -1;
    m_activeCalibrationCapturing = false;
    m_calibrationRunning = false;
    bool anyUpdated = false;
    for (int s = 0; s < 6; ++s) {
        const float avg = averages[static_cast<std::size_t>(s)];
        const float peak = peaks[static_cast<std::size_t>(s)];
        if (avg >= 0.f && peak >= 0.f) {
            m_calibrationProfile.avgRms[static_cast<std::size_t>(s)] = avg;
            m_calibrationProfile.peakLevel[static_cast<std::size_t>(s)] = peak;
            // Peak-first: scale hottest transient to 0.8 peak
            if (peak > 0.001f) {
                m_calibrationProfile.multipliers[static_cast<std::size_t>(s)] = std::clamp(0.8f / peak, 0.2f, 8.0f);
            } else {
                m_calibrationProfile.multipliers[static_cast<std::size_t>(s)] = 1.0f;
            }
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
        
        // ── Rescale noise floor into the calibrated domain ──────────────
        // The noise floor was captured in the raw (pre-gain) domain.  The
        // engine will see input multiplied by calibrationGainMultiplier, so
        // the noise gate must be expressed in that calibrated domain.
        // Use per-string: calibratedNoise = rawNoise × multiplier.
        if (m_rawCalibrationNoiseFloor > 0.f) {
            for (int s = 0; s < 6; ++s) {
                const float mult = m_calibrationProfile.multipliers[static_cast<std::size_t>(s)];
                const float calibratedNoise = m_rawCalibrationNoiseFloor * mult;
                const float clamped = std::max(calibratedNoise, 0.0005f);
                const float ng = std::clamp(std::log(clamped / 0.0005f) / std::log(20.f), 0.0f, 1.0f);
                NoteDetectionStore::instance().setValueFromKey("noiseGate", s, ng);
                SessionLogger::instance().logf("calibration",
                    "String %d: calibrated noise floor %.6f (raw %.6f × mult %.3f) → noiseGate %.4f",
                    s + 1, calibratedNoise, m_rawCalibrationNoiseFloor, mult, ng);
            }
        }

        // Commit calibration values to tuning state
        NoteDetectionStore::instance().commit();
        
        // Log calibration data
        SessionLogger::instance().log("calibration", "=== Calibration Complete ===");
        for (int s = 0; s < 6; ++s) {
            SessionLogger::instance().logf("calibration", 
                "String %d: avgRms=%.6f peakLevel=%.6f target=0.8peak multiplier=%.3f",
                s + 1,
                m_calibrationProfile.avgRms[static_cast<std::size_t>(s)],
                m_calibrationProfile.peakLevel[static_cast<std::size_t>(s)],
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

void TabEngineBridge::handleCalibrationBaselineFloorCaptured(float noiseFloor) {
    // Store the raw noise floor.  The definitive noiseGate values will be set
    // in handleCalibrationFinished() once per-string calibration multipliers
    // are known, so that the noise gate is expressed in the calibrated domain.
    m_rawCalibrationNoiseFloor = noiseFloor;

    // Set a provisional noiseGate using the raw value so the UI updates
    // immediately.  It will be overwritten with the correct (calibrated)
    // value once the full calibration sequence completes.
    const float clamped = std::max(noiseFloor, 0.0005f);
    const float ng = std::clamp(std::log(clamped / 0.0005f) / std::log(20.f), 0.0f, 1.0f);
    for (int s = 0; s < 6; ++s) {
        NoteDetectionStore::instance().setValueFromKey("noiseGate", s, ng);
    }
    SessionLogger::instance().logf("calibration", "Baseline noise floor captured (raw): %.6f  provisional noiseGate: %.4f", noiseFloor, ng);
    // Circle state transitions are handled by handleCalibrationStepChanged
    // when the audio thread announces the transition to the first string.
}

void TabEngineBridge::handleCalibrationFadeComplete() {
    // Fade all circles to inactive state
    for (int s = 0; s < 6; ++s) {
        setCalibrationStepState(s, 0);
    }
    
    // Set first string (low E) to ready state (yellow)
    setCalibrationStepState(0, 1);
    m_calibrationMessage = QStringLiteral("Pluck low E (1/6)");
    emit calibrationStatusChanged();
}

void TabEngineBridge::requestRefresh() {
    syncFromEngine();
}

void TabEngineBridge::clear() {
    if (m_engine) {
        m_engine->importEvents({});
    }
    m_liveTimeSec = 0.f;
    m_liveSampleRate = 0.f;
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

void TabEngineBridge::cancelCalibration() {
    if (!m_calibrationRunning)
        return;
    if (m_audioClient)
        m_audioClient->cancelCalibration();
    // Reset display state immediately on the Qt thread.
    m_calibrationRunning = false;
    m_activeCalibrationString = -1;
    m_activeCalibrationCapturing = false;
    m_partialCalibration = false;
    m_requestedCalibrationString = -1;
    resetCalibrationSteps();
    m_calibrationMessage = QStringLiteral("Calibration cancelled");
    emit calibrationStatusChanged();
}

void TabEngineBridge::setAudioClient(HexAudioClient* client) {    if (m_audioClient == client)
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
        updateThresholdsDisplay();  // Update thresholds when audio client connected
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
        m_lastLiveTriggerSec.fill(-1.f);
        m_lastLiveFret.fill(-1);
        reset = true;
        if (m_debugNoteLogging)
            qInfo() << "TabBridge" << "engine-reset" << "sr" << sr << "capturing" << capturing;
    }

    if (!capturing && reset) {
        m_liveTimeSec = 0.f;
    }

    if (capturing) {
        if (m_captureSampleRate <= 0.f || std::fabs(m_captureSampleRate - sr) > 1e-3f)
            m_captureSampleRate = sr;
        appendCaptureAudio(channels, n);
    }

    // NOTE: meter snapshot is handled by HexJackClient::emitMeters() via MeterPump
    // at 40ms intervals from the Qt thread. Calling postMeterSnapshot() here was
    // redundant and caused QMetaObject heap allocations on every RT callback → XRuns.

    if (m_debugNoteLogging) {
        // Debug-only: compute block RMS and log (disabled in normal operation)
        std::array<float, 6> blockRms {};
        for (int i = 0; i < 6; ++i) {
            const float* data = channels[static_cast<std::size_t>(i)];
            if (!data) continue;
            double sum = 0.0;
            for (int sample = 0; sample < n; ++sample) {
                const double value = static_cast<double>(data[sample]);
                sum += value * value;
            }
            blockRms[static_cast<std::size_t>(i)] = static_cast<float>(std::sqrt(sum / static_cast<double>(n)));
        }
        QStringList rmsSummary;
        for (int i = 0; i < 6; ++i)
            rmsSummary << QStringLiteral("s%1=%2").arg(i + 1).arg(blockRms[static_cast<std::size_t>(i)], 0, 'f', 5);
        qInfo() << "TabBridge" << "block-rms" << rmsSummary.join(' ');
    }

    // Process audio through engine - note events are handled via callback
    const float blockStart = m_liveTimeSec;
    m_engine->processBlock(channels, n, sr, blockStart);

    // Schedule tuning deviation + threshold updates on the Qt thread.
    // Calling updateTuningDeviation() / emit thresholdsChanged() directly from
    // the JACK RT thread is not safe — those functions touch Qt containers and
    // emit signals which do heap allocation and locking.
    const bool thresholdsDue = !m_thresholdsUpdateTimer.isValid() || m_thresholdsUpdateTimer.elapsed() >= 100;
    if (thresholdsDue)
        m_thresholdsUpdateTimer.restart();

    QMetaObject::invokeMethod(this, [this, thresholdsDue]() {
        updateTuningDeviation();
        if (thresholdsDue)
            updateThresholdsDisplay();
    }, Qt::QueuedConnection);
    
    m_liveTimeSec += static_cast<float>(n) / sr;

    // Limit event list growth when not capturing
    if (!capturing) {
        const auto& events = m_engine->events();
        const int total = static_cast<int>(events.size());
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

    updateThresholdsDisplay();  // Update thresholds when syncing from engine

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
        map.insert(QStringLiteral("peakLevel"), ev.peakLevel);
        {
            const char* stateStr = "unknown";
            switch (ev.state) {
                case NoteEvent::AnalysisState::PENDING_ANALYSIS: stateStr = "pending"; break;
                case NoteEvent::AnalysisState::CONFIRMED:        stateStr = "confirmed"; break;
                case NoteEvent::AnalysisState::CLOSED:           stateStr = "closed"; break;
            }
            map.insert(QStringLiteral("state"), QString::fromLatin1(stateStr));
        }
        map.insert(QStringLiteral("articulation"), QString());
        list.push_back(map);
    }

    m_events = list;
    const QJsonDocument doc = QJsonDocument::fromVariant(list);
    m_eventsJson = QString::fromUtf8(doc.toJson(QJsonDocument::Compact));
    emit eventsChanged();
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
    // Derive calibration profile from tuning state
    for (int i = 0; i < 6; ++i) {
        m_calibrationProfile.multipliers[static_cast<std::size_t>(i)] = 
            NoteDetectionStore::instance().committedValueFromKey("calibrationGainMultiplier", i);
    }
    
    // Try to load avgRms and peakLevel from legacy calibration file if it exists
    const QString path = calibrationStoragePath();
    if (!path.isEmpty()) {
        QFile file(path);
        if (file.exists() && file.open(QIODevice::ReadOnly)) {
            const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
            if (doc.isObject()) {
                const QJsonObject obj = doc.object();
                const QJsonArray avgArr = obj.value(QStringLiteral("avg")).toArray();
                const QJsonArray peakArr = obj.value(QStringLiteral("peak")).toArray();
                if (avgArr.size() == 6 && peakArr.size() == 6) {
                    for (int i = 0; i < 6; ++i) {
                        m_calibrationProfile.avgRms[static_cast<std::size_t>(i)] = static_cast<float>(avgArr[i].toDouble());
                        m_calibrationProfile.peakLevel[static_cast<std::size_t>(i)] = static_cast<float>(peakArr[i].toDouble());
                    }
                    SessionLogger::instance().log("calibration", "Loaded avgRms/peakLevel from legacy calibration file");
                }
            }
        }
    }
    
    m_calibrationProfile.valid = true;

    m_calibrationLoaded = true;
    m_calibrationMessage = QStringLiteral("Calibration loaded from tuning state");
    SessionLogger::instance().log("calibration", "Calibration profile derived from tuning state");
}

void TabEngineBridge::updateCalibrationMultipliers() {
    // Sync multipliers from NoteDetectionStore to calibration profile for engine
    bool anyChanged = false;
    for (int i = 0; i < 6; ++i) {
        const float newMult = NoteDetectionStore::instance().currentValueFromKey("calibrationGainMultiplier", i);
        if (!m_calibrationProfile.valid || m_calibrationProfile.multipliers[static_cast<std::size_t>(i)] != newMult) {
            m_calibrationProfile.multipliers[static_cast<std::size_t>(i)] = newMult;
            anyChanged = true;
        }
    }
    
    if (anyChanged) {
        m_calibrationProfile.valid = true;
        
        SessionLogger::instance().log("calibration", "Calibration multipliers synced");
    }
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
