#include <QByteArray>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQmlError>
#include <QDebug>
#include <QStringList>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cctype>
#include <csignal>
#include <cstdint>
#include <ctime>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <sstream>
#include <string>
#include <vector>

#include "AppController.h"
#include "RunSessionOptions.h"
#include "SessionLogger.h"

using namespace Qt::StringLiterals;

namespace {

constexpr const char* kRecordedFolderRoot = "sessions";
constexpr const char* kLegacyRecordedSessionDir = "samples/offline_inputs";
constexpr int kStringsPerSession = 6;
std::filesystem::path resolveExecutableDir(const char* argv0) {
    namespace fs = std::filesystem;
    if (!argv0 || argv0[0] == '\0')
        return {};

    std::error_code ec;
    fs::path path(argv0);
    if (!path.is_absolute()) {
        const auto cwd = fs::current_path(ec);
        if (!ec)
            path = cwd / path;
        ec.clear();
    }

    fs::path canonicalPath = fs::weakly_canonical(path, ec);
    if (ec) {
        ec.clear();
        canonicalPath = fs::absolute(path, ec);
        ec.clear();
    }

    if (canonicalPath.empty())
        return {};

    if (fs::is_directory(canonicalPath, ec)) {
        ec.clear();
        return canonicalPath;
    }
    ec.clear();
    return canonicalPath.parent_path();
}

std::vector<std::filesystem::path> buildSessionBaseCandidates(const std::filesystem::path& executableDir) {
    namespace fs = std::filesystem;
    std::vector<fs::path> bases;
    auto pushUnique = [&](const fs::path& candidate) {
        if (candidate.empty())
            return;
        std::error_code ec;
        fs::path normalized = fs::weakly_canonical(candidate, ec);
        if (ec) {
            ec.clear();
            normalized = fs::absolute(candidate, ec);
            ec.clear();
        }
        if (normalized.empty())
            return;
        if (std::find(bases.begin(), bases.end(), normalized) == bases.end())
            bases.push_back(normalized);
    };

    pushUnique(fs::current_path());
    if (!executableDir.empty()) {
        auto dir = executableDir;
        for (int i = 0; i < 6 && !dir.empty(); ++i) {
            pushUnique(dir);
            dir = dir.parent_path();
        }
    }

    if (bases.empty())
        bases.emplace_back(fs::current_path());
    return bases;
}

std::filesystem::path detectRepositoryRoot(const std::filesystem::path& startDir) {
    namespace fs = std::filesystem;
    fs::path dir = startDir;
    for (int i = 0; i < 8 && !dir.empty(); ++i) {
        std::error_code ec;
        const bool hasCapture = fs::exists(dir / "capture.sh", ec);
        ec.clear();
        const bool hasGit = fs::exists(dir / ".git", ec);
        ec.clear();
        const bool hasCmake = fs::exists(dir / "CMakeLists.txt", ec);
        ec.clear();
        if (hasCapture || hasGit || hasCmake)
            return dir;
        dir = dir.parent_path();
    }
    return startDir;
}

std::filesystem::path resolveLiveStartupLogPath(const std::filesystem::path& executableDir) {
    namespace fs = std::filesystem;
    fs::path base = executableDir;
    if (base.empty()) {
        std::error_code ec;
        base = fs::current_path(ec);
    }
    const fs::path repoRoot = detectRepositoryRoot(base);
    if (repoRoot.empty())
        return {};
    return repoRoot / "live-startup.log";
}

std::string formatTimestamp() {
    using clock = std::chrono::system_clock;
    const auto now = clock::now();
    const std::time_t nowTime = clock::to_time_t(now);
    std::tm tm {};
#if defined(_WIN32)
    localtime_s(&tm, &nowTime);
#else
    localtime_r(&nowTime, &tm);
#endif
    std::ostringstream oss;
    oss << std::put_time(&tm, "%Y-%m-%d %H:%M:%S");
    return oss.str();
}

struct LogFileState {
    std::mutex mutex;
    std::ofstream stream;
};

LogFileState& logFileState() {
    static LogFileState state;
    return state;
}

class ScopedSigintHandler {
public:
    explicit ScopedSigintHandler(QGuiApplication& app) {
        if (g_handlerInstalled.exchange(true))
            return;
        s_app = &app;
        s_previous = std::signal(SIGINT, &ScopedSigintHandler::handleSignal);
    }

