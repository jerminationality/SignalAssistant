#pragma once
#include <QObject>

class AudioEngine : public QObject {
    Q_OBJECT
public:
    explicit AudioEngine(QObject* parent=nullptr) : QObject(parent) {}
    virtual ~AudioEngine() = default;

    virtual bool start() = 0;              // RT thread(s) up, graph ready
    virtual void stop() = 0;               // tear down RT safely
    virtual void setBufferSize(int frames) = 0;
    virtual void setSampleRate(int sr) = 0;
};
