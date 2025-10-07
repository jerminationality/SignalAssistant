#include "AppController.h"

AppController::AppController(QObject* parent): QObject(parent) {
    // TODO(Copilot): Load persisted settings (sample rate, buffer size, last preset)
}

void AppController::setCurrentPreset(const QString& p) {
    if (p == m_currentPreset) return;
    m_currentPreset = p;
    emit currentPresetChanged();
    // TODO(Copilot): Push preset params to audio engine
}

QStringList AppController::availablePresets() const {
    // TODO(Copilot): scan preset directory; for now return mock list
    return {"Default", "Crunch", "Chime", "Lead"};
}

void AppController::savePreset(const QString& /*name*/) {
    // TODO(Copilot): Serialize chain + params + IR path to YAML/JSON
}

void AppController::loadPreset(const QString& name) {
    setCurrentPreset(name);
    // TODO(Copilot): Deserialize preset and push params to audio engine
}

void AppController::setBufferSize(int /*frames*/) {
    // TODO(Copilot): Forward to audio engine; recompute latency text and emit latencyTextChanged()
}

void AppController::setSampleRate(int /*sr*/) {
    // TODO(Copilot): Forward to audio engine; recompute latency text
}

void AppController::startAudio() {
    // TODO(Copilot): Start CarlaClient; display errors on failure
}

void AppController::stopAudio() {
    // TODO(Copilot): Stop CarlaClient cleanly
}