    ~ScopedSigintHandler() {
        s_app = nullptr;
        if (g_handlerInstalled.exchange(false))
            std::signal(SIGINT, s_previous);
    }

private:
    static void handleSignal(int sig) {
        if (sig != SIGINT)
            return;
        if (s_app)
            QMetaObject::invokeMethod(s_app, &QCoreApplication::quit, Qt::QueuedConnection);
    }

    static inline std::atomic<bool> g_handlerInstalled {false};
    static inline QGuiApplication* s_app {nullptr};
    static inline __sighandler_t s_previous {SIG_DFL};
};

void appendLogEntry(const char* level,
                    const QMessageLogContext& ctx,
                    const QByteArray& message) {
    LogFileState& state = logFileState();
    if (!state.stream.is_open())
        return;
    std::lock_guard<std::mutex> lock(state.mutex);
    state.stream << formatTimestamp() << " [" << level << "] "
                 << '(' << (ctx.file ? ctx.file : "?")
                 << ':' << ctx.line << ','
                 << (ctx.function ? ctx.function : "?")
                 << ") " << message.constData() << '\n';
    state.stream.flush();
}

void ensureDefaultMediaBackend() {
    constexpr const char* kBackendEnv = "QT_MEDIA_BACKEND";
    if (qEnvironmentVariableIsSet(kBackendEnv)) {
        qInfo() << "startup" << "qt-media-backend" << qgetenv(kBackendEnv);
        return;
    }
    const QByteArray backend("alsa");
    qputenv(kBackendEnv, backend);
    qInfo() << "startup" << "qt-media-backend" << backend << "(forced default)";
}


struct RecordedSessionEntry {
    std::filesystem::path location;
    std::vector<std::filesystem::path> sampleFiles;
    std::string displayLabel;
    double durationSec {0.0};
    bool isFolder {true};
};

uint32_t readLe32(const char* data) {
    return static_cast<uint32_t>(static_cast<unsigned char>(data[0])) |
           (static_cast<uint32_t>(static_cast<unsigned char>(data[1])) << 8U) |
           (static_cast<uint32_t>(static_cast<unsigned char>(data[2])) << 16U) |
           (static_cast<uint32_t>(static_cast<unsigned char>(data[3])) << 24U);
}

uint16_t readLe16(const char* data) {
    const auto b0 = static_cast<uint16_t>(static_cast<unsigned char>(data[0]));
    const auto b1 = static_cast<uint16_t>(static_cast<unsigned char>(data[1]));
    return static_cast<uint16_t>(b0 | (b1 << 8U));
}

double readWavDuration(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file)
        return 0.0;

    char id[4];
    file.read(id, 4);
    if (file.gcount() != 4 || std::string(id, 4) != "RIFF")
        return 0.0;

    file.seekg(4, std::ios::cur); // skip chunk size
    file.read(id, 4);
    if (file.gcount() != 4 || std::string(id, 4) != "WAVE")
        return 0.0;

    bool fmtFound = false;
    bool dataFound = false;
    uint32_t sampleRate = 0;
    uint16_t blockAlign = 0;
    uint32_t dataSize = 0;

    while (file && !(fmtFound && dataFound)) {
        file.read(id, 4);
        if (file.gcount() != 4)
            break;
        char sizeBytes[4];
        file.read(sizeBytes, 4);
        if (file.gcount() != 4)
            break;
        const uint32_t chunkSize = readLe32(sizeBytes);
        std::streampos nextChunk = file.tellg();
        nextChunk += static_cast<std::streamoff>(chunkSize);

        if (std::string(id, 4) == "fmt ") {
            std::vector<char> fmtData(chunkSize);
            file.read(fmtData.data(), chunkSize);
            if (file.gcount() == static_cast<std::streamsize>(chunkSize) && chunkSize >= 16) {
                sampleRate = readLe32(fmtData.data() + 4);
                blockAlign = readLe16(fmtData.data() + 12);
                fmtFound = true;
            }
        } else if (std::string(id, 4) == "data") {
            dataSize = chunkSize;
            file.seekg(chunkSize, std::ios::cur);
            dataFound = true;
        } else {
            file.seekg(chunkSize, std::ios::cur);
        }

        file.seekg(nextChunk);
    }

