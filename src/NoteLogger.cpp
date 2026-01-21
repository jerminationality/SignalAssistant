#include "NoteLogger.h"

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <ctime>
#include <iomanip>
#include <sstream>

NoteLogger& NoteLogger::instance() {
    static NoteLogger g_logger;
    return g_logger;
}

NoteLogger::NoteLogger() {
    const std::string dir = resolveLogDirectory();
    if (dir.empty())
        return;

    // Create a month-year subfolder (MM-YYYY) under the resolved log directory
    const auto now = std::chrono::system_clock::now();
    const std::time_t tt = std::chrono::system_clock::to_time_t(now);
    std::tm tm{};
#if defined(_WIN32)
    localtime_s(&tm, &tt);
#else
    localtime_r(&tt, &tm);
#endif
    std::ostringstream monthOss;
    monthOss << std::put_time(&tm, "%m-%Y");
    std::filesystem::path monthDir = std::filesystem::path(dir) / monthOss.str();

    std::error_code ec;
    std::filesystem::create_directories(monthDir, ec);
    if (ec)
        return;

    const std::string filename = "notes-" + makeTimestampedName() + ".log";
    std::filesystem::path path = monthDir / filename;
    m_logPath = path.string();
    m_stream.open(path, std::ios::out | std::ios::trunc);
    if (!m_stream.is_open())
        return;

    m_stream << "# SignalAssistant note event log\n";
    m_stream << "# Started at " << isoTimestamp() << "\n";
    m_stream << "# Format: [EVENT] String Fret Values\n";
    m_stream << "# S1=Low E, S2=A, S3=D, S4=G, S5=B, S6=High e\n";
    m_stream << "#\n";
    m_stream.flush();
    m_running = true;
    m_worker = std::thread(&NoteLogger::workerLoop, this);
    m_ready = true;
}

NoteLogger::~NoteLogger() {
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

std::string NoteLogger::stringName(int stringIdx) {
    // Convert 0-based index to 1-based string number (Low E=S1, High e=S6)
    return "S" + std::to_string(stringIdx + 1);
}

void NoteLogger::logNoteOn(int stringIdx, int fret, float rms, float envThreshold,
                           const std::array<float, 6>& rmsValues, const std::array<float, 6>& noiseFloor) {
    if (!m_ready)
        return;
    
    // Log all note events without noise floor filtering
    (void)rmsValues;  // Unused
    (void)noiseFloor; // Unused
    
    std::ostringstream oss;
    oss << isoTimestamp() << " ";
    oss << "[ON]       " << stringName(stringIdx) << "  F" << std::left << std::setw(2) << fret;
    oss << "        RMS = " << std::fixed << std::setprecision(2) << rms;
    oss << "  >  ENV = " << std::fixed << std::setprecision(2) << envThreshold;
    
    enqueue(oss.str());
}

void NoteLogger::logNoteOff(int stringIdx, int fret, float rms, float offThreshold,
                            const std::array<float, 6>& rmsValues, const std::array<float, 6>& noiseFloor) {
    if (!m_ready)
        return;
    
    // Log all note events without noise floor filtering
    (void)rmsValues;  // Unused
    (void)noiseFloor; // Unused
    
    std::ostringstream oss;
    oss << isoTimestamp() << " ";
    oss << "[OFF]      " << stringName(stringIdx) << "  F" << std::left << std::setw(2) << fret;
    oss << "        RMS = " << std::fixed << std::setprecision(2) << rms;
    oss << "  <  OFF = " << std::fixed << std::setprecision(2) << offThreshold;
    
    enqueue(oss.str());
}

void NoteLogger::logRetrigger(int stringIdx, int fret, float spikeRms, float threshold,
                              const std::array<float, 6>& rmsValues, const std::array<float, 6>& noiseFloor) {
    if (!m_ready)
        return;
    
    // Log all note events without noise floor filtering
    (void)rmsValues;  // Unused
    (void)noiseFloor; // Unused
    
    std::ostringstream oss;
    oss << isoTimestamp() << " ";
    oss << "[RETRIG]   " << stringName(stringIdx) << "  F" << std::left << std::setw(2) << fret;
    oss << "        Spike = " << std::fixed << std::setprecision(2) << spikeRms;
    oss << "  >  Thresh = " << std::fixed << std::setprecision(2) << threshold;
    
    enqueue(oss.str());
}

void NoteLogger::logRepitch(int stringIdx, int oldFret, int newFret, float rms, float threshold,
                            const std::array<float, 6>& rmsValues, const std::array<float, 6>& noiseFloor) {
    if (!m_ready)
        return;
    
    // Log all note events without noise floor filtering
    (void)rmsValues;  // Unused
    (void)noiseFloor; // Unused
    
    std::ostringstream oss;
    oss << isoTimestamp() << " ";
    oss << "[REPITCH]  " << stringName(stringIdx) << "  F" << oldFret << " -> F" << newFret;
    oss << "   RMS = " << std::fixed << std::setprecision(2) << rms;
    oss << "  |  Thresh = " << std::fixed << std::setprecision(2) << threshold;
    
    enqueue(oss.str());
}



void NoteLogger::enqueue(std::string line) {
    std::unique_lock<std::mutex> lock(m_queueMutex, std::try_to_lock);
    if (!lock.owns_lock())
        return;
    m_pending.emplace_back(std::move(line));
    lock.unlock();
    m_cv.notify_one();
}

void NoteLogger::workerLoop() {
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

std::string NoteLogger::makeTimestampedName() {
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

std::string NoteLogger::isoTimestamp() {
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

std::string NoteLogger::resolveLogDirectory() {
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
