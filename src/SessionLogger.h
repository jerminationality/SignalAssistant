#pragma once

#include <array>
#include <condition_variable>
#include <cstdarg>
#include <deque>
#include <fstream>
#include <mutex>
#include <string>
#include <thread>

class SessionLogger {
public:
    static SessionLogger& instance();

    void log(const std::string& component, const std::string& message);
    void logf(const std::string& component, const char* fmt, ...);
    
    /**
     * Log RMS and envelope ticker for note detection monitoring.
     * Format: RMS [ x.xx | x.xx | x.xx | x.xx | x.xx | x.xx ]   ENV [ x.xx | x.xx | x.xx | x.xx | x.xx | x.xx ]
     * Only logs if any string is above its noise floor threshold.
     * @param rmsValues RMS values for each string (index 0 = low E / S1)
     * @param envValues Envelope threshold values for each string
     * @param noiseFloor Noise floor thresholds for each string
     */
    void logRmsEnvTicker(const std::array<float, 6>& rmsValues, 
                         const std::array<float, 6>& envValues,
                         const std::array<float, 6>& noiseFloor);

    void setComponentFilter(const std::string& filter);
    void clearComponentFilter();

    [[nodiscard]] bool enabled() const { return m_ready; }
    [[nodiscard]] const std::string& logFilePath() const { return m_logPath; }
    [[nodiscard]] const std::string& componentFilter() const { return m_componentFilter; }

private:
    SessionLogger();
    ~SessionLogger();

    SessionLogger(const SessionLogger&) = delete;
    SessionLogger& operator=(const SessionLogger&) = delete;

    void writeLine(const std::string& component, const std::string& message);
    void enqueue(std::string line);
    void workerLoop();
    std::string composeLine(const std::string& component, const std::string& message) const;
    static std::string formatString(const char* fmt, va_list args);
    static std::string resolveLogDirectory();

    std::string m_logPath;
    std::string m_componentFilter;
    bool m_ready {false};
    std::ofstream m_stream;
    std::mutex m_queueMutex;
    std::condition_variable m_cv;
    std::deque<std::string> m_pending;
    bool m_running {false};
    std::thread m_worker;
};
