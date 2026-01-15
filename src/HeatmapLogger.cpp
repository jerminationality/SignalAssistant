#include "HeatmapLogger.h"

#include <chrono>
#include <ctime>
#include <filesystem>
#include <iomanip>
#include <sstream>

namespace {
std::string resolveLogDirectory() {
    // Check for environment variable override
    if (const char* dir = std::getenv("SIGNALASSISTANT_LOG_DIR")) {
        if (*dir)
            return std::string(dir);
    }
    
    // Default: logs directory relative to executable or project root
    std::filesystem::path logDir;
    
    // Try to find project root by looking for CMakeLists.txt
    std::filesystem::path current = std::filesystem::current_path();
    while (!current.empty() && current != current.root_path()) {
        if (std::filesystem::exists(current / "CMakeLists.txt")) {
            logDir = current / "logs";
            break;
        }
        current = current.parent_path();
    }
    
    // Fallback to current directory logs folder
    if (logDir.empty()) {
        logDir = std::filesystem::current_path() / "logs";
    }
    
    std::error_code ec;
    std::filesystem::create_directories(logDir, ec);
    if (ec) {
        return {};
    }
    
    return logDir.string();
}
}

HeatmapLogger& HeatmapLogger::instance() {
    static HeatmapLogger s_instance;
    return s_instance;
}

HeatmapLogger::HeatmapLogger() {
    openLogFile();
}

HeatmapLogger::~HeatmapLogger() {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_stream.is_open()) {
        m_stream << "\n# === Session ended at " << isoTimestamp() << " ===\n";
        m_stream.flush();
        m_stream.close();
    }
}

void HeatmapLogger::openLogFile() {
    const std::string dir = resolveLogDirectory();
    if (dir.empty())
        return;
    
    std::filesystem::path path = std::filesystem::path(dir) / "heatmaplog";
    m_logPath = path.string();
    
    // Open with trunc to overwrite each session
    m_stream.open(path, std::ios::out | std::ios::trunc);
    if (!m_stream.is_open())
        return;
    
    m_ready = true;
    writeHeader();
}

void HeatmapLogger::writeHeader() {
    m_stream << "# ===============================================\n";
    m_stream << "# HEATMAP MAGNITUDE LOG\n";
    m_stream << "# Session started: " << isoTimestamp() << "\n";
    m_stream << "# ===============================================\n";
    m_stream << "#\n";
    m_stream << "# Format:\n";
    m_stream << "# [MAG_UPDATE] frame=<N> - Raw CQT magnitudes from AtomicNoteState\n";
    m_stream << "#   S<0-5>: [F0:mag/thresh, F1:mag/thresh, ...] (25 frets per string)\n";
    m_stream << "#\n";
    m_stream << "# [UI_BATCH] frame=<N> - Values computed for UI drawing\n";
    m_stream << "#   S<s>F<f>: mag=<m> thresh=<t> int=<i> alpha=<a> above=<0|1>\n";
    m_stream << "#\n";
    m_stream << "# Legend:\n";
    m_stream << "#   S = String (0=Low E, 5=High e)\n";
    m_stream << "#   F = Fret (0-24)\n";
    m_stream << "#   mag = Raw CQT magnitude\n";
    m_stream << "#   thresh = Note-on threshold for this bin\n";
    m_stream << "#   int = Computed intensity (0.0-1.0)\n";
    m_stream << "#   alpha = Alpha channel for drawing (0.0-1.0)\n";
    m_stream << "#   above = 1 if magnitude >= threshold (note on)\n";
    m_stream << "# ===============================================\n\n";
    m_stream.flush();
}

std::string HeatmapLogger::isoTimestamp() const {
    const auto now = std::chrono::system_clock::now();
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()) % 1000;
    const std::time_t tt = std::chrono::system_clock::to_time_t(now);
    std::tm tm{};
#if defined(_WIN32)
    localtime_s(&tm, &tt);
#else
    localtime_r(&tt, &tm);
#endif
    std::ostringstream oss;
    oss << std::put_time(&tm, "%Y-%m-%d %H:%M:%S");
    oss << '.' << std::setfill('0') << std::setw(3) << ms.count();
    return oss.str();
}

