#pragma once

#include <string>
#include <vector>

enum class SessionInputMode {
    Live,
    Recorded
};

struct RunSessionOptions {
    SessionInputMode mode {SessionInputMode::Live};
    std::string sessionName;
    std::string sessionPath;
    double sessionDurationSec {0.0};
    std::vector<std::string> sessionSampleFiles;

    [[nodiscard]] bool isRecorded() const noexcept {
        return mode == SessionInputMode::Recorded;
    }
};