    if (!fmtFound || !dataFound || sampleRate == 0 || blockAlign == 0)
        return 0.0;

    const double bytesPerSecond = static_cast<double>(sampleRate) * static_cast<double>(blockAlign);
    if (bytesPerSecond <= 0.0)
        return 0.0;
    return static_cast<double>(dataSize) / bytesPerSecond;
}

std::string formatDurationShort(double seconds) {
    if (seconds <= 0.0)
        return {};

    std::ostringstream oss;
    oss << std::fixed << std::setprecision(seconds >= 10.0 ? 1 : 2) << seconds;
    return oss.str();
}

bool hasWavExtension(const std::filesystem::path& path) {
    std::string ext = path.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return ext == ".wav";
}

std::vector<std::filesystem::path> listSessionSamples(const std::filesystem::path& dir) {
    namespace fs = std::filesystem;
    std::vector<fs::path> wavs;
    std::error_code ec;
    for (const auto& entry : fs::directory_iterator(dir, ec)) {
        if (ec) {
            ec.clear();
            continue;
        }
        if (!entry.is_regular_file(ec)) {
            ec.clear();
            continue;
        }
        if (hasWavExtension(entry.path()))
            wavs.push_back(entry.path());
    }
    std::sort(wavs.begin(), wavs.end());
    return wavs;
}

double maxSampleDuration(const std::vector<std::filesystem::path>& files) {
    double longest = 0.0;
    for (const auto& file : files)
        longest = std::max(longest, readWavDuration(file));
    return longest;
}

std::string makeSessionLabel(const std::filesystem::path& root,
                             const std::filesystem::path& folder) {
    std::error_code ec;
    const auto rel = std::filesystem::relative(folder, root, ec);
    std::string label;
    if (!ec)
        label = rel.generic_string();
    if (label.rfind("live/", 0) == 0)
        label.erase(0, std::string("live/").size());
    if (label.empty())
        label = folder.filename().string();
    if (label.empty())
        label = folder.string();
    return label;
}

std::vector<RecordedSessionEntry> collectFolderSessions(const std::filesystem::path& root) {
    namespace fs = std::filesystem;
    std::vector<RecordedSessionEntry> sessions;
    std::error_code ec;
    if (!fs::exists(root, ec) || !fs::is_directory(root, ec))
        return sessions;

    fs::recursive_directory_iterator it(root,
                                        fs::directory_options::skip_permission_denied,
                                        ec);
    if (ec)
        return sessions;

    fs::recursive_directory_iterator end;
    for (; it != end; it.increment(ec)) {
        if (ec) {
            ec.clear();
            continue;
        }
        const auto& entry = *it;
        if (it.depth() == 0)
            continue;

        std::error_code dirEc;
        if (!entry.is_directory(dirEc)) {
            dirEc.clear();
            continue;
        }

        auto wavs = listSessionSamples(entry.path());
        if (static_cast<int>(wavs.size()) < kStringsPerSession)
            continue;

        RecordedSessionEntry session;
        session.location = entry.path();
        session.sampleFiles = std::move(wavs);
        session.durationSec = maxSampleDuration(session.sampleFiles);
        session.displayLabel = makeSessionLabel(root, session.location);
        session.isFolder = true;
        sessions.emplace_back(std::move(session));
    }

    std::sort(sessions.begin(), sessions.end(), [](const RecordedSessionEntry& a,
                                                   const RecordedSessionEntry& b) {
        return a.displayLabel < b.displayLabel;
    });
    return sessions;
}

std::vector<RecordedSessionEntry> collectLegacySessions(const std::filesystem::path& dir) {
    namespace fs = std::filesystem;
    std::vector<RecordedSessionEntry> sessions;
    std::error_code ec;
    if (!fs::exists(dir, ec) || !fs::is_directory(dir, ec))
        return sessions;

    for (const auto& entry : fs::directory_iterator(dir, ec)) {
        if (ec) {
            ec.clear();
            continue;
        }
        if (!entry.is_regular_file(ec)) {
            ec.clear();
            continue;
        }
        if (!hasWavExtension(entry.path()))
            continue;

        RecordedSessionEntry session;
        session.location = entry.path();
        session.sampleFiles = {entry.path()};
        session.durationSec = readWavDuration(entry.path());
        session.displayLabel = entry.path().filename().string();
        session.isFolder = false;
        sessions.emplace_back(std::move(session));
    }

    std::sort(sessions.begin(), sessions.end(), [](const RecordedSessionEntry& a,
                                                   const RecordedSessionEntry& b) {
        return a.displayLabel < b.displayLabel;
    });
    return sessions;
}

