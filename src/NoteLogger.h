#pragma once
/**
 * NoteLogger.h - Dedicated note event logger
 * 
 * Outputs note events (ON, OFF, RETRIG, REPITCH) to a separate log file
 * in the same session directory as the main session log.
 * 
 * String naming convention:
 * - S1 = Low E (string index 0)
 * - S6 = High e (string index 5)
 */

#include <array>
#include <condition_variable>
#include <deque>
#include <fstream>
#include <mutex>
#include <string>
#include <thread>

class NoteLogger {
public:
    static NoteLogger& instance();

    // Note event logging
    // All methods accept rmsValues (current RMS for all 6 strings) and noiseFloor (noise gate thresholds)
    // to check if any string is above noise floor before logging
    void logNoteOn(int stringIdx, int fret, float rms, float envThreshold,
                   const std::array<float, 6>& rmsValues, const std::array<float, 6>& noiseFloor);
    void logNoteOff(int stringIdx, int fret, float rms, float offThreshold,
                    const std::array<float, 6>& rmsValues, const std::array<float, 6>& noiseFloor);
    void logRetrigger(int stringIdx, int fret, float spikeRms, float threshold,
                      const std::array<float, 6>& rmsValues, const std::array<float, 6>& noiseFloor);
    void logRepitch(int stringIdx, int oldFret, int newFret, float rms, float threshold,
                    const std::array<float, 6>& rmsValues, const std::array<float, 6>& noiseFloor);

    [[nodiscard]] bool enabled() const { return m_ready; }
    [[nodiscard]] const std::string& logFilePath() const { return m_logPath; }

private:
    NoteLogger();
    ~NoteLogger();

    NoteLogger(const NoteLogger&) = delete;
    NoteLogger& operator=(const NoteLogger&) = delete;

    void enqueue(std::string line);
    void workerLoop();
    static std::string resolveLogDirectory();
    static std::string makeTimestampedName();
    static std::string isoTimestamp();
    
    // Convert internal string index (0-5) to user-facing string number (S1-S6)
    static std::string stringName(int stringIdx);

    std::string m_logPath;
    bool m_ready {false};
    std::ofstream m_stream;
    std::mutex m_queueMutex;
    std::condition_variable m_cv;
    std::deque<std::string> m_pending;
    bool m_running {false};
    std::thread m_worker;
};
