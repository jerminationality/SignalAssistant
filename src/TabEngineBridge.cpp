#include "TabEngineBridge.h"

#include "SessionLogger.h"
#include "HeatmapLogger.h"
#include "NoteDetectionStore.h"
#include "NoteLogger.h"

// Enable detailed note event logging for debugging note-off issues
#define NOTE_DEBUG 0
#include "audio/HexAudioClient.h"
#include "audio/HexJackClient.h"
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QVariantMap>
#include <QColor>
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

// ARM NEON SIMD for log-scale magnitude conversion
#ifdef __ARM_NEON
#include <arm_neon.h>
#define USE_NEON_LOG_SCALE 1
#else
#define USE_NEON_LOG_SCALE 0
#endif

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
    // Set note state reference for TabEngine to consume YIN results
    m_engine->setNoteState(&m_atomicNoteState);
    
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
        stringThresholds["onsetThreshold"] = 0.0;
        stringThresholds["gateThreshold"] = 0.0;
        stringThresholds["envFloor"] = 0.0;
        stringThresholds["sustainFloor"] = 0.0;
        stringThresholds["retriggerGate"] = 0.0;
        m_thresholds.append(stringThresholds);
    }
    resetCalibrationSteps();
    m_calibrationFadeTimer = new QTimer(this);
    m_calibrationFadeTimer->setSingleShot(true);
    connect(m_calibrationFadeTimer, &QTimer::timeout, this, &TabEngineBridge::handleCalibrationFadeComplete);
    
    // Timer for UI note state updates - runs at 10Hz from main thread to avoid xruns
    m_noteStatePollTimer = new QTimer(this);
    m_noteStatePollTimer->setInterval(100); // 10Hz
    connect(m_noteStatePollTimer, &QTimer::timeout, this, &TabEngineBridge::updateNoteStateCache);
    m_noteStatePollTimer->start();
    
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
        stringThresholds["onsetThreshold"] = thresholds[i].onsetThreshold;
        stringThresholds["baseline"] = thresholds[i].baseline;
        stringThresholds["gateThreshold"] = thresholds[i].gateThreshold;
        stringThresholds["envFloor"] = thresholds[i].envFloor;
        stringThresholds["sustainFloor"] = thresholds[i].sustainFloor;
        stringThresholds["retriggerGate"] = thresholds[i].retriggerGate;
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

qreal TabEngineBridge::getBinMagnitude(int stringIndex, int fretIndex) const {
    // O(1) lookup for QML colorProvider - called 150 times per render but very cheap
    if (stringIndex < 0 || stringIndex >= 6) return 0.0;
    if (fretIndex < 0 || fretIndex > 24) return 0.0;
    return static_cast<qreal>(m_noteStateCache[stringIndex][fretIndex]);
}

qreal TabEngineBridge::getBinThreshold(int stringIndex, int fretIndex) const {
    // O(1) lookup for QML colorProvider - threshold for this string/fret combo
    if (stringIndex < 0 || stringIndex >= 6) return 0.0;
    if (fretIndex < 0 || fretIndex > 24) return 0.0;
    return static_cast<qreal>(m_binThresholdCache[stringIndex][fretIndex]);
}

