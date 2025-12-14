#pragma once
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariant>
#include <memory>
#include <QElapsedTimer>

#include "TabEngineBridge.h"
#include "RunSessionOptions.h"
#include "DetectionTuningController.h"

class CarlaClient;
class HexJackClient;
class RecordedSessionPlayer;

class AppController : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString currentPreset READ currentPreset WRITE setCurrentPreset NOTIFY currentPresetChanged)
    Q_PROPERTY(QString latencyText READ latencyText NOTIFY latencyTextChanged)
    Q_PROPERTY(bool testMode READ testMode NOTIFY testSessionChanged)
    Q_PROPERTY(QString testSessionName READ testSessionName NOTIFY testSessionChanged)
    Q_PROPERTY(QString testPlaybackState READ testPlaybackState NOTIFY testPlaybackChanged)
    Q_PROPERTY(qreal testPlaybackProgress READ testPlaybackProgress NOTIFY testPlaybackChanged)
    Q_PROPERTY(qreal testPlaybackDuration READ testPlaybackDuration NOTIFY testPlaybackChanged)
    Q_PROPERTY(qreal testPlaybackPosition READ testPlaybackPosition NOTIFY testPlaybackChanged)
    Q_PROPERTY(bool testHexAudioEnabled READ testHexAudioEnabled WRITE setTestHexAudioEnabled NOTIFY testPlaybackSettingsChanged)
    Q_PROPERTY(bool testLoopEnabled READ testLoopEnabled WRITE setTestLoopEnabled NOTIFY testPlaybackSettingsChanged)
    Q_PROPERTY(bool liveHexMonitorEnabled READ liveHexMonitorEnabled WRITE setLiveHexMonitorEnabled NOTIFY liveHexMonitorChanged)
    Q_PROPERTY(QObject* tabBridge READ tabBridgeObject CONSTANT)
    Q_PROPERTY(QObject* tuningController READ tuningControllerObject CONSTANT)
public:
    explicit AppController(const RunSessionOptions& options = RunSessionOptions{}, QObject* parent=nullptr);
    ~AppController() override;

    QString currentPreset() const { return m_currentPreset; }
    void setCurrentPreset(const QString& p);

    QString latencyText() const { return m_latencyText; }

    Q_INVOKABLE QStringList availablePresets() const;          // TODO(Copilot): populate from disk
    Q_INVOKABLE void savePreset(const QString& name);           // TODO(Copilot)
    Q_INVOKABLE void loadPreset(const QString& name);           // TODO(Copilot)
    Q_INVOKABLE void setBufferSize(int frames);                 // TODO(Copilot): plumb to audio engine
    Q_INVOKABLE void setSampleRate(int sr);                     // TODO(Copilot)
    Q_INVOKABLE void startAudio();                              // TODO(Copilot)
    Q_INVOKABLE void stopAudio();                               // TODO(Copilot)
    Q_INVOKABLE void toggleLiveRecording();
    Q_INVOKABLE void submitLiveRecordingLabel(const QString& label);
    Q_INVOKABLE void cancelLiveRecordingLabel();

    QObject* tabBridgeObject() { return &m_tabBridge; }
    const QObject* tabBridgeObject() const { return &m_tabBridge; }
    TabEngineBridge* tabBridge() { return &m_tabBridge; }
    const TabEngineBridge* tabBridge() const { return &m_tabBridge; }
    QObject* tuningControllerObject() { return &m_tuningController; }
    const QObject* tuningControllerObject() const { return &m_tuningController; }
    DetectionTuningController* tuningController() { return &m_tuningController; }
    const DetectionTuningController* tuningController() const { return &m_tuningController; }

    bool testMode() const { return m_runOptions.isRecorded(); }
    QString testSessionName() const { return m_testSessionName; }
    QString testPlaybackState() const { return m_testPlaybackState; }
    qreal testPlaybackProgress() const { return m_testPlaybackProgress; }
    qreal testPlaybackDuration() const { return m_testPlaybackDuration; }
    qreal testPlaybackPosition() const { return m_testPlaybackPosition; }
    bool testHexAudioEnabled() const { return m_testHexAudioEnabled; }
    bool testLoopEnabled() const { return m_testLoopEnabled; }
    bool liveHexMonitorEnabled() const { return m_liveHexMonitorEnabled; }

    Q_INVOKABLE void testPlay();
    Q_INVOKABLE void testPause();
    Q_INVOKABLE void testStop();
    Q_INVOKABLE void testTogglePlayPause();
    Q_INVOKABLE void setTestHexAudioEnabled(bool enabled);
    Q_INVOKABLE void testSeekToProgress(qreal normalized);
    Q_INVOKABLE void setTestLoopEnabled(bool enabled);
    Q_INVOKABLE void setLiveHexMonitorEnabled(bool enabled);

signals:
    void currentPresetChanged();
    void latencyTextChanged();
    void testSessionChanged();
    void testPlaybackChanged();
    void testPlaybackSettingsChanged();
    void liveRecordingLabelRequested();
    void liveHexMonitorChanged();

private:
    bool ensureAudioClient();
    void updateLatencyText(int sampleRate, int bufferSize);
    void initializeTestPlayback();
    void emitTestPlaybackChanged();
    void logTestAction(const char* action) const;
    void handleRecordedProgress(double positionSec, double durationSec);
    void handleRecordedFinished();
    void handleRecordedError(const QString& description);
    bool autoTestPlayEnabled() const;

    QString m_currentPreset {"Default"};
    QString m_latencyText {"—"};
    std::unique_ptr<CarlaClient> m_audioClient;
    std::unique_ptr<HexJackClient> m_hexClient;
    int m_requestedBufferSize {0};
    int m_requestedSampleRate {0};
    int m_activeBufferSize {0};
    int m_activeSampleRate {0};
    bool m_audioRunning {false};
    bool m_hexRunning {false};
    TabEngineBridge m_tabBridge;
    DetectionTuningController m_tuningController;
    RunSessionOptions m_runOptions;
    QString m_testSessionName;
    QString m_testPlaybackState {QStringLiteral("Stopped")};
    qreal m_testPlaybackProgress {0.0};
    qreal m_testPlaybackDuration {0.0};
    qreal m_testPlaybackPosition {0.0};
    bool m_testPlaying {false};
    bool m_testHexAudioEnabled {false};
    bool m_testLoopEnabled {false};
    bool m_liveHexMonitorEnabled {false};
    std::unique_ptr<RecordedSessionPlayer> m_recordedPlayer;
    QElapsedTimer m_liveRecordingTimer;
    qreal m_lastLiveRecordingDuration {0.0};
    bool m_liveRecordingAwaitingLabel {false};
    bool m_autoTestPlaybackTriggered {false};
};
