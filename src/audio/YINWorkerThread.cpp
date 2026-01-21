/**
 * YINWorkerThread.cpp - TIER 2 Processing Thread Implementation (YIN-based)
 * 
 * Replaces CQTWorkerThread with YIN pitch detection for lower latency
 * and better low-frequency detection.
 */

#include "YINWorkerThread.h"
#include "../NoteDetectionStore.h"
#include "../SessionLogger.h"

#include <QDebug>
#include <chrono>
#include <cstring>
#include <algorithm>
#include <cmath>

// Enable detailed YIN note event logging for debugging
#define YIN_NOTE_DEBUG 1

#ifdef __linux__
#include <pthread.h>
#endif

namespace audio {

YINWorkerThread::YINWorkerThread(AudioRingBuffer& inputBuffer,
                                 AtomicNoteState& outputState,
                                 const YINWorkerConfig& config)
    : m_inputBuffer(inputBuffer)
    , m_outputState(outputState)
    , m_config(config)
    , m_sampleRate(config.sampleRate)
    , m_accumBuffers{}  // Zero-initialize
{
    // Initialize calibration atomics
    for (int i = 0; i < 6; ++i) {
        m_calibrationAvgRms[i].store(0.0f);
        m_calibrationPeakRms[i].store(0.0f);
    }
    
    // Initialize YIN parameters from config
    m_yinThreshold.store(config.yinThreshold);
    m_noiseGateRMS.store(config.noiseGateRMS);
    m_onsetSensitivity.store(config.onsetSensitivity);
    m_releaseRatio.store(config.releaseRatio);
    m_fretStabilityFrames.store(config.fretStabilityFrames);
    
    // Initialize band-pass filter bank for crosstalk rejection
    if (config.enableBandPassFilters) {
        m_bandPassBank.configure(static_cast<double>(config.sampleRate));
    }
    
    // Create per-string YIN detectors with adaptive buffer sizes
    const int sampleRate = static_cast<int>(config.sampleRate);
    for (int s = 0; s < 6; ++s) {
        auto [onsetSize, pitchSize] = getAdaptiveBufferSizes(s);
        m_yinDetectors[s] = std::make_unique<FastYINDetector>(
            sampleRate, onsetSize, pitchSize, s);
    }
}

YINWorkerThread::~YINWorkerThread() {
    stop();
}

void YINWorkerThread::start() {
    if (m_running.load(std::memory_order_acquire)) {
        return;  // Already running
    }
    
    m_stopRequested.store(false, std::memory_order_release);
    m_running.store(true, std::memory_order_release);
    
    m_thread = std::thread(&YINWorkerThread::workerLoop, this);
    
#ifdef __linux__
    // Set thread name for debugging
    pthread_setname_np(m_thread.native_handle(), "YINWorker");
#endif
}

void YINWorkerThread::stop() {
    if (!m_running.load(std::memory_order_acquire)) {
        return;
    }
    
    m_stopRequested.store(true, std::memory_order_release);
    m_audioAvailable.store(true, std::memory_order_release);  // Wake up if sleeping
    
    if (m_thread.joinable()) {
        m_thread.join();
    }
    
    m_running.store(false, std::memory_order_release);
}

void YINWorkerThread::setSampleRate(float sr) {
    m_sampleRate.store(sr, std::memory_order_release);
    m_outputState.setSampleRate(sr);
    
    // Reconfigure band-pass filters for new sample rate
    if (m_config.enableBandPassFilters) {
        m_bandPassBank.configure(static_cast<double>(sr));
        m_bandPassBank.reset();
    }
    
    // Update YIN detectors
    const int sampleRate = static_cast<int>(sr);
    for (auto& detector : m_yinDetectors) {
        if (detector) {
            detector->setSampleRate(sampleRate);
        }
    }
}

void YINWorkerThread::setCalibration(const std::array<float, 6>& avgRms,
                                      const std::array<float, 6>& peakRms) {
    for (int i = 0; i < 6; ++i) {
        m_calibrationAvgRms[i].store(avgRms[i], std::memory_order_relaxed);
        m_calibrationPeakRms[i].store(peakRms[i], std::memory_order_relaxed);
    }
    m_calibrationValid.store(true, std::memory_order_release);
}

void YINWorkerThread::setYINThreshold(float threshold) {
    m_yinThreshold.store(std::clamp(threshold, 0.01f, 0.50f), std::memory_order_release);
}

void YINWorkerThread::setNoiseGateRMS(float noiseGate) {
    m_noiseGateRMS.store(std::clamp(noiseGate, 0.001f, 0.10f), std::memory_order_release);
}

void YINWorkerThread::setOnsetSensitivity(float sensitivity) {
    m_onsetSensitivity.store(std::clamp(sensitivity, 1.1f, 3.0f), std::memory_order_release);
}

void YINWorkerThread::setReleaseRatio(float ratio) {
    m_releaseRatio.store(std::clamp(ratio, 0.1f, 0.8f), std::memory_order_release);
}

void YINWorkerThread::setFretStabilityFrames(int frames) {
    m_fretStabilityFrames.store(std::clamp(frames, 1, 10), std::memory_order_release);
}

FastYINParams YINWorkerThread::buildParams() {
    FastYINParams params;
    params.yinThreshold = m_yinThreshold.load(std::memory_order_acquire);
    params.noiseGateRMS = m_noiseGateRMS.load(std::memory_order_acquire);
    params.onsetSensitivity = m_onsetSensitivity.load(std::memory_order_acquire);
    params.releaseRatio = m_releaseRatio.load(std::memory_order_acquire);
    params.fretStabilityFrames = m_fretStabilityFrames.load(std::memory_order_acquire);
    return params;
}

void YINWorkerThread::workerLoop() {
    // Configure thread for TIER 2
    if (m_config.enableCoreAffinity) {
        bool pinned = pinToCore(m_config.coreId);
        bool highPri = setHighPriority(50);
        (void)pinned;
        (void)highPri;
        // Log success/failure in production
    }
    
    AudioFrame frame;
    
    while (!m_stopRequested.load(std::memory_order_acquire)) {
        // Wait for audio to be available (spin with backoff)
        int spinCount = 0;
        while (!m_audioAvailable.exchange(false, std::memory_order_acq_rel)) {
            if (m_stopRequested.load(std::memory_order_acquire)) {
                return;
            }
            
            // Adaptive backoff: spin briefly, then yield, then sleep
            if (++spinCount < 100) {
                // Spin
                __asm__ volatile("" ::: "memory");
            } else if (spinCount < 1000) {
                std::this_thread::yield();
            } else {
                // Short sleep to avoid burning CPU
                std::this_thread::sleep_for(std::chrono::microseconds(100));
                spinCount = 500;  // Reset to yield range
            }
        }
        
        // Drain all available audio from ring buffer
        while (m_inputBuffer.pop(frame)) {
            // Apply per-string band-pass filtering for crosstalk rejection
            // Filter first, THEN accumulate for YIN processing
            if (m_config.enableBandPassFilters) {
                m_bandPassBank.processFrame(frame.samples);
            }
            
            // Accumulate filtered samples for each string
            for (int s = 0; s < 6; ++s) {
                if (m_accumSamples < kMaxAccumSamples) {
                    m_accumBuffers[s][m_accumSamples] = frame.samples[s];
                }
            }
            m_accumSamples++;
            
            // Check if we've accumulated enough for a YIN hop
            if (m_accumSamples >= m_config.hopSize) {
                processYINFrame();
                
                // Shift buffers by hop size (keep overlap for continuity)
                const int overlap = m_accumSamples - m_config.hopSize;
                if (overlap > 0) {
                    for (auto& buf : m_accumBuffers) {
                        std::memmove(buf.data(), buf.data() + m_config.hopSize,
                                    overlap * sizeof(float));
                    }
                }
                m_accumSamples = overlap;
            }
        }
    }
}

void YINWorkerThread::processYINFrame() {
    // Build detection parameters
    FastYINParams params = buildParams();
    
    // Process each string with its YIN detector
    for (int s = 0; s < 6; ++s) {
        if (!m_yinDetectors[s]) continue;
        
        // Run YIN detection on accumulated samples
        FastYINResult result = m_yinDetectors[s]->process(
            m_accumBuffers[s].data(), m_accumSamples, params);
        
        // Update atomic note state
        // During ATTACK, use detectedFret for faster response (don't wait for stability)
        // During SUSTAIN, use stableFret for less jitter
        const bool isAttack = result.isOnset;
        const int fret = isAttack ? result.detectedFret : result.stableFret;
        const float energy = result.currentRMS;
        const bool isSustaining = result.isSustaining;
        const float pitchHz = result.pitchResult.frequency;
        const float onsetThreshold = result.onsetThreshold;
        
        // Signal onset counter when entering ATTACK state
        // This ensures TabEngine never misses an onset, even if ATTACK only lasts one frame
        if (isAttack) {
            m_outputState.signalOnset(s);
        }
        
#if YIN_NOTE_DEBUG
        // Log YIN state transitions for debugging note-off issues
        static std::array<int, 6> s_prevFret = {-1, -1, -1, -1, -1, -1};
        static std::array<bool, 6> s_prevSustaining = {false, false, false, false, false, false};
        
        // Log on state change or when there's activity
        if (fret != s_prevFret[s] || isSustaining != s_prevSustaining[s] || 
            (energy > 0.01f && (isAttack || !isSustaining))) {
            SessionLogger::instance().logf("yin-state",
                "S%d: fret=%d attack=%d sustain=%d rms=%.4f pitch=%.1fHz state=%d prevFret=%d",
                s, fret, isAttack ? 1 : 0, isSustaining ? 1 : 0,
                energy, pitchHz, static_cast<int>(result.state), s_prevFret[s]);
        }
        s_prevFret[s] = fret;
        s_prevSustaining[s] = isSustaining;
#endif
        
        m_outputState.updateString(s, fret, energy, isAttack, isSustaining, pitchHz, onsetThreshold);
        
        // Call legacy callback if set
        if (m_noteCallback) {
            m_noteCallback(s, fret, energy, isAttack, isSustaining);
        }
        
        // Update heatmap bin magnitudes if enabled
        // For YIN, we use the pitch confidence as the bin magnitude for the detected fret
        if (m_heatmapEnabled.load(std::memory_order_acquire)) {
            // Clear all bin magnitudes for this string
            for (int f = 0; f < 25; ++f) {
                m_outputState.setBinMagnitude(s, f, 0.0f);
            }
            
            // Set magnitude for detected fret based on confidence and energy
            if (fret >= 0 && fret <= 24) {
                float magnitude = result.pitchResult.confidence * energy * 10.0f;
                magnitude = std::min(1.0f, magnitude);
                m_outputState.setBinMagnitude(s, fret, magnitude);
                
                // Add slight energy to adjacent frets for visual smoothness
                if (fret > 0) {
                    m_outputState.setBinMagnitude(s, fret - 1, magnitude * 0.2f);
                }
                if (fret < 24) {
                    m_outputState.setBinMagnitude(s, fret + 1, magnitude * 0.2f);
                }
            }
        }
    }
    
    // Increment frame counter
    m_framesProcessed.fetch_add(1, std::memory_order_relaxed);
    
    // Increment frame counter on output state
    m_outputState.advanceFrame();
}

} // namespace audio
