// SpeechPipeline.cpp
#include "SpeechPipeline.hpp"
#include "VoiceEffects.hpp"

#include <iostream>

SpeechPipeline::~SpeechPipeline() {
    shutdown();
}

bool SpeechPipeline::initialize(
    AudioEngine& audio_engine,
    const TtsConfig& config,
    const std::string& espeak_data_path,
    std::string& error
) {
    shutdown();

    m_audio = &audio_engine;
    m_warp_config = config.warp;

    if (!m_tts.initialize(config.model_path, config.model_config_path, espeak_data_path, error)) {
        m_audio = nullptr;
        return false;
    }

    m_tts.setSpeakerId(config.speaker_id);
    m_tts.setLengthScale(config.length_scale);
    m_tts.setNoiseScale(config.noise_scale);
    m_tts.setNoiseWScale(config.noise_w_scale);

    m_running = true;
    m_worker = std::thread(&SpeechPipeline::workerLoop, this);
    return true;
}

void SpeechPipeline::shutdown() {
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_running = false;
    }
    m_cv.notify_all();

    if (m_worker.joinable()) {
        m_worker.join();
    }

    m_sentence_queue.clear();
    m_ready_audio.clear();
    m_tts.shutdown();
    m_audio = nullptr;
}

void SpeechPipeline::pushToken(const std::string& token) {
    m_detector.pushToken(token);

    std::lock_guard<std::mutex> lock(m_mutex);
    while (m_detector.hasSentence()) {
        m_sentence_queue.push_back(m_detector.popSentence());
    }

    m_cv.notify_one();
}

void SpeechPipeline::flushText() {
    const std::string tail = m_detector.flushRemainder();
    if (!tail.empty()) {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_sentence_queue.push_back(tail);
        m_cv.notify_one();
    }
}

void SpeechPipeline::update() {
    if (!m_audio) {
        return;
    }

    // First clean up any sources that have finished playback.
    m_audio->update();

    // If something is still playing, leave queued clips alone.
    if (m_audio->isPlaying()) {
        return;
    }

    ReadyClip next_clip;
    bool have_clip = false;

    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (!m_ready_audio.empty()) {
            next_clip = std::move(m_ready_audio.front());
            m_ready_audio.pop_front();
            have_clip = true;
        }
    }

    if (!have_clip) {
        return;
    }

    std::string error;
    if (!m_audio->playPcmMonoFloat(next_clip.samples, next_clip.sample_rate, error)) {
        std::cerr << error << "\n";
    }
}

void SpeechPipeline::workerLoop() {
    while (true) {
        std::string sentence;

        {
            std::unique_lock<std::mutex> lock(m_mutex);
            m_cv.wait(lock, [this] {
                return !m_running || !m_sentence_queue.empty();
            });

            if (!m_running && m_sentence_queue.empty()) {
                return;
            }

            sentence = std::move(m_sentence_queue.front());
            m_sentence_queue.pop_front();
        }

        TtsAudio audio;
        std::string error;
        if (!m_tts.synthesizeSentence(sentence, audio, error)) {
            std::cerr << error << "\n";
            continue;
        }

        VoiceEffects::applyWarp(audio.samples, audio.sample_rate, m_warp_config);

        ReadyClip clip;
        clip.samples = std::move(audio.samples);
        clip.sample_rate = audio.sample_rate;

        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_ready_audio.push_back(std::move(clip));
        }
    }
}