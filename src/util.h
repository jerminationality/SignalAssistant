#pragma once
#include <string>
#include <vector>

int   hzToMidi(float hz);
int   midiToFret(int midi, int openMidi);
float midiToHz(int midi);
float centsBetween(float hzA, float hzB);

// Simple RMS helper for envelope
float rms(const float* x, int n);

// WAV I/O (used only by the tab_module test harness)
bool loadWavMono(const std::string& path, std::vector<float>& out, float& sr);
