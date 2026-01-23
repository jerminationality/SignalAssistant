/**
 * CQTWorkerThread.cpp - TIER 2 Processing Thread Implementation
 */

#include "CQTWorkerThread.h"
#include "../CQT/CQTNoteDetector.h"
#include "../NoteDetectionStore.h"

#include <QDebug>
#include <chrono>
#include <cstring>
#include <algorithm>
#include <cmath>

#ifdef __linux__
#include <pthread.h>
#endif

namespace audio {

CQTWorkerThread::CQTWorkerThread(AudioRingBuffer& inputBuffer,
                                 AtomicNoteState& outputState,
                                 const CQTWorkerConfig& config)
    : m_inputBuffer(inputBuffer)
    , m_outputState(outputState)
    , m_config(config)
    , m_sampleRate(config.sampleRate)
    , m_accumBuffers{}  // Zero-initialize std::array (no heap allocation)
    , m_detectionParams{}
    , m_outputFrames{}
{
    // Initialize calibration atomics
    for (int i = 0; i < 6; ++i) {
        m_calibrationAvgRms[i].store(0.0f);
        m_calibrationPeakRms[i].store(0.0f);
    }
    
    // Initialize band-pass filter bank for crosstalk rejection
    if (config.enableBandPassFilters) {
        m_bandPassBank.configure(static_cast<double>(config.sampleRate));
    }
    
    // Create CQT detector
    m_cqtDetector = std::make_unique<CQTNoteDetector>(static_cast<double>(config.sampleRate));
}

CQTWorkerThread::~CQTWorkerThread() {
    stop();
}

void CQTWorkerThread::start() {
    if (m_running.load(std::memory_order_acquire)) {
        return;  // Already running
    }
    
    m_stopRequested.store(false, std::memory_order_release);
    m_running.store(true, std::memory_order_release);
    
    m_thread = std::thread(&CQTWorkerThread::workerLoop, this);
    
#ifdef __linux__
    // Set thread name for debugging
    pthread_setname_np(m_thread.native_handle(), "CQTWorker");
#endif
}

void CQTWorkerThread::stop() {
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

void CQTWorkerThread::setSampleRate(float sr) {
    m_sampleRate.store(sr, std::memory_order_release);
    m_outputState.setSampleRate(sr);
    
    // Reconfigure band-pass filters for new sample rate
    if (m_config.enableBandPassFilters) {
        m_bandPassBank.configure(static_cast<double>(sr));
        m_bandPassBank.reset();  // Reset filter state on sample rate change
    }
    
    if (m_cqtDetector) {
        m_cqtDetector->setSampleRate(static_cast<double>(sr));
    }
}

void CQTWorkerThread::setCalibration(const std::array<float, 6>& avgRms,
                                      const std::array<float, 6>& peakRms) {
    for (int i = 0; i < 6; ++i) {
        m_calibrationAvgRms[i].store(avgRms[i], std::memory_order_relaxed);
        m_calibrationPeakRms[i].store(peakRms[i], std::memory_order_relaxed);
    }
    m_calibrationValid.store(true, std::memory_order_release);
}

void CQTWorkerThread::workerLoop() {
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
            // Filter first, THEN accumulate for CQT/RMS processing
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
            
            // Check if we've accumulated enough for a CQT hop
            if (m_accumSamples >= m_config.hopSize) {
                processCQTFrame();
                
                // Shift buffers by hop size (keep overlap)
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

void CQTWorkerThread::processCQTFrame() {
    if (!m_cqtDetector) return;
        // Update sample rate if changed
    float sr = m_sampleRate.load(std::memory_order_acquire);
    m_cqtDetector->setSampleRate(static_cast<double>(sr));
    
    // Build detection parameters into pre-allocated array (NO HEAP ALLOCATION)
    buildDetectionParamsNoAlloc(m_detectionParams);
    
    // Prepare buffer pointers for CQT
    float* buffers[6];
    for (int s = 0; s < 6; ++s) {
        buffers[s] = m_accumBuffers[s].data();
    }
    
    // Process CQT into pre-allocated output frames (NO HEAP ALLOCATION)
    m_cqtDetector->processNoAlloc(buffers, m_accumSamples, m_detectionParams, m_outputFrames);
    
    // Update atomic note state from detected notes (ZERO I/O - no logging in DSP thread!)
    for (const auto& gf : m_outputFrames) {
        const int s = gf.stringID;
        if (s < 0 || s >= 6) continue;
        
        m_outputState.updateString(s, gf.fret, gf.binEnergy, 
                                   gf.isAttack, gf.isSustaining, gf.pitchHz);
        
        // Call legacy callback if set
        if (m_noteCallback) {
            m_noteCallback(s, gf.fret, gf.binEnergy, gf.isAttack, gf.isSustaining);
        }
    }
    
    // Conditional Atomics - only write 150 bin magnitudes when heatmap UI is visible
    // This prevents CPU leakage (150 atomic writes per audio frame) when heatmap is closed
    // CRITICAL: ZERO I/O in this loop - no qDebug/printf/cout (causes priority inversion)
    if (m_heatmapEnabled.load(std::memory_order_acquire)) {
        // =========================================================
        // ITERATED CQT SHARPENING (SAFE VERSION)
        // =========================================================
        
        constexpr float kGain = 2.5f;  // Prevent extinction during squaring
        
        for (int s = 0; s < 6; ++s) {
            std::array<float, 25> raw;
            std::array<float, 25> processed;

            // 1. Fill raw data and apply gain
            for (int f = 0; f < 25; ++f) {
                raw[f] = m_cqtDetector->getFretBinMagnitude(s, f) * kGain;
            }

            // 2. HARMONIC REINFORCEMENT (HRP)
            // Check if the octave (f+12) is strong, use it to bolster the root.
            for (int f = 0; f < 13; ++f) {
                float octaveVal = raw[f + 12];
                if (octaveVal > 0.05f) {
                    raw[f] += (octaveVal * 0.5f);  // Add 50% of octave energy to root
                }
            }

            // 3. SAFE LATERAL INHIBITION
            for (int f = 0; f < 25; ++f) {
                float current = raw[f];
                float left = (f > 0) ? raw[f-1] : 0.0f;
                float right = (f < 24) ? raw[f+1] : 0.0f;

                // Peak Logic: Only suppress if neighbors are CLEARLY stronger
                if (current > left && current > right) {
                    processed[f] = current;  // Keep the peak at 100%
                } else {
                    processed[f] = current * 0.15f;  // Dim neighbors by 85%, don't kill them
                }
            }

            // 4. TEMPORAL SMOOTHING & CONTRAST
            for (int f = 0; f < 25; ++f) {
                float prev = m_prevBinMagnitudes[s][f];
                
                // 50/50 Mix for balance of speed and stability
                float smoothed = (processed[f] * 0.5f) + (prev * 0.5f);
                
                // Non-Linear Contrast (Square only if significant)
                float finalOutput = (smoothed > 0.05f) ? (smoothed * smoothed) : 0.0f;
                finalOutput = std::min(finalOutput, 1.0f);
                
                // Store for next frame's temporal smoothing
                m_prevBinMagnitudes[s][f] = smoothed;
                
                // Write to atomic state
                m_outputState.setBinMagnitude(s, f, finalOutput);
            }
        }
        // =========================================================
    }
    
    m_outputState.advanceFrame();
    m_framesProcessed.fetch_add(1, std::memory_order_relaxed);
}

void CQTWorkerThread::buildDetectionParamsNoAlloc(std::array<DetectionParams, 6>& outParams) {
    // Get parameters from the global store - NO HEAP ALLOCATION
    auto& store = NoteDetectionStore::instance();
    const auto& np = store.current();
    
    bool calibValid = m_calibrationValid.load(std::memory_order_acquire);
    
    // Pre-fetch max RMS for spatial weight calculation (avoid repeated loop)
    float maxRms = 0.0f;
    if (calibValid) {
        for (int i = 0; i < 6; ++i) {
            float r = m_calibrationAvgRms[i].load(std::memory_order_relaxed);
            if (r > maxRms) maxRms = r;
        }
    }
    
    for (int s = 0; s < 6; ++s) {
        DetectionParams& dp = outParams[s];
        dp.baseline = np.baselineFloor[s];
        dp.envFloor = np.envelopeFloor[s];
        dp.preAmpGain = np.calibrationGainMultiplier[s];
        dp.gateRatio = np.gateRatio[s];
        dp.confirmationFrames = np.confirmationFrames[s];
        dp.fluxSensitivity = np.fluxSensitivity[s];
        dp.slopeDecay = np.slopeDecay[s];
        
        // Calculate spatial weight from calibration
        if (calibValid) {
            float avgRms = m_calibrationAvgRms[s].load(std::memory_order_relaxed);
            if (avgRms > 0.0001f && maxRms > 0.0001f) {
                dp.spatialWeight = maxRms / avgRms;
            } else {
                dp.spatialWeight = 1.0f;
            }
        } else {
            dp.spatialWeight = 1.0f;
        }
    }
}

} // namespace audio
