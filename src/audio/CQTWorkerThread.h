#pragma once
/**
 * CQTWorkerThread.h - TIER 2 Processing Thread
 * 
 * Responsibilities:
 * - Pull audio frames from ring buffer (from TIER 1)
 * - Apply per-string band-pass filtering (crosstalk rejection)
 * - Accumulate hop-size worth of samples
 * - Perform CQT analysis
 * - Update atomic note state (for TIER 3)
 * 
 * Runs on Core 1 with high priority (SCHED_RR)
 */

#include "LockFreeRingBuffer.h"
#include "AtomicNoteState.h"
#include "ThreadPriority.h"
#include "ButterworthFilter.h"
#include "../CQT/CQTNoteDetector.h"  // For DetectionParams, GuitarFrame std::array types

#include <atomic>
#include <memory>
#include <thread>
#include <array>
#include <vector>
#include <functional>

// Forward declarations (CQTNoteDetector.h already included for DetectionParams, GuitarFrame)
class NoteDetectionStore;

namespace audio {

/**
 * Configuration for CQT Worker
 */
struct CQTWorkerConfig {
    int hopSize = 512;           // Samples between CQT frames (~11.6ms at 44.1kHz)
    float sampleRate = 44100.0f;
    bool enableCoreAffinity = true;
    int coreId = 1;              // Default to Core 1
    bool enableBandPassFilters = true;  // Enable per-string band-pass filtering
};

/**
 * Callback for when new note events are detected
 * Called from TIER 2 thread - must be thread-safe!
 */
using NoteEventCallback = std::function<void(int stringIdx, int fret, float energy, 
                                              bool isAttack, bool isSustaining)>;

/**
 * CQT Worker Thread (TIER 2)
 * 
 * Pulls audio from ring buffer, processes CQT, updates atomic note state
 */
class CQTWorkerThread {
public:
    explicit CQTWorkerThread(AudioRingBuffer& inputBuffer, 
                             AtomicNoteState& outputState,
                             const CQTWorkerConfig& config = {});
    ~CQTWorkerThread();
    
    // Non-copyable, non-movable
    CQTWorkerThread(const CQTWorkerThread&) = delete;
    CQTWorkerThread& operator=(const CQTWorkerThread&) = delete;
    
    /**
     * Start the worker thread
     */
    void start();
    
    /**
     * Stop the worker thread (blocks until thread exits)
     */
    void stop();
    
    /**
     * Check if worker is running
     */
    bool isRunning() const { return m_running.load(std::memory_order_acquire); }
    
    /**
     * Signal that new audio is available (called from TIER 1)
     * This is a lightweight hint - worker will check ring buffer
     */
    void notifyAudioAvailable() {
        m_audioAvailable.store(true, std::memory_order_release);
    }
    
    /**
     * Set sample rate (thread-safe)
     */
    void setSampleRate(float sr);
    
    /**
     * Get current processing statistics
     */
    std::uint64_t framesProcessed() const { return m_framesProcessed.load(); }
    std::uint64_t bufferUnderruns() const { return m_underruns.load(); }
    
    /**
     * Set callback for note events (optional, for legacy integration)
     */
    void setNoteEventCallback(NoteEventCallback cb) { m_noteCallback = std::move(cb); }
    
    /**
     * Update calibration data (thread-safe)
     */
    void setCalibration(const std::array<float, 6>& avgRms, 
                        const std::array<float, 6>& peakRms);
    
    /**
     * Enable/disable heatmap bin magnitude writes (GOAL 3: Conditional Atomics)
     * When disabled, skips 150 atomic writes per audio frame to save CPU
     */
    void setHeatmapEnabled(bool enabled) {
        m_heatmapEnabled.store(enabled, std::memory_order_release);
    }
    bool heatmapEnabled() const {
        return m_heatmapEnabled.load(std::memory_order_acquire);
    }
    
private:
    void workerLoop();
    void processCQTFrame();
    void buildDetectionParamsNoAlloc(std::array<DetectionParams, 6>& outParams);
    
    // Input/Output interfaces
    AudioRingBuffer& m_inputBuffer;
    AtomicNoteState& m_outputState;
    
    // Configuration
    CQTWorkerConfig m_config;
    std::atomic<float> m_sampleRate;
    
    // Thread management
    std::thread m_thread;
    std::atomic<bool> m_running{false};
    std::atomic<bool> m_stopRequested{false};
    std::atomic<bool> m_audioAvailable{false};
    
    // Pre-CQT Band-Pass Filtering (crosstalk rejection)
    HexBandPassBank m_bandPassBank;
    
    // CQT Processing
    std::unique_ptr<CQTNoteDetector> m_cqtDetector;
    
    // Pre-allocated buffers for RT processing (NO HEAP ALLOCATION)
    std::array<DetectionParams, 6> m_detectionParams;  // Reused each frame
    std::array<GuitarFrame, 6> m_outputFrames;         // Reused each frame
    
    // Accumulation buffers (hop-size collection) - use std::array, NOT std::vector
    static constexpr int kMaxAccumSamples = 4096;
    std::array<std::array<float, kMaxAccumSamples>, 6> m_accumBuffers;
    int m_accumSamples{0};
    
    // Calibration data (updated atomically via mutex-free double-buffer or atomics)
    std::atomic<bool> m_calibrationValid{false};
    std::array<std::atomic<float>, 6> m_calibrationAvgRms;
    std::array<std::atomic<float>, 6> m_calibrationPeakRms;
    
    // Statistics
    std::atomic<std::uint64_t> m_framesProcessed{0};
    std::atomic<std::uint64_t> m_underruns{0};
    
    // Heatmap control (GOAL 3: Conditional Atomics)
    std::atomic<bool> m_heatmapEnabled{true};  // Controls 150 atomic writes per frame (default ON)
    
    // CQT Sharpening: Previous bin magnitudes for temporal smoothing
    std::array<std::array<float, 25>, 6> m_prevBinMagnitudes{};
    
    // Optional callback
    NoteEventCallback m_noteCallback;
};

} // namespace audio