std::vector<RecordedSessionEntry> collectRecordedSessions(const std::vector<std::filesystem::path>& baseCandidates) {
    namespace fs = std::filesystem;
    std::error_code ec;

    for (const auto& base : baseCandidates) {
        if (base.empty())
            continue;
        const auto root = base / kRecordedFolderRoot;
        if (fs::exists(root, ec) && fs::is_directory(root, ec)) {
            auto sessions = collectFolderSessions(root);
            if (!sessions.empty())
                return sessions;
        }
        ec.clear();
    }

    for (const auto& base : baseCandidates) {
        if (base.empty())
            continue;
        const auto legacy = base / kLegacyRecordedSessionDir;
        if (fs::exists(legacy, ec) && fs::is_directory(legacy, ec)) {
            auto sessions = collectLegacySessions(legacy);
            if (!sessions.empty())
                return sessions;
        }
        ec.clear();
    }

    return {};
}

int promptForInt(const std::string& prompt) {
    while (true) {
        std::cout << prompt;
        std::string line;
        if (!std::getline(std::cin, line))
            return 0;
        try {
            int value = std::stoi(line);
            return value;
        } catch (const std::exception&) {
            std::cout << "Invalid input. Please enter a number.\n";
        }
    }
}

RunSessionOptions promptRunOptions(const std::vector<std::filesystem::path>& sessionBaseCandidates) {
    RunSessionOptions options;
    std::cout << "Select input mode:\n";
    std::cout << "  1) Live hex input\n";
    std::cout << "  2) Recorded session test\n";

    int mode = promptForInt("> ");
    if (mode != 2)
        return options;

    auto sessions = collectRecordedSessions(sessionBaseCandidates);
    if (sessions.empty()) {
        std::cout << "No recorded sessions found under '" << kRecordedFolderRoot
                  << "' or legacy folder '" << kLegacyRecordedSessionDir << "'. Continuing in live mode.\n";
        return options;
    }

    std::cout << "Available recorded sessions:\n";
    for (std::size_t i = 0; i < sessions.size(); ++i) {
        const auto& session = sessions[i];
        std::cout << "  " << (i + 1) << ") " << session.displayLabel
                  << " [" << session.sampleFiles.size() << " samples]";
        const auto durationStr = formatDurationShort(session.durationSec);
        if (!durationStr.empty())
            std::cout << " (" << durationStr << " sec)";
        if (!session.isFolder)
            std::cout << " [legacy single file]";
        std::cout << '\n';
    }

    int selection = 0;
    while (selection < 1 || selection > static_cast<int>(sessions.size())) {
        selection = promptForInt("Choose session number: ");
        if (selection < 1 || selection > static_cast<int>(sessions.size()))
            std::cout << "Selection out of range.\n";
    }

    const auto& chosen = sessions[static_cast<std::size_t>(selection - 1)];
    options.mode = SessionInputMode::Recorded;
    options.sessionPath = chosen.location.string();
    options.sessionName = chosen.displayLabel;
    options.sessionDurationSec = chosen.durationSec;
    options.sessionSampleFiles.clear();
    options.sessionSampleFiles.reserve(chosen.sampleFiles.size());
    for (const auto& file : chosen.sampleFiles)
        options.sessionSampleFiles.emplace_back(file.string());
    if (options.sessionDurationSec <= 0.0)
        options.sessionDurationSec = 0.0;

    std::cout << "Loaded recorded session: " << options.sessionName;
    if (options.sessionDurationSec > 0.0)
        std::cout << " (" << options.sessionDurationSec << " sec)";
    std::cout << " with " << options.sessionSampleFiles.size() << " samples";
    std::cout << "\n";
    return options;
}

