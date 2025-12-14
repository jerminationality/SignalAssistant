#include <iostream>
#include <vector>
#include <string>
#include "TabEngine.h"
#include "StringTracker.h"
#include "util.h"

// Simple functional test for the TabEngine module.
// This can be built separately using `make test_tab_module`.

int runTabModuleTest(int argc, char **argv) {
    if (argc != 7) {
        std::cerr << "Usage: test_tab_module e6.wav a5.wav d4.wav g3.wav b2.wav e1.wav\n";
        return 1;
    }

    std::vector<std::vector<float>> audio(6);
    float sr = 48000.0f;
    for (int i = 0; i < 6; ++i) {
        if (!loadWavMono(argv[i + 1], audio[i], sr)) {
            std::cerr << "Failed to load: " << argv[i + 1] << "\n";
            return 1;
        }
        std::cout << "Loaded " << argv[i + 1]
                  << " (" << audio[i].size() << " @ " << sr << " Hz)\n";
    }

    Tuning tuning;
    TrackerConfig cfg;
    TabEngine engine(tuning, cfg);

    const int blockSize = int(sr * cfg.hopSec);
    const float hopSec = float(blockSize) / sr;

    size_t maxSamples = 0;
    for (auto &ch : audio) maxSamples = std::max(maxSamples, ch.size());
    size_t nBlocks = maxSamples / size_t(blockSize);

    std::vector<const float*> ptrs(6, nullptr);
    for (size_t b = 0; b < nBlocks; ++b) {
        for (int s = 0; s < 6; ++s) {
            size_t off = b * size_t(blockSize);
            ptrs[s] = (off < audio[s].size()) ? (audio[s].data() + off) : nullptr;
        }
        engine.processBlock(ptrs.data(), blockSize, sr, float(b) * hopSec);
    }

    std::cout << engine.toJson(true) << std::endl;
    return 0;
}

// Provide a tiny standalone main() only when built as a separate target.
#ifdef BUILD_TAB_MODULE_TEST
int main(int argc, char **argv) {
    return runTabModuleTest(argc, argv);
}
#endif
