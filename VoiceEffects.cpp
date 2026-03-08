#include "VoiceEffects.hpp"

#include <algorithm>
#include <cmath>
#include <vector>

namespace {
    constexpr float kPi = 3.14159265358979323846f;

    float clamp1(float x) {
        return std::max(-1.0f, std::min(1.0f, x));
    }

    void applyLowerSentenceEnd(std::vector<float>& samples, int sample_rate, int tail_ms, float drop) {
        if (samples.empty() || sample_rate <= 0 || tail_ms <= 0 || drop <= 0.0f) {
            return;
        }

        const int tail_samples = static_cast<int>((sample_rate * tail_ms) / 1000.0f);
        if (tail_samples <= 1 || static_cast<int>(samples.size()) < tail_samples) {
            return;
        }

        const int head_count = static_cast<int>(samples.size()) - tail_samples;
        std::vector<float> tail(samples.begin() + head_count, samples.end());

        std::vector<float> lowered;
        lowered.reserve(static_cast<std::size_t>(tail.size() / std::max(drop, 0.01f)) + 2);

        for (float src = 0.0f; src < static_cast<float>(tail.size() - 1); src += drop) {
            const int i0 = static_cast<int>(src);
            const int i1 = std::min(i0 + 1, static_cast<int>(tail.size()) - 1);
            const float frac = src - static_cast<float>(i0);
            const float v = tail[i0] * (1.0f - frac) + tail[i1] * frac;
            lowered.push_back(v);
        }

        samples.resize(head_count);
        samples.insert(samples.end(), lowered.begin(), lowered.end());
    }

    void applyRingMod(std::vector<float>& samples, int sample_rate, float freq_hz, float mix) {
        if (samples.empty() || sample_rate <= 0 || freq_hz <= 0.0f || mix <= 0.0f) {
            return;
        }

        mix = std::clamp(mix, 0.0f, 1.0f);

        for (std::size_t i = 0; i < samples.size(); ++i) {
            const float t = static_cast<float>(i) / static_cast<float>(sample_rate);
            const float carrier = std::sin(2.0f * kPi * freq_hz * t);
            const float wet = samples[i] * carrier;
            samples[i] = samples[i] * (1.0f - mix) + wet * mix;
        }
    }

    void applyBitcrush(std::vector<float>& samples, int bit_depth) {
        if (samples.empty() || bit_depth <= 0 || bit_depth >= 24) {
            return;
        }

        const float max_val = static_cast<float>(1 << (bit_depth - 1));
        for (float& s : samples) {
            s = std::round(s * max_val) / max_val;
            s = clamp1(s);
        }
    }

    void applyHardClip(std::vector<float>& samples, float clip_level) {
        if (samples.empty()) {
            return;
        }

        clip_level = std::clamp(clip_level, 0.0f, 1.0f);
        for (float& s : samples) {
            s = std::clamp(s, -clip_level, clip_level);
        }
    }

    void applyGain(std::vector<float>& samples, float gain) {
        if (samples.empty()) {
            return;
        }

        for (float& s : samples) {
            s = clamp1(s * gain);
        }
    }
}

namespace VoiceEffects {
    void applyWarp(
        std::vector<float>& samples,
        int sample_rate,
        const TtsWarpConfig& config
    ) {
        if (!config.enabled || samples.empty()) {
            return;
        }

        if (config.lower_sentence_end_enabled) {
            applyLowerSentenceEnd(samples, sample_rate, config.tail_ms, config.tail_drop);
        }

        if (config.ring_mod_enabled) {
            applyRingMod(samples, sample_rate, config.ring_mod_frequency_hz, config.ring_mod_mix);
        }

        if (config.bitcrush_enabled) {
            applyBitcrush(samples, config.bit_depth);
        }

        if (config.hard_clip_enabled) {
            applyHardClip(samples, config.clip_level);
        }

        applyGain(samples, config.output_gain);
    }
}