void TabEngineBridge::updateNoteStateCache() {
    // NOTE STATE CACHE UPDATE
    // Reads note state from AtomicNoteState (populated by YIN Worker)
    // Uses magnitude gate to prevent UI signal flooding on Pi5
    
    // FRAME COUNTER CHECK: Skip if no new YIN frame since last poll
    const std::uint64_t currentFrame = m_atomicNoteState.frameCounter();
    
    if (currentFrame == m_lastFrameCount) {
        return;  // No new data from YIN worker
    }
    m_lastFrameCount = currentFrame;
    
    static constexpr float kMagnitudeGateThreshold = 0.005f;  // 0.5% intensity gate
    
    bool anySignificantChange = false;
    
    // Get current YIN threshold for threshold calculation
    auto& store = NoteDetectionStore::instance();
    const auto& np = store.current();
    
    // Update live onset thresholds from AtomicNoteState (per-string adaptive values)
    bool thresholdsChanged = false;
    for (int s = 0; s < 6; ++s) {
        const float liveOnsetThreshold = m_atomicNoteState.onsetThreshold(s);
        
        // Update the thresholds display with live adaptive threshold
        if (s < m_thresholds.size()) {
            QVariantMap stringThresholds = m_thresholds[s].toMap();
            const float oldEnv = stringThresholds["envFloor"].toFloat();
            if (std::abs(liveOnsetThreshold - oldEnv) > 0.0001f) {
                stringThresholds["envFloor"] = liveOnsetThreshold;
                m_thresholds[s] = stringThresholds;
                thresholdsChanged = true;
            }
        }
        
        const float envFloor = np.yinThreshold[s];
        
        for (int f = 0; f <= 24; ++f) {
            // Read note energy from atomic state (populated by YIN Worker)
            const float liveVal = m_atomicNoteState.binMagnitude(s, f);
            float& cached = m_noteStateCache[s][f];
            
            // MAGNITUDE GATE: Only count as change if delta > 0.5% intensity
            // This prevents signal flooding that causes Pi 5 freezes
            if (std::abs(liveVal - cached) > kMagnitudeGateThreshold) {
                anySignificantChange = true;
            }
            cached = liveVal;
            
            // Update threshold cache (threshold = envFloor for YIN)
            // YIN uses uniform threshold across frets (no frequency-dependent scaling)
            // Ensure minimum threshold to prevent division by zero in QML
            m_binThresholdCache[s][f] = std::max(envFloor, 0.001f);
        }
    }
    
    // Emit thresholds changed signal if any live onset thresholds changed
    if (thresholdsChanged) {
        emit this->thresholdsChanged();
    }
    
    // Log magnitude update to heatmap logger
    HeatmapLogger::instance().logMagnitudeUpdate(currentFrame, m_noteStateCache, m_binThresholdCache);
    
    // GOAL 1: Emit single batched signal with all 150 bin colors (prevents UI halting)
    // Compute NEON-optimized log-scale colors and emit once per 100ms cycle
    QVariantMap batchUpdates;
    computeBatchedColors(batchUpdates);
    
    m_noteStateRevision++;
    emit noteStateChanged();  // Signal for revision tracking
    emit binColorBatchChanged(batchUpdates);  // New batched signal with pre-computed colors
}

// ============================================================================
// GOAL 2: NEON-Optimized Log-Scale Magnitude to Color Conversion
// ============================================================================
// Converts linear magnitudes [0.0-1.0] to dB scale, then to alpha [0.0-1.0]
// Formula: db = 20 * log10(max(mag, 0.001)), alpha = clamp((db + 60) / 60, 0, 1)
// This ensures low-frequency fundamentals (82Hz Low E) are visible in heatmap

#if USE_NEON_LOG_SCALE
namespace {
// Fast log10 approximation using NEON (accuracy ~0.1%)
// Based on: log10(x) = log2(x) / log2(10) ≈ log2(x) * 0.30103f
inline float32x4_t neon_log10_approx(float32x4_t x) {
    // Clamp to minimum to avoid log(0)
    const float32x4_t minVal = vdupq_n_f32(0.001f);
    x = vmaxq_f32(x, minVal);
    
    // Extract exponent and mantissa for log2 approximation
    // log2(x) ≈ exponent + log2(mantissa), mantissa in [1,2)
    int32x4_t xi = vreinterpretq_s32_f32(x);
    int32x4_t exp = vsubq_s32(vshrq_n_s32(xi, 23), vdupq_n_s32(127));
    float32x4_t expF = vcvtq_f32_s32(exp);
    
    // Mantissa: clear exponent bits, set to 1.0 range
    int32x4_t mantissa = vorrq_s32(vandq_s32(xi, vdupq_n_s32(0x007FFFFF)), vdupq_n_s32(0x3F800000));
    float32x4_t m = vreinterpretq_f32_s32(mantissa);
    
    // Polynomial approximation for log2(m) where m in [1,2)
    // log2(m) ≈ -1.725 + m*(2.008 + m*(-0.718 + m*0.108)) [minimax]
    const float32x4_t c0 = vdupq_n_f32(-1.725f);
    const float32x4_t c1 = vdupq_n_f32(2.008f);
    const float32x4_t c2 = vdupq_n_f32(-0.718f);
    const float32x4_t c3 = vdupq_n_f32(0.108f);
    
    float32x4_t log2m = vmlaq_f32(c2, m, c3);  // c2 + m*c3
    log2m = vmlaq_f32(c1, m, log2m);            // c1 + m*(c2 + m*c3)
    log2m = vmlaq_f32(c0, m, log2m);            // c0 + m*(c1 + ...)
    
    // log2(x) = exponent + log2(mantissa)
    float32x4_t log2x = vaddq_f32(expF, log2m);
    
    // log10(x) = log2(x) * log10(2) ≈ log2(x) * 0.30103f
    const float32x4_t log10_2 = vdupq_n_f32(0.30103f);
    return vmulq_f32(log2x, log10_2);
}
} // namespace
#endif

