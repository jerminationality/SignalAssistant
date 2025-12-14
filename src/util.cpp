#include "util.h"
#include <cmath>
#include <sndfile.h>
#include <algorithm>

int hzToMidi(float hz) {
  if (hz <= 0.f) return -1;
  return int(std::round(69.0 + 12.0 * std::log2(hz / 440.0)));
}

int midiToFret(int midi, int openMidi) {
  return midi - openMidi;
}

float midiToHz(int midi) {
  return 440.0f * std::pow(2.0f, (float(midi) - 69.0f) / 12.0f);
}

float centsBetween(float hzA, float hzB) {
  if (hzA <= 0.f || hzB <= 0.f) return 0.f;
  return 1200.f * std::log2(hzA / hzB);
}

float rms(const float* x, int n) {
  if (!x || n <= 0) return 0.f;
  double s = 0.0;
  for (int i = 0; i < n; ++i) s += double(x[i]) * double(x[i]);
  return float(std::sqrt(s / std::max(1, n)));
}

bool loadWavMono(const std::string &path, std::vector<float> &out, float &sr) {
  SF_INFO info{};
  SNDFILE* sf = sf_open(path.c_str(), SFM_READ, &info);
  if (!sf) return false;
  sr = float(info.samplerate);
  std::vector<float> tmp(size_t(info.frames) * size_t(info.channels));
  sf_readf_float(sf, tmp.data(), info.frames);
  sf_close(sf);

  out.resize(info.frames);
  if (info.channels == 1) {
    std::copy(tmp.begin(), tmp.end(), out.begin());
  } else {
    for (int i = 0; i < info.frames; ++i) {
      double sum = 0.0;
      for (int c = 0; c < info.channels; ++c)
        sum += tmp[size_t(i) * size_t(info.channels) + size_t(c)];
      out[i] = float(sum / info.channels);
    }
  }
  return true;
}
