// PiperTts.cpp
#include "PiperTts.hpp"

#include <piper.h>

PiperTts::~PiperTts() {
    shutdown();
}

bool PiperTts::initialize(
    const std::string& model_path,
    const std::string& config_path,
    const std::string& espeak_data_path,
    std::string& error
) {
    shutdown();

    const char* config_ptr = config_path.empty() ? nullptr : config_path.c_str();

    m_synth = piper_create(
        model_path.c_str(),
        config_ptr,
        espeak_data_path.c_str()
    );

    if (!m_synth) {
        error = "PiperTts: piper_create failed.";
        return false;
    }

    return true;
}

void PiperTts::shutdown() {
    if (m_synth) {
        piper_free(m_synth);
        m_synth = nullptr;
    }
}

bool PiperTts::synthesizeSentence(
    const std::string& text,
    TtsAudio& out_audio,
    std::string& error
) {
    out_audio.samples.clear();
    out_audio.sample_rate = 0;
    out_audio.channels = 1;

    if (!m_synth) {
        error = "PiperTts: synthesizer not initialized.";
        return false;
    }

    piper_synthesize_options options = piper_default_synthesize_options(m_synth);
    options.speaker_id = m_speaker_id;
    options.length_scale = m_length_scale;

    if (m_noise_scale >= 0.0f) {
        options.noise_scale = m_noise_scale;
    }

    if (m_noise_w_scale >= 0.0f) {
        options.noise_w_scale = m_noise_w_scale;
    }

    const int start_rc = piper_synthesize_start(m_synth, text.c_str(), &options);
    if (start_rc != PIPER_OK) {
        error = "PiperTts: piper_synthesize_start failed.";
        return false;
    }

    piper_audio_chunk chunk{};
    while (true) {
        const int rc = piper_synthesize_next(m_synth, &chunk);

        if (rc == PIPER_DONE) {
            break;
        }

        if (rc != PIPER_OK) {
            error = "PiperTts: piper_synthesize_next failed.";
            return false;
        }

        if (chunk.num_samples > 0 && chunk.samples) {
            if (out_audio.sample_rate == 0) {
                out_audio.sample_rate = chunk.sample_rate;
            }

            out_audio.samples.insert(
                out_audio.samples.end(),
                chunk.samples,
                chunk.samples + chunk.num_samples
            );
        }
    }

    if (out_audio.sample_rate <= 0) {
        error = "PiperTts: synthesis produced no sample rate.";
        return false;
    }

    return true;
}