void TabEngineBridge::computeBatchedColors(QVariantMap& batchOut) {
    // Process all 150 bins and convert magnitudes to QColor with log-scaled alpha
    // Uses NEON SIMD to process 4 bins at a time on ARM platforms
    
#if USE_NEON_LOG_SCALE
    // NEON path: process 4 magnitudes at a time
    alignas(16) float magBuffer[4];
    alignas(16) float alphaBuffer[4];
    
    const float32x4_t twenty = vdupq_n_f32(20.0f);
    const float32x4_t sixty = vdupq_n_f32(60.0f);
    const float32x4_t zero = vdupq_n_f32(0.0f);
    const float32x4_t one = vdupq_n_f32(1.0f);
    
    for (int s = 0; s < 6; ++s) {
        for (int f = 0; f < 25; f += 4) {
            // Load 4 magnitudes (handle tail case)
            const int remaining = std::min(4, 25 - f);
            for (int i = 0; i < 4; ++i) {
                magBuffer[i] = (f + i < 25) ? m_noteStateCache[s][f + i] : 0.0f;
            }
            
            // Load into NEON register
            float32x4_t mag = vld1q_f32(magBuffer);
            
            // db = 20 * log10(max(mag, 0.001))
            float32x4_t db = vmulq_f32(twenty, neon_log10_approx(mag));
            
            // alpha = clamp((db + 60) / 60, 0, 1)
            float32x4_t alpha = vdivq_f32(vaddq_f32(db, sixty), sixty);
            alpha = vmaxq_f32(alpha, zero);
            alpha = vminq_f32(alpha, one);
            
            // Store results
            vst1q_f32(alphaBuffer, alpha);
            
            // Create QColor entries for each bin
            for (int i = 0; i < remaining; ++i) {
                const int fret = f + i;
                const float threshold = m_binThresholdCache[s][fret];
                const float rawMag = m_noteStateCache[s][fret];
                const bool aboveThreshold = rawMag >= threshold;
                
                // Color: cyan for above threshold, blue-gray for below (ghosting)
                QColor color;
                if (aboveThreshold) {
                    // Bright cyan with log-scaled alpha
                    color = QColor::fromRgbF(0.0, 0.8, 1.0, alphaBuffer[i]);
                } else {
                    // Ghosted: 50% alpha reduction
                    color = QColor::fromRgbF(0.2, 0.4, 0.6, alphaBuffer[i] * 0.5);
                }
                
                // Key format: "bin_s{1-6}_f{0-24}"
                QString key = QStringLiteral("bin_s%1_f%2").arg(s + 1).arg(fret);
                batchOut[key] = color;
            }
        }
    }
    
#else
    // Scalar fallback for non-ARM platforms
    for (int s = 0; s < 6; ++s) {
        for (int f = 0; f < 25; ++f) {
            const float rawMag = m_noteStateCache[s][f];
            const float threshold = m_binThresholdCache[s][f];
            const bool aboveThreshold = rawMag >= threshold;
            
            // Log-scale conversion: db = 20 * log10(max(mag, 0.001))
            const float clampedMag = std::max(rawMag, 0.001f);
            const float db = 20.0f * std::log10(clampedMag);
            
            // Normalize to [0,1]: alpha = clamp((db + 60) / 60, 0, 1)
            float alpha = (db + 60.0f) / 60.0f;
            alpha = std::clamp(alpha, 0.0f, 1.0f);
            
            QColor color;
            if (aboveThreshold) {
                color = QColor::fromRgbF(0.0, 0.8, 1.0, alpha);
            } else {
                color = QColor::fromRgbF(0.2, 0.4, 0.6, alpha * 0.5);
            }
            
            QString key = QStringLiteral("bin_s%1_f%2").arg(s + 1).arg(f);
            batchOut[key] = color;
        }
    }
#endif
}

void TabEngineBridge::setTuningModeEnabled(bool enabled) {
    if (m_tuningModeEnabled == enabled)
        return;
    m_tuningModeEnabled = enabled;
    emit tuningModeEnabledChanged();
}

