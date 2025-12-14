#include "RecordedSessionPlayer.h"

#include "RunSessionOptions.h"
#include "TabEngineBridge.h"
#include "audio/JackMonitorSink.h"

#include <QAudioDevice>
#include <QAudioFormat>
#include <QAudioSink>
#include <QByteArray>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMediaDevices>
#include <QMutexLocker>
#include <QIODevice>
#include <QSet>
#include <QStringList>
#include <QThread>
#include <QtGlobal>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <cerrno>
#include <string>


namespace {
constexpr int kPlaybackReadFrames = 512;
constexpr int kDefaultChunkFrames = 128;
constexpr int kMinChunkFrames = 64;

QStringList defaultStringNames() {
    return {QStringLiteral("LowE"), QStringLiteral("A"), QStringLiteral("D"),
            QStringLiteral("G"), QStringLiteral("B"), QStringLiteral("HighE")};
}

QString normalizedStem(const QString& input) {
    QString stem = QFileInfo(input).completeBaseName();
    return stem.toLower();
}
}

class RecordedSessionPlayer::MonitorBuffer : public QIODevice {
public:
    MonitorBuffer() {
        open(QIODevice::ReadOnly);
    }

    qint64 readData(char* data, qint64 maxlen) override {
        if (!data || maxlen <= 0)
            return 0;

        QMutexLocker locker(&m_mutex);
        const qint64 available = std::min<qint64>(m_buffer.size(), maxlen);
        if (available > 0) {
            std::memcpy(data, m_buffer.constData(), static_cast<std::size_t>(available));
            m_buffer.remove(0, static_cast<int>(available));
        }

        if (available < maxlen) {
            const qint64 remaining = maxlen - available;
            std::memset(data + available, 0, static_cast<std::size_t>(remaining));
            return maxlen;
        }

        return available;
    }

    qint64 writeData(const char*, qint64) override { return 0; }

    void pushBytes(const char* data, int byteCount) {
        if (!data || byteCount <= 0)
            return;
        QMutexLocker locker(&m_mutex);
        m_buffer.append(data, byteCount);
        constexpr int kMaxFramesBuffered = 48000 * 2; // ~2 seconds at 48 kHz stereo
        const int maxBytes = kMaxFramesBuffered * static_cast<int>(sizeof(float)) * 2;
        if (m_buffer.size() > maxBytes)
            m_buffer.remove(0, m_buffer.size() - maxBytes);
    }

    void clear() {
        QMutexLocker locker(&m_mutex);
        m_buffer.clear();
    }

private:
    QByteArray m_buffer;
    QMutex m_mutex;
};


RecordedSessionPlayer::RecordedSessionPlayer(TabEngineBridge* bridge, QObject* parent)
    : QObject(parent)
    , m_bridge(bridge)
    , m_disableJackMonitor(qEnvironmentVariableIsSet("GUITARPI_DISABLE_JACK_MONITOR")) {
    m_debugLogging = qEnvironmentVariableIsSet("GUITARPI_TEST_LOG_NOTES");
    if (m_debugLogging)
        qInfo() << "RecordedPlayer" << "debug-logging" << "enabled";
}

RecordedSessionPlayer::~RecordedSessionPlayer() {
    stop();
    if (m_thread.joinable())
        m_thread.join();
    destroyMonitorSink();
    closeTracks();
}

void RecordedSessionPlayer::closeTracks() {
    QMutexLocker locker(&m_trackMutex);
    for (auto& track : m_tracks) {
        if (track.handle) {
            sf_close(track.handle);
            track.handle = nullptr;
        }
        track.buffer.clear();
        track.filePath.clear();
        track.info = {};
        track.totalFrames = 0;
        track.atEnd = false;
    }
}

void RecordedSessionPlayer::rewindAll() {
    QMutexLocker locker(&m_trackMutex);
    for (auto& track : m_tracks) {
        if (track.handle)
            sf_seek(track.handle, 0, SEEK_SET);
        track.atEnd = false;
    }
}

