#pragma once

#include <array>
#include <fstream>
#include <mutex>
#include <string>

/**
 * @brief HeatmapLogger - Logs CQT magnitude values and UI draw states
 * 
 * Captures:
 * 1. Raw magnitude values being pushed from CQT (150 bins: 6 strings × 25 frets)
 * 2. Computed UI states (intensity, alpha, stroke color)
 * 
 * Output: logs/heatmaplog (overwritten each session)
 */
class HeatmapLogger {
public:
    static HeatmapLogger& instance();

    // Called from TabEngineBridge::updateNoteStateCache() when note states change
    void logMagnitudeUpdate(
        std::uint64_t frameCount,
        const std::array<std::array<float, 25>, 6>& magnitudes,
        const std::array<std::array<float, 25>, 6>& thresholds
    );

    // Called from QML via bridge to log what's actually being drawn
    void logUIDrawState(
        int stringIndex,
        int fretIndex,
        float magnitude,
        float threshold,
        float intensity,
        float alpha,
        bool isAboveThreshold
    );
    
    // Log a batch of UI states (more efficient)
    void logUIBatchStart(std::uint64_t frameCount);
    void logUIBatchEntry(
        int stringIndex,
        int fretIndex,
        float magnitude,
        float threshold,
        float intensity,
        float alpha,
        bool isAboveThreshold
    );
    void logUIBatchEnd();

    [[nodiscard]] bool enabled() const { return m_ready; }
    [[nodiscard]] const std::string& logFilePath() const { return m_logPath; }
    
    void setEnabled(bool enabled);

private:
    HeatmapLogger();
    ~HeatmapLogger();

    HeatmapLogger(const HeatmapLogger&) = delete;
    HeatmapLogger& operator=(const HeatmapLogger&) = delete;

    void openLogFile();
    void writeHeader();
    std::string isoTimestamp() const;

    std::string m_logPath;
    std::ofstream m_stream;
    std::mutex m_mutex;
    bool m_ready {false};
    bool m_userEnabled {true};
    bool m_inBatch {false};
    std::uint64_t m_currentBatchFrame {0};
    int m_batchEntryCount {0};
};
