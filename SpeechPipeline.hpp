// SpeechPipeline.hpp
#pragma once

#include "AudioEngine.hpp"
#include "PiperTts.hpp"
#include "SentenceDetector.hpp"
#include "AppConfig.hpp"

#include <condition_variable>
#include <deque>
#include <mutex>
#include <string>
#include <thread>

class SpeechPipeline {
public:
    SpeechPipeline() = default;
    ~SpeechPipeline();

    SpeechPipeline(const SpeechPipeline&) = delete;
    SpeechPipeline& operator=(const SpeechPipeline&) = delete;

    bool initialize(
        AudioEngine& audio_engine,
        const TtsConfig& config,
        const std::string& espeak_data_path,
        std::string& error
    );

    void shutdown();

    void pushToken(const std::string& token);
    void flushText();

    // call every frame on main thread
    void update();

private:
    void workerLoop();

private:
    AudioEngine* m_audio = nullptr;
    PiperTts m_tts;
    SentenceDetector m_detector;
    TtsWarpConfig m_warp_config;

    struct ReadyClip {
        std::vector<float> samples;
        int sample_rate = 0;
    };

    std::mutex m_mutex;
    std::condition_variable m_cv;
    bool m_running = false;
    std::thread m_worker;

    std::deque<std::string> m_sentence_queue;
    std::deque<ReadyClip> m_ready_audio;
};