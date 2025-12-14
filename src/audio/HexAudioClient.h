#pragma once

class TabEngineBridge;

class HexAudioClient {
public:
    virtual ~HexAudioClient() = default;
    virtual void setTabBridge(TabEngineBridge* bridge) = 0;
    virtual void connectMeters(TabEngineBridge* bridge) = 0;
    virtual void connectCalibration(TabEngineBridge* /*bridge*/) {}
    virtual void requestCalibration(int stringIndex = -1) { Q_UNUSED(stringIndex); }
};
