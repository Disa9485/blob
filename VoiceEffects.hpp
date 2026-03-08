#pragma once

#include "AppConfig.hpp"

#include <vector>

namespace VoiceEffects {
    void applyWarp(
        std::vector<float>& samples,
        int sample_rate,
        const TtsWarpConfig& config
    );
}