TabEngineBridge::~TabEngineBridge() {
    // Stop YIN worker thread before destruction
    if (m_yinWorker) {
        m_yinWorker->stop();
        m_yinWorker.reset();
    }
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
    } else if (m_activeCalibrationString < 0) {
        m_calibrationMessage = QStringLiteral("Calibrating... follow string prompts");
    }
    emit calibrationStatusChanged();
}

void TabEngineBridge::handleCalibrationStepChanged(int stringIndex, bool capturing) {
    if (!m_calibrationRunning)
        return;

    // Handle noise floor phase (stringIndex == -1 with capturing == true)
    if (stringIndex == -1 && capturing) {
        // Noise floor capture starting - show all circles as grey (state 4)
        for (int s = 0; s < 6; ++s) {
            setCalibrationStepState(s, 4);
        }
        m_calibrationMessage = QStringLiteral("Capturing noise floor...");
        emit calibrationStatusChanged();
        return;
    }
    
    // Handle transition from noise floor to first string - clear grey circles
    if (m_activeCalibrationString == -1 && stringIndex >= 0) {
        for (int s = 0; s < 6; ++s) {
            setCalibrationStepState(s, 0);
        }
    }

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
    
    // First pass: calculate preAmpGain (multipliers) and find max avgRms for spatialWeight
    float maxAvgRms = 0.f;
    for (int s = 0; s < 6; ++s) {
        const float avg = averages[static_cast<std::size_t>(s)];
        const float peak = peaks[static_cast<std::size_t>(s)];
        if (avg >= 0.f && peak >= 0.f) {
            m_calibrationProfile.avgRms[static_cast<std::size_t>(s)] = avg;
            m_calibrationProfile.peakRms[static_cast<std::size_t>(s)] = peak;
            // Calculate preAmpGain: targetRMS / avgInputRMS
            const float targetRms = NoteDetectionStore::instance().currentValueFromKey("targetRms", s);
            const float multiplier = (avg > 0.f) ? (targetRms / avg) : 1.0f;
            m_calibrationProfile.multipliers[static_cast<std::size_t>(s)] = std::clamp(multiplier, 0.2f, 8.0f);
            maxAvgRms = std::max(maxAvgRms, avg);
            anyUpdated = true;
        }
    }

    if (anyUpdated) {
        // Store preAmpGain (via calibrationGainMultiplier key for now)
        for (int s = 0; s < 6; ++s) {
            NoteDetectionStore::instance().setValueFromKey("calibrationGainMultiplier", s, 
                                                          m_calibrationProfile.multipliers[static_cast<std::size_t>(s)]);
        }
        
        // Calculate and store spatialWeight: maxAvgRms / thisStringAvgRms
        // This normalizes all strings to the loudest string's output level
        // spatialWeight is fixed at calibration time and not user-adjustable
        if (maxAvgRms > 0.f && !m_partialCalibration) {
            for (int s = 0; s < 6; ++s) {
                const float avg = m_calibrationProfile.avgRms[static_cast<std::size_t>(s)];
                const float weight = (avg > 0.f) ? (maxAvgRms / avg) : 1.0f;
                m_calibrationProfile.spatialWeight[static_cast<std::size_t>(s)] = std::clamp(weight, 0.5f, 2.0f);
                NoteDetectionStore::instance().setValueFromKey("spatialWeight", s, 
                                                              m_calibrationProfile.spatialWeight[static_cast<std::size_t>(s)]);
            }
        }
        
        m_calibrationProfile.valid = true;
        if (m_engine)
            m_engine->applyCalibration(m_calibrationProfile);
        
        // Update YIN Worker Thread with new calibration data
        if (m_yinWorker) {
            m_yinWorker->setCalibration(m_calibrationProfile.avgRms, 
                                        m_calibrationProfile.peakRms);
        }
        
        // Commit calibration values to tuning state
        NoteDetectionStore::instance().commit();
        
        // Notify UI of changed calibration gains (queued to ensure values are accessible)
        QMetaObject::invokeMethod(this, [this]() {
            emit calibrationGainsChanged();
            emit calibrationParametersUpdated();
        }, Qt::QueuedConnection);
        
        // Log calibration data
        SessionLogger::instance().log("calibration", "=== Calibration Complete ===");
        for (int s = 0; s < 6; ++s) {
            const float targetRms = NoteDetectionStore::instance().currentValueFromKey("targetRms", s);
            SessionLogger::instance().logf("calibration", 
                "String %d: avgRms=%.6f peakRms=%.6f targetRms=%.6f preAmpGain=%.3f spatialWeight=%.3f",
                s + 1,
                m_calibrationProfile.avgRms[static_cast<std::size_t>(s)],
                m_calibrationProfile.peakRms[static_cast<std::size_t>(s)],
                targetRms,
                m_calibrationProfile.multipliers[static_cast<std::size_t>(s)],
                m_calibrationProfile.spatialWeight[static_cast<std::size_t>(s)]);
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
    // Update baselineFloor parameter for all strings in tuning panel
    for (int s = 0; s < 6; ++s) {
        NoteDetectionStore::instance().setValueFromKey("baselineFloor", s, noiseFloor);
    }
    // Commit baseline floor to tuning state
    NoteDetectionStore::instance().commit();
    SessionLogger::instance().logf("calibration", "Baseline noise floor captured: %.6f", noiseFloor);
    
    // Set all circles to fade state (medium-dark grey)
    for (int s = 0; s < 6; ++s) {
        setCalibrationStepState(s, 4);
    }
    m_calibrationMessage = QStringLiteral("Noise floor captured");
    emit calibrationStatusChanged();
    
    // Start 2-second fade timer
    m_calibrationFadeTimer->start(2000);
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
        const std::size_t reserveCap = (m_captureSampleRate > 1.0f)
            ? static_cast<std::size_t>(std::max(1.0f, m_captureSampleRate * kSessionWaveTapSeconds))
            : static_cast<std::size_t>(44100 * kSessionWaveTapSeconds);
        for (auto& buffer : m_captureBuffers) {
            buffer.clear();
            buffer.reserve(reserveCap);
        }
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

    // Stop existing YIN worker if any
    if (m_yinWorker) {
        m_yinWorker->stop();
        m_yinWorker.reset();
    }

    if (m_audioClient) {
        m_audioClient->setTabBridge(nullptr);
        // Disconnect from previous client's signals if it's a HexJackClient
        auto* prevJackClient = dynamic_cast<HexJackClient*>(m_audioClient);
        if (prevJackClient) {
            disconnect(prevJackClient, nullptr, this, nullptr);
        }
    }

    m_audioClient = client;
    m_externalMetersActive = (m_audioClient != nullptr);

    if (m_audioClient) {
        m_audioClient->setTabBridge(this);
        m_audioClient->connectMeters(this);
        m_audioClient->connectCalibration(this);
        updateThresholdsDisplay();  // Update thresholds when audio client connected
        
        // Connect to bufferConfigChanged to initialize YIN worker once JACK is started
        auto* jackClient = dynamic_cast<HexJackClient*>(m_audioClient);
        if (jackClient) {
            // Try to create YIN worker now if sample rate is already valid
            if (jackClient->sampleRate() > 0) {
                initYINWorkerForJack(jackClient);
            } else {
                // Sample rate not yet available - connect to signal for deferred init
                // (The !m_yinWorker check prevents multiple initializations)
                connect(jackClient, &HexJackClient::bufferConfigChanged,
                        this, [this, jackClient](int sampleRate, int /*bufferSize*/) {
                    if (sampleRate > 0 && !m_yinWorker) {
                        initYINWorkerForJack(jackClient);
                    }
                });
                qInfo() << "TabEngineBridge: JACK sample rate not yet available, deferring YIN worker init";
            }
        } else {
            qWarning() << "TabEngineBridge: setAudioClient called but dynamic_cast to HexJackClient FAILED";
        }
    }
}

void TabEngineBridge::initYINForRecordedSession(float sampleRate) {
    // Stop existing YIN worker if any
    if (m_yinWorker) {
        m_yinWorker->stop();
        m_yinWorker.reset();
    }
    
    // Create standalone ring buffer for recorded session audio
    if (!m_standaloneRingBuffer) {
        m_standaloneRingBuffer = std::make_unique<audio::AudioRingBuffer>();
    }
    
    // Create YIN Worker Thread (TIER 2) for recorded session
    audio::YINWorkerConfig workerConfig;
    workerConfig.sampleRate = sampleRate;
    workerConfig.hopSize = 256;  // ~5.3ms at 48kHz (faster response than CQT)
    workerConfig.enableCoreAffinity = false;  // Don't pin cores in recorded mode
    
    m_yinWorker = std::make_unique<audio::YINWorkerThread>(
        *m_standaloneRingBuffer,
        m_atomicNoteState,
        workerConfig
    );
    
    // Pass calibration data to worker
    if (m_calibrationProfile.valid) {
        m_yinWorker->setCalibration(m_calibrationProfile.avgRms, 
                                    m_calibrationProfile.peakRms);
    }
    
    m_yinWorker->start();
    qInfo() << "TabEngineBridge: YIN Worker Thread started for RECORDED SESSION (sample rate:" << sampleRate << ")";
}

void TabEngineBridge::initYINWorkerForJack(HexJackClient* jackClient) {
    if (!jackClient) return;
    
    // Stop existing YIN worker if any
    if (m_yinWorker) {
        m_yinWorker->stop();
        m_yinWorker.reset();
    }
    
    audio::YINWorkerConfig workerConfig;
    workerConfig.sampleRate = static_cast<float>(jackClient->sampleRate());
    workerConfig.hopSize = 256;  // ~5.3ms at 48kHz (faster response than CQT)
    workerConfig.enableCoreAffinity = true;
    workerConfig.coreId = 1;  // Core 1 for YIN processing
    
    qInfo() << "TabEngineBridge: Creating YIN Worker with ring buffer from JACK client, sampleRate:" << workerConfig.sampleRate;
    
    m_yinWorker = std::make_unique<audio::YINWorkerThread>(
        jackClient->audioRingBuffer(),
        m_atomicNoteState,
        workerConfig
    );
    
    // Pass calibration data to worker
    if (m_calibrationProfile.valid) {
        m_yinWorker->setCalibration(m_calibrationProfile.avgRms, 
                                    m_calibrationProfile.peakRms);
    }
    
    m_yinWorker->start();
    qInfo() << "TabEngineBridge: YIN Worker Thread started (TIER 2) with sample rate:" << workerConfig.sampleRate;
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
        m_activeNoteDisplayed.fill(false);
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

    // Always update meters when processing audio blocks
    // This ensures both live (JACK) and recorded session playback show meter activity
    postMeterSnapshot(blockRms);

    // =========================================================================
    // RECORDED SESSION MODE: Push audio to standalone ring buffer for YIN worker
    // This replicates what HexJackClient does in live mode, but for recorded sessions
    // =========================================================================
    if (m_standaloneRingBuffer && m_yinWorker) {
        audio::AudioFrame frame;
        for (int i = 0; i < n; ++i) {
            for (int s = 0; s < 6; ++s) {
                frame.samples[s] = channels[s] ? channels[s][i] : 0.0f;
            }
            // Push to ring buffer - if full, we drop samples
            m_standaloneRingBuffer->push(frame);
        }
        // Notify YIN worker thread that audio is available
        m_yinWorker->notifyAudioAvailable();
    }

    if (m_debugNoteLogging) {
        QStringList rmsSummary;
        for (int i = 0; i < 6; ++i)
            rmsSummary << QStringLiteral("s%1=%2").arg(i + 1).arg(blockRms[static_cast<std::size_t>(i)], 0, 'f', 5);
        qInfo() << "TabBridge" << "block-rms" << rmsSummary.join(' ');
    }

    const float blockStart = m_liveTimeSec;
    m_engine->processBlock(channels, n, sr, blockStart);
    updateTuningDeviation();
    
    // Update thresholds and bin magnitudes (throttled to 100ms to avoid overwhelming QML)
    if (!m_thresholdsUpdateTimer.isValid() || m_thresholdsUpdateTimer.elapsed() >= 100) {
        auto thresholds = m_engine->getThresholds();
        for (int i = 0; i < 6; ++i) {
            QVariantMap stringThresholds;
            stringThresholds["onsetThreshold"] = thresholds[i].onsetThreshold;
            stringThresholds["baseline"] = thresholds[i].baseline;
            stringThresholds["gateThreshold"] = thresholds[i].gateThreshold;
            stringThresholds["envFloor"] = thresholds[i].envFloor;
            stringThresholds["sustainFloor"] = thresholds[i].sustainFloor;
            stringThresholds["retriggerGate"] = thresholds[i].retriggerGate;
            m_thresholds[i] = stringThresholds;
        }
        emit thresholdsChanged();
        
        // NOTE: Note state cache is updated by m_noteStatePollTimer (UI thread)
        // Do NOT call updateNoteStateCache() here - it emits signals from audio thread and causes xruns
        
        m_thresholdsUpdateTimer.restart();
    }
    
    m_liveTimeSec += static_cast<float>(n) / sr;

    const auto& events = m_engine->events();
    const int total = static_cast<int>(events.size());
    int last = m_lastDispatchedEvent.load(std::memory_order_acquire);
    
    std::vector<LiveEvent> newEvents;
    if (total > last) {
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
            // Mark this note as displayed immediately
            m_activeNoteDisplayed[std::size_t(ev.stringIdx)] = true;
            if (m_debugNoteLogging) {
                qInfo() << "TabBridge" << "note"
                        << "string" << ev.stringIdx
                        << "fret" << ev.fret
                        << "velocity" << QString::number(ev.velocity, 'f', 3)
                        << "start" << QString::number(ev.startSec, 'f', 3);
            }
        }

        m_lastDispatchedEvent.store(total, std::memory_order_release);
    }

    // Check for notes that have ended and emit liveNoteEnded signal
    // Also emit envelope updates for active notes to drive visual feedback
    // Skip entirely if no notes are active to avoid unnecessary processing
    bool anyNotesDisplayed = false;
    for (int s = 0; s < 6; ++s) {
        if (m_activeNoteDisplayed[std::size_t(s)]) {
            anyNotesDisplayed = true;
            break;
        }
    }
    
    if (anyNotesDisplayed) {
        for (int s = 0; s < 6; ++s) {
            // Only check if we have a displayed note on this string
            if (!m_activeNoteDisplayed[std::size_t(s)])
                continue;
            
        bool hasActiveNote = false;
        float activeEnvelope = 0.f;
        int mostRecentFret = -1;
        float mostRecentEndSec = -1.f;
        float mostRecentStartSec = -1.f;
        
        // Find the most recent note for this string
        for (int i = total - 1; i >= 0; --i) {
            const auto& ev = events[std::size_t(i)];
            if (ev.stringIdx == s) {
                // Note is active if:
                // 1. endSec is 0 (not yet ended - still sustaining), OR
                // 2. endSec is ahead of current time (still within sustain window)
                const bool noteStillActive = (ev.endSec <= ev.startSec) || (ev.endSec > m_liveTimeSec);
                mostRecentFret = ev.fret;
                mostRecentEndSec = ev.endSec;
                mostRecentStartSec = ev.startSec;
                
                if (noteStillActive) {
                    hasActiveNote = true;
                    activeEnvelope = ev.velocity; // velocity is updated from envelope
                }
                break; // Only check most recent note for this string
            }
        }
        
#if NOTE_DEBUG
        // Log detailed note state for debugging
        SessionLogger::instance().logf("note-state",
            "S%d: displayed=%d hasActive=%d mostRecentFret=%d start=%.3f end=%.3f liveT=%.3f lastFret=%d",
            s, m_activeNoteDisplayed[std::size_t(s)] ? 1 : 0, hasActiveNote ? 1 : 0,
            mostRecentFret, mostRecentStartSec, mostRecentEndSec, m_liveTimeSec,
            m_lastLiveFret[std::size_t(s)]);
#endif
        
        // If we have an active note, emit envelope update
        if (hasActiveNote) {
            QMetaObject::invokeMethod(this,
                [this, s, activeEnvelope]() { emit liveNoteEnvelopeUpdated(s, activeEnvelope); },
                Qt::QueuedConnection);
        }
        
        // If we had a displayed note and now it's not active, emit noteEnded
        // Use mostRecentFret from event data instead of m_lastLiveFret which may be stale
        // (m_lastLiveFret is only updated in dispatchLiveEvents which is queued)
        if (!hasActiveNote) {
            // Prefer mostRecentFret from event scan; fallback to m_lastLiveFret for edge cases
            const int endedFret = (mostRecentFret >= 0) ? mostRecentFret : m_lastLiveFret[std::size_t(s)];
            if (endedFret >= 0) {
                qInfo() << "[UI-NOTE-OFF] S" << s << "F" << endedFret
                        << "endSec=" << QString::number(mostRecentEndSec, 'f', 3)
                        << "liveT=" << QString::number(m_liveTimeSec, 'f', 3);
#if NOTE_DEBUG
                SessionLogger::instance().logf("note-off-emit",
                    "S%d F%d: Emitting liveNoteEnded (endSec=%.3f, liveT=%.3f)",
                    s, endedFret, mostRecentEndSec, m_liveTimeSec);
#endif
                QMetaObject::invokeMethod(this,
                    [this, s, endedFret]() { emit liveNoteEnded(s, endedFret); },
                    Qt::QueuedConnection);
            }
            m_activeNoteDisplayed[std::size_t(s)] = false;
            // Clear m_lastLiveFret so we don't emit duplicate note-off on next pass
            m_lastLiveFret[std::size_t(s)] = -1;
        }
    }
    }

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
        qInfo() << "[UI-NOTE-ON]  S" << ev.stringIndex << "F" << ev.fretIndex 
                << "vel=" << QString::number(ev.velocity, 'f', 2)
                << "t=" << QString::number(ev.startSec, 'f', 3);
        emit liveNoteTriggered(ev.stringIndex, ev.fretIndex, ev.velocity);
    }
    
    // UI-to-YIN Sync Check: Compare what UI displays vs what YIN worker reports
    // Done at end of dispatchLiveEvents after all UI state updates are complete
    // This helps debug timing/latency issues between detection and display
    {
        std::array<int, 6> uiDisplayedFrets{};
        std::array<int, 6> yinActiveFrets{};
        std::array<bool, 6> uiDisplayedFlags{};
        std::array<bool, 6> yinSustainingFlags{};
        
        for (int s = 0; s < 6; ++s) {
            // UI state: what is currently shown on screen
            uiDisplayedFrets[s] = m_lastLiveFret[s];
            uiDisplayedFlags[s] = m_activeNoteDisplayed[s];
            
            // YIN worker state: what the detection engine currently reports
            int fret = -1;
            float energy = 0.0f;
            bool attack = false;
            bool sustaining = false;
            float pitchHz = 0.0f;
            float threshold = 0.0f;
            m_atomicNoteState.readString(s, fret, energy, attack, sustaining, pitchHz, threshold);
            
            yinActiveFrets[s] = fret;
            yinSustainingFlags[s] = sustaining;
        }
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
    // Derive calibration profile from tuning state
    for (int i = 0; i < 6; ++i) {
        m_calibrationProfile.multipliers[static_cast<std::size_t>(i)] = 
            NoteDetectionStore::instance().committedValueFromKey("calibrationGainMultiplier", i);
    }
    
    // Try to load avgRms and peakRms from legacy calibration file if it exists
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
                        m_calibrationProfile.peakRms[static_cast<std::size_t>(i)] = static_cast<float>(peakArr[i].toDouble());
                    }
                    SessionLogger::instance().log("calibration", "Loaded avgRms/peakRms from legacy calibration file");
                }
            }
        }
    }
    
    m_calibrationProfile.valid = true;
    if (m_engine)
        m_engine->applyCalibration(m_calibrationProfile);

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
        // Apply to engine
        if (m_engine)
            m_engine->applyCalibration(m_calibrationProfile);
        
        SessionLogger::instance().log("calibration", "Calibration multipliers synced to engine");
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
// =============================================================================
// HEATMAP LOGGING (for UI debugging)
// =============================================================================