void RecordedSessionPlayer::setHexMonitorEnabled(bool enabled) {
    const bool previous = m_monitorEnabled.exchange(enabled, std::memory_order_acq_rel);
    if (previous == enabled)
        return;

    if (enabled) {
        if (!ensureMonitorSink()) {
            qWarning() << "RecordedPlayer" << "monitor" << "failed-to-init";
            m_monitorEnabled.store(false, std::memory_order_release);
        } else if (m_debugLogging) {
            qInfo() << "RecordedPlayer" << "monitor" << "enabled";
        }
    } else {
        destroyMonitorSink();
        if (m_debugLogging)
            qInfo() << "RecordedPlayer" << "monitor" << "disabled";
    }
}

double RecordedSessionPlayer::durationSec() const noexcept {
    if (m_sampleRate <= 0 || m_totalFrames <= 0)
        return 0.0;
    return static_cast<double>(m_totalFrames) / static_cast<double>(m_sampleRate);
}

double RecordedSessionPlayer::positionSec() const noexcept {
    const sf_count_t frames = m_positionFrames.load(std::memory_order_acquire);
    if (m_sampleRate <= 0 || frames <= 0)
        return 0.0;
    return static_cast<double>(frames) / static_cast<double>(m_sampleRate);
}

bool RecordedSessionPlayer::seekToSeconds(double seconds) {
    if (!m_ready || m_sampleRate <= 0 || seconds < 0.0)
        return false;

    const double clamped = std::clamp(seconds, 0.0, durationSec());
    const sf_count_t target = static_cast<sf_count_t>(clamped * static_cast<double>(m_sampleRate));

    {
        QMutexLocker locker(&m_trackMutex);
        for (auto& track : m_tracks) {
            if (!track.handle)
                continue;
            const sf_count_t capped = std::min(track.totalFrames, target);
            sf_seek(track.handle, capped, SEEK_SET);
            track.atEnd = false;
        }
    }

    m_positionFrames.store(std::min(target, m_totalFrames), std::memory_order_release);
    emit playbackProgress(positionSec(), durationSec());
    if (m_debugLogging) {
        qInfo() << "RecordedPlayer" << "seek" << QString::number(positionSec(), 'f', 3) << "sec";
    }
    return true;
}

bool RecordedSessionPlayer::seekToProgress(double normalized) {
    if (std::isnan(normalized) || std::isinf(normalized))
        return false;
    const double clamped = std::clamp(normalized, 0.0, 1.0);
    return seekToSeconds(durationSec() * clamped);
}

