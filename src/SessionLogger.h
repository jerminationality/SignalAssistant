#pragma once

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

    [[nodiscard]] bool enabled() const { return m_ready; }
    [[nodiscard]] const std::string& logFilePath() const { return m_logPath; }

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
    bool m_ready {false};
    std::ofstream m_stream;
    std::mutex m_queueMutex;
    std::condition_variable m_cv;
    std::deque<std::string> m_pending;
    bool m_running {false};
    std::thread m_worker;
};