void TabEngineBridge::logHeatmapUIBatchStart() {
    HeatmapLogger::instance().logUIBatchStart(m_noteStateRevision);
}

void TabEngineBridge::logHeatmapUIEntry(int stringIndex, int fretIndex, qreal magnitude,
                                        qreal threshold, qreal intensity, qreal alpha, bool isAboveThreshold) {
    HeatmapLogger::instance().logUIBatchEntry(
        stringIndex, fretIndex,
        static_cast<float>(magnitude),
        static_cast<float>(threshold),
        static_cast<float>(intensity),
        static_cast<float>(alpha),
        isAboveThreshold
    );
}

void TabEngineBridge::logHeatmapUIBatchEnd() {
    HeatmapLogger::instance().logUIBatchEnd();
}

void TabEngineBridge::setHeatmapLoggingEnabled(bool enabled) {
    HeatmapLogger::instance().setEnabled(enabled);
}

void TabEngineBridge::setHeatmapEnabled(bool enabled) {
    const bool wasEnabled = m_heatmapEnabled.exchange(enabled, std::memory_order_acq_rel);
    if (wasEnabled != enabled) {
        // Notify YIN worker of heatmap state change
        if (m_yinWorker) {
            m_yinWorker->setHeatmapEnabled(enabled);
        }
        emit heatmapEnabledChanged();
    }
}