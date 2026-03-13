// PiperTts.hpp
#pragma once

#include <string>
#include <vector>

struct piper_synthesizer;

struct TtsAudio {
    std::vector<float> samples;
    int sample_rate = 0;
    int channels = 1;
};

class PiperTts {
public:
    PiperTts() = default;
    ~PiperTts();

    PiperTts(const PiperTts&) = delete;
    PiperTts& operator=(const PiperTts&) = delete;

    bool initialize(
        const std::string& model_path,
        const std::string& config_path,
        const std::string& espeak_data_path,
        std::string& error
    );

    void shutdown();
    bool isInitialized() const { return m_synth != nullptr; }

    bool synthesizeSentence(
        const std::string& text,
        TtsAudio& out_audio,
        std::string& error
    );

    void setSpeakerId(int speaker_id) { m_speaker_id = speaker_id; }
    void setLengthScale(float v) { m_length_scale = v; }
    void setNoiseScale(float v) { m_noise_scale = v; }
    void setNoiseWScale(float v) { m_noise_w_scale = v; }

private:
    piper_synthesizer* m_synth = nullptr;

    int m_speaker_id = 0;
    float m_length_scale = 1.2f;
    float m_noise_scale = -1.0f;   // negative = use model default
    float m_noise_w_scale = -1.0f; // negative = use model default
};