void HeatmapLogger::setEnabled(bool enabled) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_userEnabled = enabled;
}

void HeatmapLogger::logMagnitudeUpdate(
    std::uint64_t frameCount,
    const std::array<std::array<float, 25>, 6>& magnitudes,
    const std::array<std::array<float, 25>, 6>& thresholds)
{
    if (!m_ready || !m_userEnabled)
        return;
    
    std::lock_guard<std::mutex> lock(m_mutex);
    
    m_stream << "[MAG_UPDATE] " << isoTimestamp() << " frame=" << frameCount << "\n";
    
    for (int s = 0; s < 6; ++s) {
        m_stream << "  S" << s << ": [";
        
        bool first = true;
        for (int f = 0; f <= 24; ++f) {
            const float mag = magnitudes[s][f];
            const float thresh = thresholds[s][f];
            
            // Only log non-trivial values to reduce noise
            if (mag > 0.005f || f == 0 || f == 12) {  // Always log open string and 12th fret
                if (!first) m_stream << ", ";
                first = false;
                m_stream << "F" << f << ":"
                         << std::fixed << std::setprecision(4) << mag
                         << "/" << std::setprecision(3) << thresh;
                
                // Mark if above threshold
                if (mag >= thresh && thresh > 0.01f) {
                    m_stream << "*";  // Asterisk indicates note-on
                }
            }
        }
        m_stream << "]\n";
    }
    m_stream << "\n";
    m_stream.flush();
}

void HeatmapLogger::logUIDrawState(
    int stringIndex,
    int fretIndex,
    float magnitude,
    float threshold,
    float intensity,
    float alpha,
    bool isAboveThreshold)
{
    if (!m_ready || !m_userEnabled)
        return;
    
    // Only log non-trivial draws
    if (magnitude < 0.01f)
        return;
    
    std::lock_guard<std::mutex> lock(m_mutex);
    
    m_stream << "[UI_DRAW] " << isoTimestamp()
             << " S" << stringIndex << "F" << fretIndex
             << " mag=" << std::fixed << std::setprecision(4) << magnitude
             << " thresh=" << std::setprecision(3) << threshold
             << " int=" << std::setprecision(2) << intensity
             << " alpha=" << alpha
             << " above=" << (isAboveThreshold ? 1 : 0)
             << "\n";
    m_stream.flush();
}

void HeatmapLogger::logUIBatchStart(std::uint64_t frameCount) {
    if (!m_ready || !m_userEnabled)
        return;
    
    std::lock_guard<std::mutex> lock(m_mutex);
    m_inBatch = true;
    m_currentBatchFrame = frameCount;
    m_batchEntryCount = 0;
    
    m_stream << "[UI_BATCH] " << isoTimestamp() << " frame=" << frameCount << "\n";
}

void HeatmapLogger::logUIBatchEntry(
    int stringIndex,
    int fretIndex,
    float magnitude,
    float threshold,
    float intensity,
    float alpha,
    bool isAboveThreshold)
{
    if (!m_ready || !m_userEnabled || !m_inBatch)
        return;
    
    // Only log entries with meaningful values
    if (magnitude < 0.01f)
        return;
    
    // No lock needed - we're in a batch (called from single thread)
    m_stream << "  S" << stringIndex << "F" << std::setw(2) << std::setfill(' ') << fretIndex
             << ": mag=" << std::fixed << std::setprecision(4) << magnitude
             << " thresh=" << std::setprecision(3) << threshold
             << " int=" << std::setprecision(2) << intensity
             << " alpha=" << alpha
             << (isAboveThreshold ? " [NOTE-ON]" : "")
             << "\n";
    
    ++m_batchEntryCount;
}

void HeatmapLogger::logUIBatchEnd() {
    if (!m_ready || !m_userEnabled || !m_inBatch)
        return;
    
    // No lock - same thread as batch start
    if (m_batchEntryCount == 0) {
        m_stream << "  (no active bins above noise floor)\n";
    }
    m_stream << "\n";
    m_stream.flush();
    
    m_inBatch = false;
    m_batchEntryCount = 0;
}
