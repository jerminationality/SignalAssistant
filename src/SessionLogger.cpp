#include "SessionLogger.h"

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <cstdio>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <string>

namespace {
std::string makeTimestampedName() {
    const auto now = std::chrono::system_clock::now();
    const std::time_t tt = std::chrono::system_clock::to_time_t(now);
    std::tm tm{};
#if defined(_WIN32)
    localtime_s(&tm, &tt);
#else
    localtime_r(&tt, &tm);
#endif
    std::ostringstream oss;
    oss << std::put_time(&tm, "%Y%m%d-%H%M%S");
    return oss.str();
}

std::string isoTimestamp() {
    const auto now = std::chrono::system_clock::now();
    const std::time_t tt = std::chrono::system_clock::to_time_t(now);
    std::tm tm{};
#if defined(_WIN32)
    localtime_s(&tm, &tt);
#else
    localtime_r(&tt, &tm);
#endif
    std::ostringstream oss;
    oss << std::put_time(&tm, "%Y-%m-%d %H:%M:%S");
    return oss.str();
}
}

SessionLogger& SessionLogger::instance() {
    static SessionLogger g_logger;
    return g_logger;
}

SessionLogger::SessionLogger() {
    const std::string dir = resolveLogDirectory();
    if (dir.empty())
        return;

    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    if (ec)
        return;

    const std::string filename = "session-" + makeTimestampedName() + ".log";
    std::filesystem::path path = std::filesystem::path(dir) / filename;
    m_logPath = path.string();
    m_stream.open(path, std::ios::out | std::ios::trunc);
    if (!m_stream.is_open())
        return;

    m_stream << "# SignalAssistant session log\n";
    m_stream << "# Started at " << isoTimestamp() << "\n";
    m_stream.flush();
    m_running = true;
    m_worker = std::thread(&SessionLogger::workerLoop, this);
    m_ready = true;
}

SessionLogger::~SessionLogger() {
    if (m_ready) {
        {
            std::lock_guard<std::mutex> guard(m_queueMutex);
            m_running = false;
        }
        m_cv.notify_all();
        if (m_worker.joinable())
            m_worker.join();
    }
    if (m_stream.is_open()) {
        m_stream << "# Session closed at " << isoTimestamp() << "\n";
        m_stream.flush();
    }
}

void SessionLogger::log(const std::string& component, const std::string& message) {
    if (!m_ready)
        return;
    writeLine(component, message);
}

void SessionLogger::logf(const std::string& component, const char* fmt, ...) {
    if (!m_ready || !fmt)
        return;
    va_list args;
    va_start(args, fmt);
    const std::string formatted = formatString(fmt, args);
    va_end(args);
    writeLine(component, formatted);
}

void SessionLogger::writeLine(const std::string& component, const std::string& message) {
    enqueue(composeLine(component, message));
}

void SessionLogger::enqueue(std::string line) {
    std::unique_lock<std::mutex> lock(m_queueMutex, std::try_to_lock);
    if (!lock.owns_lock())
        return;
    m_pending.emplace_back(std::move(line));
    lock.unlock();
    m_cv.notify_one();
}

void SessionLogger::workerLoop() {
    std::unique_lock<std::mutex> lock(m_queueMutex);
    while (m_running || !m_pending.empty()) {
        if (m_pending.empty()) {
            m_cv.wait(lock, [this]() { return !m_running || !m_pending.empty(); });
            continue;
        }
        auto line = std::move(m_pending.front());
        m_pending.pop_front();
        lock.unlock();
        if (m_stream.is_open()) {
            m_stream << line << '\n';
            m_stream.flush();
        }
        lock.lock();
    }
    if (m_stream.is_open())
        m_stream.flush();
}

std::string SessionLogger::composeLine(const std::string& component, const std::string& message) const {
    std::ostringstream oss;
    oss << isoTimestamp() << ' ';
    if (!component.empty())
        oss << '[' << component << "] ";
    oss << message;
    return oss.str();
}

std::string SessionLogger::formatString(const char* fmt, va_list args) {
    if (!fmt)
        return {};
    va_list copy;
    va_copy(copy, args);
    const int needed = std::vsnprintf(nullptr, 0, fmt, copy);
    va_end(copy);
    if (needed <= 0)
        return {};
    std::string buffer(static_cast<std::size_t>(needed) + 1, '\0');
    std::vsnprintf(buffer.data(), buffer.size(), fmt, args);
    buffer.resize(static_cast<std::size_t>(needed));
    return buffer;
}

std::string SessionLogger::resolveLogDirectory() {
    if (const char* env = std::getenv("SIGNALASSISTANT_LOG_DIR")) {
        if (*env)
            return std::string(env);
    }
    if (const char* xdgState = std::getenv("XDG_STATE_HOME")) {
        if (*xdgState)
            return (std::filesystem::path(xdgState) / "SignalAssistant" / "logs").string();
    }
    return (std::filesystem::current_path() / "logs").string();
}
