#pragma once
#include "AudioEngine.h"
#include <QObject>

// TODO(Copilot): Implement Carla graph control via JACK/MIDI/OSC as needed
class CarlaClient : public AudioEngine {
    Q_OBJECT
public:
    explicit CarlaClient(QObject* parent=nullptr) : AudioEngine(parent) {}
    bool start() override;        // TODO(Copilot)
    void stop() override;         // TODO(Copilot)
    void setBufferSize(int frames) override; // TODO(Copilot)
    void setSampleRate(int sr) override;     // TODO(Copilot)

signals:
    void xrunsChanged(int count);
    void metersChanged(float inL, float inR, float outL, float outR); // poll or subscribe
};