bool RecordedSessionPlayer::loadSession(const RunSessionOptions& options) {
    stop();
    if (m_thread.joinable())
        m_thread.join();
    closeTracks();

    m_ready = false;
    m_sampleRate = 0;
    m_totalFrames = 0;
    m_positionFrames.store(0, std::memory_order_release);

    if (options.sessionPath.empty()) {
        emit playbackError(QStringLiteral("Recorded session path is empty"));
        return false;
    }

    const QString sessionDir = QFileInfo(QString::fromStdString(options.sessionPath)).absoluteFilePath();
    QDir dir(sessionDir);
    if (!dir.exists()) {
        emit playbackError(QStringLiteral("Recorded session folder '%1' not found").arg(sessionDir));
        return false;
    }

    QStringList stringNames;
    QFile metadataFile(dir.filePath(QStringLiteral("metadata.json")));
    if (metadataFile.open(QIODevice::ReadOnly)) {
        const QJsonDocument doc = QJsonDocument::fromJson(metadataFile.readAll());
        metadataFile.close();
        if (doc.isObject()) {
            const QJsonArray array = doc.object().value(QStringLiteral("stringNames")).toArray();
            if (array.size() == 6) {
                for (const QJsonValue& value : array)
                    stringNames.append(value.toString());
            }
        }
    }

    if (stringNames.size() != 6)
        stringNames = defaultStringNames();

    QSet<QString> usedPaths;
    for (int stringIndex = 0; stringIndex < 6; ++stringIndex) {
        const QString preferred = stringNames[static_cast<std::size_t>(stringIndex)];
        QString filePath = resolveFileForString(stringIndex, preferred, options, sessionDir);
        if (filePath.isEmpty()) {
            emit playbackError(QStringLiteral("Missing WAV file for string %1 in '%2'").arg(stringIndex).arg(sessionDir));
            closeTracks();
            return false;
        }

        const QString canonical = QFileInfo(filePath).absoluteFilePath();
        if (usedPaths.contains(canonical)) {
            emit playbackError(QStringLiteral("Duplicate WAV mapping for '%1'").arg(canonical));
            closeTracks();
            return false;
        }
        usedPaths.insert(canonical);

        if (!openTrack(stringIndex, canonical)) {
            closeTracks();
            return false;
        }

        if (m_debugLogging) {
            qInfo() << "RecordedPlayer" << "track" << stringIndex
                    << QFileInfo(canonical).fileName()
                    << "frames" << m_tracks[static_cast<std::size_t>(stringIndex)].totalFrames
                    << "sr" << m_tracks[static_cast<std::size_t>(stringIndex)].info.samplerate;
        }
    }

    m_ready = (m_sampleRate > 0 && m_totalFrames > 0);
    if (!m_ready)
        emit playbackError(QStringLiteral("Recorded session has no audio data"));
    else if (m_debugLogging)
        qInfo() << "RecordedPlayer" << "loaded" << "sr" << m_sampleRate << "frames" << m_totalFrames;

    if (m_monitorEnabled.load(std::memory_order_acquire)) {
        destroyMonitorSink();
        ensureMonitorSink();
    }

    return m_ready;
}

QString RecordedSessionPlayer::resolveFileForString(int stringIndex,
                                                    const QString& preferredName,
                                                    const RunSessionOptions& options,
                                                    const QString& sessionDir) const {
    const QString trimmed = preferredName.trimmed();
    const QString lower = trimmed.toLower();
    QDir dir(sessionDir);

    auto candidateWithStem = [&](const QString& stem) -> QString {
        if (stem.isEmpty())
            return {};
        const QString lowerStem = stem.toLower();
        const QStringList suffixes {QStringLiteral(".wav"), QStringLiteral(".WAV")};
        for (const QString& suffix : suffixes) {
            const QString candidate = dir.filePath(stem + suffix);
            if (QFileInfo::exists(candidate))
                return QFileInfo(candidate).absoluteFilePath();
            const QString alt = dir.filePath(lowerStem + suffix);
            if (QFileInfo::exists(alt))
                return QFileInfo(alt).absoluteFilePath();
        }
        return {};
    };

    QString resolved = candidateWithStem(trimmed);
    if (resolved.isEmpty())
        resolved = candidateWithStem(lower);

    if (resolved.isEmpty()) {
        for (const std::string& rawPath : options.sessionSampleFiles) {
            const QString candidate = QFileInfo(QString::fromStdString(rawPath)).absoluteFilePath();
            if (candidate.isEmpty())
                continue;
            if (!lower.isEmpty() && normalizedStem(candidate).contains(lower)) {
                resolved = candidate;
                break;
            }
        }
    }

    if (resolved.isEmpty() && stringIndex < static_cast<int>(options.sessionSampleFiles.size())) {
        resolved = QFileInfo(QString::fromStdString(options.sessionSampleFiles[static_cast<std::size_t>(stringIndex)])).absoluteFilePath();
    }

    if (resolved.isEmpty()) {
        const QStringList wavFiles = dir.entryList(QStringList{QStringLiteral("*.wav"), QStringLiteral("*.WAV")}, QDir::Files, QDir::Name);
        if (stringIndex < wavFiles.size())
            resolved = dir.filePath(wavFiles[static_cast<std::size_t>(stringIndex)]);
    }

    return resolved;
}