void logRunOptions(const RunSessionOptions& options) {
    auto& logger = SessionLogger::instance();
    const char* mode = options.isRecorded() ? "recorded" : "live";
    logger.logf("session",
                "mode=%s session='%s' path='%s' duration=%.2f files=%zu",
                mode,
                options.sessionName.c_str(),
                options.sessionPath.c_str(),
                options.sessionDurationSec,
                static_cast<unsigned long long>(options.sessionSampleFiles.size()));
}

void installMessageHandler(const std::filesystem::path& logFile) {
    if (!logFile.empty()) {
        auto& state = logFileState();
        std::error_code ec;
        const auto parent = logFile.parent_path();
        if (!parent.empty())
            std::filesystem::create_directories(parent, ec);
        state.stream.open(logFile, std::ios::out | std::ios::trunc);
        if (!state.stream)
            std::cerr << "Failed to open log file '" << logFile.string() << "' for writing.\n";
    }

    static const auto handler = [](QtMsgType type, const QMessageLogContext& ctx, const QString& msg) {
        static const QStringList suppressedFragments = {
            QStringLiteral("Qt6CTPlatformTheme::palette")
        };

        for (const QString& fragment : suppressedFragments) {
            if (msg.contains(fragment))
                return; // skip noisy theme debug spam
        }

        QByteArray localMsg = msg.toLocal8Bit();
        const char* level = "info";
        switch (type) {
        case QtDebugMsg: level = "debug"; break;
        case QtInfoMsg: level = "info"; break;
        case QtWarningMsg: level = "warning"; break;
        case QtCriticalMsg: level = "critical"; break;
        case QtFatalMsg: level = "fatal"; break;
        }
        fprintf(stderr, "qtmsg [%s] (%s:%u,%s): %s\n",
                level,
                ctx.file ? ctx.file : "?",
                ctx.line,
                ctx.function ? ctx.function : "?",
                localMsg.constData());
        appendLogEntry(level, ctx, localMsg);
        if (type == QtFatalMsg)
            abort();
    };
    qInstallMessageHandler(handler);
}

}

int main(int argc, char *argv[]) {
    ensureDefaultMediaBackend();
    const std::filesystem::path executableDir = resolveExecutableDir(argc > 0 ? argv[0] : nullptr);
    const std::filesystem::path liveStartupLog = resolveLiveStartupLogPath(executableDir);
    installMessageHandler(liveStartupLog);
    const auto sessionBaseCandidates = buildSessionBaseCandidates(executableDir);
    const RunSessionOptions runOptions = promptRunOptions(sessionBaseCandidates);
    logRunOptions(runOptions);
    qInfo() << "startup" << "prompt-complete"
            << (runOptions.isRecorded() ? "recorded" : "live")
            << QString::fromStdString(runOptions.sessionName);

    qInfo() << "startup" << "creating-qguiapplication";
    QGuiApplication app(argc, argv);
    qInfo() << "startup" << "qguiapplication-ready";

    QQmlApplicationEngine engine;
    QObject::connect(&engine, &QQmlEngine::warnings, &engine, [](const QList<QQmlError>& warnings) {
        for (const QQmlError& warning : warnings)
            qWarning().noquote() << "qml-warning" << warning.toString();
    });
        ScopedSigintHandler sigintGuard(app);
    qInfo() << "startup" << "constructing-appcontroller";
    AppController controller(runOptions);
    qInfo() << "startup" << "appcontroller-ready";

    engine.rootContext()->setContextProperty("AppController", &controller);
    engine.rootContext()->setContextProperty("TabBridge", controller.tabBridge());
    const QUrl url(u"qrc:/qt/qml/GuitarPi/qml/Main.qml"_s);
    QObject::connect(&engine, &QQmlApplicationEngine::objectCreated, &app,
                     [url](QObject *obj, const QUrl &objUrl) {
                         if (!obj && url == objUrl) {
                             qCritical() << "startup" << "qml-object-create-failed" << objUrl;
                             QCoreApplication::exit(-1);
                             return;
                         }
                         qInfo() << "startup" << "qml-root-object-created" << objUrl;
                     }, Qt::QueuedConnection);
    qInfo() << "startup" << "qml-engine-load" << url;
    engine.load(url);
    qInfo() << "startup" << "qml-engine-load-complete" << engine.rootObjects().size();

    return app.exec();
}
