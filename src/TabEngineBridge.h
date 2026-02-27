#pragma once
#include <QObject>
#include <QVariantList>
#include <QElapsedTimer>
#include <QTimer>
#include <array>
#include <atomic>
#include <filesystem>
#include <memory>
#include <mutex>
#include <vector>

#include "TabEngine.h"

class HexAudioClient;

class TabEngineBridge : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantList events READ events NOTIFY eventsChanged)
    Q_PROPERTY(QString eventsJson READ eventsJson NOTIFY eventsChanged)
    Q_PROPERTY(bool recording READ recording WRITE setRecording NOTIFY recordingChanged)
    Q_PROPERTY(QVariantList hexMeters READ hexMeters NOTIFY hexMetersChanged)
    Q_PROPERTY(QVariantList rawMeters READ rawMeters NOTIFY rawMetersChanged)
    Q_PROPERTY(QVariantList thresholds READ thresholds NOTIFY thresholdsChanged)
    Q_PROPERTY(bool calibrationRunning READ calibrationRunning NOTIFY calibrationStatusChanged)
    Q_PROPERTY(QString calibrationMessage READ calibrationMessage NOTIFY calibrationStatusChanged)
    Q_PROPERTY(QVariantList calibrationSteps READ calibrationSteps NOTIFY calibrationStatusChanged)
    Q_PROPERTY(bool calibrationReady READ calibrationReady NOTIFY calibrationStatusChanged)
    Q_PROPERTY(bool tuningModeEnabled READ tuningModeEnabled WRITE setTuningModeEnabled NOTIFY tuningModeEnabledChanged)
    Q_PROPERTY(QVariantList tuningDeviation READ tuningDeviation NOTIFY tuningDeviationChanged)
    Q_PROPERTY(QVariantList calibrationGains READ calibrationGains NOTIFY calibrationGainsChanged)
    Q_PROPERTY(bool hardwareClipRisk READ hardwareClipRisk NOTIFY hardwareClipRiskChanged)
public:
    explicit TabEngineBridge(QObject* parent=nullptr);
    ~TabEngineBridge();

    QVariantList events() const { return m_events; }
    QString eventsJson() const { return m_eventsJson; }
    bool recording() const { return m_captureEnabled.load(std::memory_order_acquire); }
    QVariantList hexMeters() const { return m_hexMeters; }
    QVariantList rawMeters() const { return m_rawMeters; }
    QVariantList thresholds() const { return m_thresholds; }
    bool calibrationRunning() const { return m_calibrationRunning; }
    QString calibrationMessage() const { return m_calibrationMessage; }
    QVariantList calibrationSteps() const { return m_calibrationSteps; }
    bool calibrationReady() const { return m_calibrationProfile.valid; }
    bool tuningModeEnabled() const { return m_tuningModeEnabled; }
    QVariantList tuningDeviation() const;
    QVariantList calibrationGains() const;
    bool hardwareClipRisk() const { return m_hardwareClipRisk; }

    Q_INVOKABLE void requestRefresh();
    Q_INVOKABLE void clear();
    Q_INVOKABLE void seedMockSession();
    Q_INVOKABLE void setRecording(bool value);
    Q_INVOKABLE void startCalibration();
    Q_INVOKABLE void recalibrateString(int stringIndex);
    Q_INVOKABLE void cancelCalibration();
    Q_INVOKABLE void setTuningModeEnabled(bool enabled);
    Q_INVOKABLE void setCalibrationGain(int stringIndex, double gain);
    Q_INVOKABLE void updateCalibrationMultipliers();

    void setAudioClient(HexAudioClient* client);
    void getCalibrationMultipliers(std::array<float, 6>& multipliers) const;
    void processLiveAudioBlock(const float* const channels[6], int n, float sr);
    bool exportPendingCapture(const QString& label);
    bool hasPendingCapture() const { return m_pendingCaptureValid; }
    void discardPendingCapture();
    void updateThresholdsDisplay();

    TabEngine& engine() { return *m_engine; }
    const TabEngine& engine() const { return *m_engine; }
    TrackerConfig& trackerConfig() { return m_cfg; }
    const TrackerConfig& trackerConfig() const { return m_cfg; }
    int liveBlockFramesHint() const;