bool RecordedSessionPlayer::openTrack(int stringIndex, const QString& filePath) {
    if (filePath.isEmpty())
        return false;

    Track& track = m_tracks[static_cast<std::size_t>(stringIndex)];
    track = Track{};
    track.filePath = filePath;
    track.buffer.resize(kPlaybackReadFrames, 0.f);

    QByteArray encoded = QFile::encodeName(filePath);
    track.handle = sf_open(encoded.constData(), SFM_READ, &track.info);
    if (!track.handle) {
        emit playbackError(QStringLiteral("Unable to open '%1' for playback").arg(filePath));
        return false;
    }

    if (track.info.channels != 1) {
        emit playbackError(QStringLiteral("Expected mono WAV for '%1'").arg(filePath));
        sf_close(track.handle);
        track.handle = nullptr;
        return false;
    }

    track.totalFrames = track.info.frames;
    track.atEnd = false;

    if (m_sampleRate == 0)
        m_sampleRate = track.info.samplerate;
    else if (track.info.samplerate != m_sampleRate) {
        emit playbackError(QStringLiteral("Sample rate mismatch in '%1'").arg(filePath));
        sf_close(track.handle);
        track.handle = nullptr;
        return false;
    }

    m_totalFrames = std::max(m_totalFrames, track.totalFrames);
    return true;
}

bool RecordedSessionPlayer::play() {
    if (!m_ready)
        return false;

    if (m_running.load(std::memory_order_acquire)) {
        if (m_paused.load(std::memory_order_acquire)) {
            {
                QMutexLocker locker(&m_pauseMutex);
                m_paused.store(false, std::memory_order_release);
            }
            m_pauseCond.wakeAll();
        }
        return true;
    }

    if (m_thread.joinable())
        m_thread.join();

    m_abort.store(false, std::memory_order_release);
    m_paused.store(false, std::memory_order_release);
    m_running.store(true, std::memory_order_release);
    m_thread = std::thread(&RecordedSessionPlayer::playbackLoop, this);
    if (m_debugLogging)
        qInfo() << "RecordedPlayer" << "play";
    return true;
}

void RecordedSessionPlayer::pause() {
    if (!m_running.load(std::memory_order_acquire))
        return;
    m_paused.store(true, std::memory_order_release);
    if (m_debugLogging)
        qInfo() << "RecordedPlayer" << "pause";
}

void RecordedSessionPlayer::stop() {
    if (m_running.load(std::memory_order_acquire)) {
        m_abort.store(true, std::memory_order_release);
        {
            QMutexLocker locker(&m_pauseMutex);
            m_paused.store(false, std::memory_order_release);
        }
        m_pauseCond.wakeAll();
        if (m_thread.joinable())
            m_thread.join();
        m_running.store(false, std::memory_order_release);
    } else if (m_thread.joinable()) {
        m_thread.join();
    }

    m_abort.store(false, std::memory_order_release);
    m_paused.store(false, std::memory_order_release);
    rewindAll();
    m_positionFrames.store(0, std::memory_order_release);
    {
        QMutexLocker locker(&m_monitorMutex);
        if (m_monitorBuffer)
            m_monitorBuffer->clear();
    }
    emit playbackProgress(positionSec(), durationSec());
    if (m_debugLogging)
        qInfo() << "RecordedPlayer" << "stop";
}

void RecordedSessionPlayer::waitWhilePaused() {
    QMutexLocker locker(&m_pauseMutex);
    while (m_paused.load(std::memory_order_acquire) && !m_abort.load(std::memory_order_acquire))
        m_pauseCond.wait(&m_pauseMutex, 50);
}

