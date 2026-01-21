#pragma once
/**
 * YINWorkerThread.h - TIER 2 Processing Thread (YIN-based)
 * 
 * Replaces CQTWorkerThread with YIN pitch detection for:
 * - Lower latency (23-46ms vs 93ms)
 * - Better low-frequency detection (Low E string)
 * - Faster attack response (16th notes at 120 BPM)
 * - Lower CPU usage (no FFT required)
 * 
 * Responsibilities:
 * - Pull audio frames from ring buffer (from TIER 1)
 * - Apply per-string band-pass filtering (crosstalk rejection) 
 * - Run YIN pitch detection per string
 * - Update atomic note state (for TIER 3)
 * 
 * Runs on Core 1 with high priority (SCHED_RR)
 */

#include "LockFreeRingBuffer.h"
#include "AtomicNoteState.h"
#include "ThreadPriority.h"
#include "ButterworthFilter.h"
#include "../YIN/FastYINDetector.h"

#include <atomic>
#include <memory>
#include <thread>
#include <array>
#include <functional>

// Forward declarations
class NoteDetectionStore;

namespace audio {

/**
 * Configuration for YIN Worker
 */
struct YINWorkerConfig {
    int hopSize = 256;           // Samples between YIN frames (~5.3ms at 48kHz)
    float sampleRate = 48000.0f;
    bool enableCoreAffinity = true;
    int coreId = 1;              // Default to Core 1
    bool enableBandPassFilters = true;  // Enable per-string band-pass filtering
    
    // YIN-specific parameters
    float yinThreshold = 0.10f;       // YIN confidence threshold
    float noiseGateRMS = 0.015f;      // Minimum RMS for detection
    float onsetSensitivity = 1.4f;    // RMS ratio for onset
    float releaseRatio = 0.35f;       // Release threshold ratio
    int fretStabilityFrames = 2;      // Fret stability hysteresis
};

/**
 * Callback for when new note events are detected
 * Called from TIER 2 thread - must be thread-safe!
 */
using YINNoteEventCallback = std::function<void(int stringIdx, int fret, float energy,
                                                 bool isAttack, bool isSustaining)>;

/**
 * YIN Worker Thread (TIER 2)
 * 
 * Pulls audio from ring buffer, processes YIN, updates atomic note state
 */
class YINWorkerThread {
public:
    explicit YINWorkerThread(AudioRingBuffer& inputBuffer,
                             AtomicNoteState& outputState,
                             const YINWorkerConfig& config = {});
    ~YINWorkerThread();
    
    // Non-copyable, non-movable
    YINWorkerThread(const YINWorkerThread&) = delete;
    YINWorkerThread& operator=(const YINWorkerThread&) = delete;
    
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
    void setNoteEventCallback(YINNoteEventCallback cb) { m_noteCallback = std::move(cb); }
    
    /**
     * Update calibration data (thread-safe)
     */
    void setCalibration(const std::array<float, 6>& avgRms,
                        const std::array<float, 6>& peakRms);
    
    /**
     * Enable/disable heatmap bin magnitude writes
     * When disabled, skips atomic writes to save CPU
     */
    void setHeatmapEnabled(bool enabled) {
        m_heatmapEnabled.store(enabled, std::memory_order_release);
    }
    bool heatmapEnabled() const {
        return m_heatmapEnabled.load(std::memory_order_acquire);
    }
    
    /**
     * Update YIN parameters (thread-safe)
     */
    void setYINThreshold(float threshold);
    void setNoiseGateRMS(float noiseGate);
    void setOnsetSensitivity(float sensitivity);
    void setReleaseRatio(float ratio);
    void setFretStabilityFrames(int frames);
    
    /**
     * Get current YIN parameters
     */
    float yinThreshold() const { return m_yinThreshold.load(std::memory_order_acquire); }
    float noiseGateRMS() const { return m_noiseGateRMS.load(std::memory_order_acquire); }
    float onsetSensitivity() const { return m_onsetSensitivity.load(std::memory_order_acquire); }
    float releaseRatio() const { return m_releaseRatio.load(std::memory_order_acquire); }
    int fretStabilityFrames() const { return m_fretStabilityFrames.load(std::memory_order_acquire); }

private:
    void workerLoop();
    void processYINFrame();
    FastYINParams buildParams();
    
    // Input/Output interfaces
    AudioRingBuffer& m_inputBuffer;
    AtomicNoteState& m_outputState;
    
    // Configuration
    YINWorkerConfig m_config;
    std::atomic<float> m_sampleRate;
    
    // Thread management
    std::thread m_thread;
    std::atomic<bool> m_running{false};
    std::atomic<bool> m_stopRequested{false};
    std::atomic<bool> m_audioAvailable{false};
    
    // Pre-YIN Band-Pass Filtering (crosstalk rejection)
    HexBandPassBank m_bandPassBank;
    
    // Per-string YIN detectors with adaptive buffer sizes
    std::array<std::unique_ptr<FastYINDetector>, 6> m_yinDetectors;
    
    // Accumulation buffers for collecting hop-size samples
    static constexpr int kMaxAccumSamples = 4096;
    std::array<std::array<float, kMaxAccumSamples>, 6> m_accumBuffers;
    int m_accumSamples{0};
    
    // Calibration data (updated atomically)
    std::atomic<bool> m_calibrationValid{false};
    std::array<std::atomic<float>, 6> m_calibrationAvgRms;
    std::array<std::atomic<float>, 6> m_calibrationPeakRms;
    
    // YIN parameters (atomically updatable)
    std::atomic<float> m_yinThreshold{0.10f};
    std::atomic<float> m_noiseGateRMS{0.015f};
    std::atomic<float> m_onsetSensitivity{1.4f};
    std::atomic<float> m_releaseRatio{0.35f};
    std::atomic<int> m_fretStabilityFrames{2};
    
    // Statistics
    std::atomic<std::uint64_t> m_framesProcessed{0};
    std::atomic<std::uint64_t> m_underruns{0};
    
    // Heatmap control
    std::atomic<bool> m_heatmapEnabled{true};
    
    // Optional callback
    YINNoteEventCallback m_noteCallback;
};

} // namespace audio
