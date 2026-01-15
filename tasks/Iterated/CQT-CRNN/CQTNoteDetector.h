#pragma once
#include <vector>

struct GuitarFrame {
    int stringID;           // 0-5
    int fret;               // 0-24 (stabilized)
    float rmsAmplitude;     // Existing RMS
    float centOffset;       // Sub-bin pitch (bends)
    float binMagnitude;     // fundamental energy
    float spectralFlux;     // CQT energy change
    bool isAttack;          // Triggered by flux spike
    bool isSustaining;      // Active note state
};

struct DetectionParams {
    float baseline;         // Noise floor baseline
    float envFloor;        // Minimum musical threshold
    float calibrationGain;  // Per-string gain multiplier
    float gateRatio;        // Onset multiplier
    float sustainScale;     // Note-off scale
};

class CQTNoteDetector {
public:
    CQTNoteDetector(double sampleRate = 44100.0);
    
    // Unified process for all 6 strings to handle crosstalk
    std::vector<GuitarFrame> process(float** hexBuffers, const std::vector<DetectionParams>& params);

private:
    const int BINS_PER_OCTAVE = 36;
    const float SLOPE_DECAY = 0.015f; 
    const int CONFIRMATION_FRAMES = 3;

    struct StringState {
        int currentFret = -1;
        int candidateFret = -1;
        int confirmationCount = 0;
        float lastBins[144] = {0.0f};
        float lastPeakMag = 0.0f;
    };
    StringState states[6];
};