void RecordedSessionPlayer::playbackLoop() {
    std::array<std::vector<float>, 6> buffers;
    for (auto& buffer : buffers)
        buffer.assign(kPlaybackReadFrames, 0.f);

    rewindAll();
    m_positionFrames.store(0, std::memory_order_release);

    while (!m_abort.load(std::memory_order_acquire)) {
        waitWhilePaused();
        if (m_abort.load(std::memory_order_acquire))
            break;

        const auto loopStart = std::chrono::steady_clock::now();

        int framesThisBlock = 0;
        bool anyData = false;
        {
            QMutexLocker locker(&m_trackMutex);
            for (int i = 0; i < 6; ++i) {
                auto& track = m_tracks[static_cast<std::size_t>(i)];
                if (!track.handle) {
                    std::fill(buffers[static_cast<std::size_t>(i)].begin(), buffers[static_cast<std::size_t>(i)].end(), 0.f);
                    continue;
                }

                const sf_count_t read = sf_readf_float(track.handle,
                                                       buffers[static_cast<std::size_t>(i)].data(),
                                                       kPlaybackReadFrames);
                if (read < kPlaybackReadFrames)
                    std::fill(buffers[static_cast<std::size_t>(i)].begin() + static_cast<int>(read),
                              buffers[static_cast<std::size_t>(i)].end(),
                              0.f);
                if (read > 0) {
                    anyData = true;
                    framesThisBlock = std::max(framesThisBlock, static_cast<int>(read));
                }
            }
        }

        if (!anyData || framesThisBlock <= 0)
            break;

        if (m_debugLogging && framesThisBlock > 0) {
            QStringList rmsSummary;
            for (int i = 0; i < 6; ++i) {
                const float* data = buffers[static_cast<std::size_t>(i)].data();
                double sum = 0.0;
                for (int sample = 0; sample < framesThisBlock; ++sample) {
                    const double value = static_cast<double>(data[sample]);
                    sum += value * value;
                }
                const double rms = std::sqrt(sum / static_cast<double>(framesThisBlock));
                rmsSummary << QStringLiteral("s%1=%2").arg(i + 1).arg(rms, 0, 'f', 5);
            }
            qInfo() << "RecordedPlayer" << "block-rms" << rmsSummary.join(' ');
        }

        int chunkHint = m_bridge ? m_bridge->liveBlockFramesHint() : 0;
        if (chunkHint <= 0)
            chunkHint = kDefaultChunkFrames;
        chunkHint = std::clamp(chunkHint, kMinChunkFrames, kPlaybackReadFrames);

        const bool monitorActive = m_monitorEnabled.load(std::memory_order_acquire);
        int consumed = 0;
        while (consumed < framesThisBlock) {
            const int framesNow = std::min(chunkHint, framesThisBlock - consumed);
            const float* channelPtrs[6];
            for (int i = 0; i < 6; ++i)
                channelPtrs[i] = buffers[static_cast<std::size_t>(i)].data() + consumed;

            if (m_bridge)
                m_bridge->processLiveAudioBlock(channelPtrs, framesNow, static_cast<float>(m_sampleRate));

            if (monitorActive)
                pushMonitorBlock(channelPtrs, framesNow);

            consumed += framesNow;
        }

        const sf_count_t updated = std::min<sf_count_t>(m_positionFrames.load(std::memory_order_acquire) + framesThisBlock,
                                   m_totalFrames);
        m_positionFrames.store(updated, std::memory_order_release);
        emit playbackProgress(positionSec(), durationSec());
        if (m_debugLogging) {
            qInfo() << "RecordedPlayer" << "block"
                << "pos" << QString::number(positionSec(), 'f', 3)
                << "dur" << QString::number(durationSec(), 'f', 3)
                << "frames" << framesThisBlock;
        }

        const double blockMs = 1000.0 * static_cast<double>(framesThisBlock) / static_cast<double>(m_sampleRate);
        const double elapsedMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - loopStart).count();
        double remainingMs = blockMs - elapsedMs;
        if (remainingMs < 0.0)
            remainingMs = 0.0;

        while (remainingMs > 0.0 && !m_abort.load(std::memory_order_acquire) && !m_paused.load(std::memory_order_acquire)) {
            const double chunk = std::min(remainingMs, 5.0);
            if (chunk >= 1.0)
                QThread::msleep(static_cast<unsigned long>(std::round(chunk)));
            else
                QThread::usleep(static_cast<unsigned long>(chunk * 1000.0));
            remainingMs -= chunk;
        }
    }

    const bool completed = !m_abort.load(std::memory_order_acquire);
    m_running.store(false, std::memory_order_release);
    if (completed)
        emit playbackFinished();
    if (m_debugLogging)
        qInfo() << "RecordedPlayer" << (completed ? "finished" : "aborted");
}

