#pragma once
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariant>

class AppController : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString currentPreset READ currentPreset WRITE setCurrentPreset NOTIFY currentPresetChanged)
    Q_PROPERTY(QString latencyText READ latencyText NOTIFY latencyTextChanged)
public:
    explicit AppController(QObject* parent=nullptr);

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

signals:
    void currentPresetChanged();
    void latencyTextChanged();

private:
    QString m_currentPreset {"Default"};
    QString m_latencyText {"—"};
};