public slots:
    void updateLiveMeters(const std::array<float, 6>& meters);
    void handleCalibrationStarted();
    void handleCalibrationStepChanged(int stringIndex, bool capturing);
    void handleCalibrationFinished(const std::array<float, 6>& averages,
                                   const std::array<float, 6>& peaks);
    void handleCalibrationBaselineFloorCaptured(float noiseFloor);
    void handleCalibrationFadeComplete();

signals:
    void eventsChanged();
    void recordingChanged();
    void liveNoteTriggered(int stringIndex, int fretIndex, float velocity);
    void liveNoteEnded(int stringIndex, int fretIndex);
    void liveNoteEnvelopeUpdated(int stringIndex, float envelope);
    void hexMetersChanged();
    void rawMetersChanged();
    void thresholdsChanged();
    void calibrationStatusChanged();
    void tuningModeEnabledChanged();
    void tuningDeviationChanged();
    void calibrationGainsChanged();
    void hardwareClipRiskChanged();

private:
    void syncFromEngine();
    void resetCalibrationSteps();
    void setCalibrationStepState(int stringIdx, int state);
    void markSingleCalibrationPending(int stringIdx);
    QString calibrationStoragePath() const;
    void loadPersistentCalibration();
    void savePersistentCalibration() const;
    void appendCaptureAudio(const float* const channels[6], int n);
    void finalizeCaptureBuffers();
    void clearPendingCapture();
    void appendSessionWaveTap(const float* const channels[6], int n, float sr);
    void dumpSessionWaveSnapshot(const char* reason = nullptr);
    std::filesystem::path sessionWaveDirectory() const;
    QString stringNoteToken(int stringIdx) const;
    static QString sanitizeLabel(const QString& label);
    std::filesystem::path captureRootDirectory() const;
    bool writeWavFile(const std::filesystem::path& path,
                      const std::vector<float>& samples,
                      float sampleRate) const;
    double pendingCaptureDurationSec() const;

    Tuning m_tuning;
    TrackerConfig m_cfg;
    std::unique_ptr<TabEngine> m_engine;
    QVariantList m_events;
    QString m_eventsJson {"[]"};
    QVariantList m_hexMeters;
    QVariantList m_rawMeters;
    QVariantList m_thresholds;
    bool m_calibrationRunning {false};
    QString m_calibrationMessage {QStringLiteral("Uncalibrated")};
    CalibrationProfile m_calibrationProfile;
    QVariantList m_calibrationSteps;
    std::array<int, 6> m_calibrationStepStates {};
    QTimer* m_calibrationFadeTimer {nullptr};
    int m_activeCalibrationString {-1};
    bool m_activeCalibrationCapturing {false};
    bool m_calibrationLoaded {false};
    int m_requestedCalibrationString {-1};
    bool m_partialCalibration {false};
    float m_rawCalibrationNoiseFloor {0.f};  // raw RMS captured during noise phase
    // Indicates whether we are actively collecting a capture session; live detection
    // stays active regardless so overlays remain responsive when capture is off.
    std::atomic<bool> m_captureEnabled {false};
    std::atomic<bool> m_resetRequested {true};
    float m_liveTimeSec {0.f};
    float m_liveSampleRate {0.f};
    std::atomic<int> m_lastProcessBlockFrames {0};

    HexAudioClient* m_audioClient {nullptr};
    std::array<float, 6> m_lastLiveTriggerSec {};
    std::array<int, 6> m_lastLiveFret {};
    std::array<std::vector<float>, 6> m_captureBuffers;
    std::array<std::vector<float>, 6> m_pendingCaptureBuffers;
    std::array<std::vector<float>, 6> m_sessionWaveTap;
    std::array<std::size_t, 6> m_sessionWaveTapWriteIndex {};
    std::array<std::size_t, 6> m_sessionWaveTapCount {};
    std::size_t m_sessionWaveTapCapacity {0};
    float m_captureSampleRate {0.f};
    float m_pendingSampleRate {0.f};
    float m_sessionWaveTapSampleRate {0.f};
    bool m_sessionWaveTapDirty {false};
    bool m_pendingCaptureValid {false};
    QString m_pendingEventsJsonSnapshot;
    bool m_debugNoteLogging {false};
    bool m_externalMetersActive {false};
    bool m_tuningModeEnabled {false};
    bool m_hardwareClipRisk {false};
    std::array<float, 6> m_tuningDeviationCents {};
    QElapsedTimer m_thresholdsUpdateTimer;

    void postMeterSnapshot(const std::array<float, 6>& meters);
    void updateTuningDeviation();
};