bool RecordedSessionPlayer::ensureMonitorSink() {
    QMutexLocker locker(&m_monitorMutex);
    if (m_monitorBackend == MonitorBackend::Jack && m_jackMonitor)
        return true;
    if (m_monitorBackend == MonitorBackend::QtAudio && m_monitorSink)
        return true;

    destroyMonitorSinkLocked();

    if (jackMonitorAllowed() && initJackMonitorLocked())
        return true;

    if (initQtMonitorLocked())
        return true;

    qWarning() << "RecordedPlayer" << "monitor" << "backend-unavailable";
    return false;
}

void RecordedSessionPlayer::destroyMonitorSink() {
    QMutexLocker locker(&m_monitorMutex);
    destroyMonitorSinkLocked();
}

void RecordedSessionPlayer::destroyMonitorSinkLocked() {
    if (m_monitorSink) {
        m_monitorSink->stop();
        m_monitorSink.reset();
    }
    if (m_monitorBuffer)
        m_monitorBuffer.reset();
    if (m_jackMonitor) {
        m_jackMonitor->stop();
        m_jackMonitor.reset();
    }
    m_monitorFormat = QAudioFormat();
    m_monitorMixBuffer.clear();
    m_monitorByteBuffer.clear();
    m_monitorBackend = MonitorBackend::None;
}

bool RecordedSessionPlayer::initJackMonitorLocked() {
    if (m_sampleRate <= 0)
        return false;

    auto monitor = std::make_unique<JackMonitorSink>(QStringLiteral("RecordedPlayer"));
    if (!monitor->start(m_sampleRate))
        return false;

    m_jackMonitor = std::move(monitor);
    m_monitorBackend = MonitorBackend::Jack;
    m_monitorFormat = QAudioFormat();
    return true;
}

bool RecordedSessionPlayer::initQtMonitorLocked() {
    if (m_sampleRate <= 0)
        return false;

    auto matchesHint = [](const QAudioDevice& dev, const QStringList& hints) {
        const QString desc = dev.description();
        const QString idStr = QString::fromUtf8(dev.id());
        for (const QString& hint : hints) {
            if (desc.contains(hint, Qt::CaseInsensitive) || idStr.contains(hint, Qt::CaseInsensitive))
                return true;
        }
        return false;
    };

    auto selectOutputDevice = [&]() -> QAudioDevice {
        const QList<QAudioDevice> outputs = QMediaDevices::audioOutputs();
        if (outputs.isEmpty())
            return QAudioDevice{};

        const QByteArray filterRaw = qgetenv("GUITARPI_MONITOR_DEVICE");
        if (!filterRaw.isEmpty()) {
            const QString filter = QString::fromUtf8(filterRaw).trimmed();
            for (const QAudioDevice& dev : outputs) {
                if (dev.description().contains(filter, Qt::CaseInsensitive) ||
                    QString::fromUtf8(dev.id()).contains(filter, Qt::CaseInsensitive)) {
                    return dev;
                }
            }
            qWarning() << "RecordedPlayer" << "monitor" << "device-filter-not-found" << filter;
        }

        const QStringList scarlettHints {QStringLiteral("scarlett"), QStringLiteral("focusrite")};
        for (const QAudioDevice& dev : outputs) {
            if (matchesHint(dev, scarlettHints))
                return dev;
        }

        const QAudioDevice defaultDevice = QMediaDevices::defaultAudioOutput();
        if (!defaultDevice.isNull())
            return defaultDevice;
        return outputs.first();
    };

    const QAudioDevice outputDevice = selectOutputDevice();
    if (outputDevice.isNull()) {
        qWarning() << "RecordedPlayer" << "monitor" << "no-output-device";
        return false;
    }

    const QList<QAudioFormat::SampleFormat> preferredFormats {
        QAudioFormat::Float,
        QAudioFormat::Int16
    };

    QAudioFormat format;
    bool formatSelected = false;
    for (QAudioFormat::SampleFormat fmt : preferredFormats) {
        if (fmt == QAudioFormat::Unknown)
            continue;
        QAudioFormat candidate;
        candidate.setSampleRate(m_sampleRate);
        candidate.setChannelCount(2);
        candidate.setSampleFormat(fmt);
        if (!outputDevice.isFormatSupported(candidate))
            continue;
        format = candidate;
        formatSelected = true;
        break;
    }

    if (!formatSelected) {
        qWarning() << "RecordedPlayer" << "monitor" << "unsupported-format" << m_sampleRate;
        return false;
    }

    m_monitorBuffer = std::make_unique<MonitorBuffer>();
    m_monitorSink = std::make_unique<QAudioSink>(outputDevice, format);
    m_monitorSink->start(m_monitorBuffer.get());
    m_monitorFormat = format;
    m_monitorMixBuffer.clear();
    m_monitorByteBuffer.clear();
    m_monitorBackend = MonitorBackend::QtAudio;
    qInfo() << "RecordedPlayer" << "monitor" << "device" << outputDevice.description()
            << "format" << format.sampleRate() << "Hz"
            << (format.sampleFormat() == QAudioFormat::Float ? "float32" : "int16");
    return true;
}

bool RecordedSessionPlayer::jackMonitorAllowed() const noexcept {
    return !m_disableJackMonitor;
}

void RecordedSessionPlayer::pushMonitorBlock(const float* const channels[6], int frames) {
    if (!channels || frames <= 0)
        return;

    QMutexLocker locker(&m_monitorMutex);
    const MonitorBackend backend = m_monitorBackend;
    if (backend == MonitorBackend::None)
        return;

    m_monitorMixBuffer.resize(static_cast<std::size_t>(frames) * 2);
    for (int frame = 0; frame < frames; ++frame) {
        float sum = 0.f;
        for (int stringIndex = 0; stringIndex < 6; ++stringIndex) {
            const float sample = channels[stringIndex] ? channels[stringIndex][frame] : 0.f;
            sum += sample;
        }
        const float mono = (sum / 6.f) * m_monitorGain;
        m_monitorMixBuffer[static_cast<std::size_t>(frame) * 2] = mono;
        m_monitorMixBuffer[static_cast<std::size_t>(frame) * 2 + 1] = mono;
    }

    if (backend == MonitorBackend::Jack) {
        if (m_jackMonitor)
            m_jackMonitor->push(m_monitorMixBuffer.data(), frames);
        return;
    }

    if (!m_monitorBuffer || !m_monitorFormat.isValid())
        return;

    const QAudioFormat::SampleFormat fmt = m_monitorFormat.sampleFormat();
    if (fmt == QAudioFormat::Float) {
        const int byteCount = frames * 2 * static_cast<int>(sizeof(float));
        m_monitorBuffer->pushBytes(reinterpret_cast<const char*>(m_monitorMixBuffer.data()), byteCount);
        return;
    }

    if (fmt == QAudioFormat::Int16) {
        const int sampleCount = frames * 2;
        m_monitorByteBuffer.resize(sampleCount * static_cast<int>(sizeof(qint16)));
        auto* dst = reinterpret_cast<qint16*>(m_monitorByteBuffer.data());
        for (int i = 0; i < sampleCount; ++i) {
            const float clamped = std::clamp(m_monitorMixBuffer[static_cast<std::size_t>(i)], -1.f, 1.f);
            dst[i] = static_cast<qint16>(std::lrint(clamped * 32767.f));
        }
        m_monitorBuffer->pushBytes(m_monitorByteBuffer.data(), static_cast<int>(m_monitorByteBuffer.size()));
    }
}
