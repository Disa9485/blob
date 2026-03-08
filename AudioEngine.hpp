// AudioEngine.hpp
#pragma once

#include <string>
#include <vector>

struct ALCdevice;
struct ALCcontext;

class AudioEngine {
public:
    AudioEngine() = default;
    ~AudioEngine();

    AudioEngine(const AudioEngine&) = delete;
    AudioEngine& operator=(const AudioEngine&) = delete;

    AudioEngine(AudioEngine&& other) noexcept;
    AudioEngine& operator=(AudioEngine&& other) noexcept;

    bool initialize(std::string& error);
    void shutdown();

    bool isInitialized() const { return m_device != nullptr && m_context != nullptr; }
    bool isPlaying() const;

    bool playPcmMonoFloat(
        const std::vector<float>& samples,
        int sample_rate,
        std::string& error
    );

    void update();

private:
    struct PlayingSound {
        unsigned int source = 0;
        unsigned int buffer = 0;
    };

private:
    ALCdevice* m_device = nullptr;
    ALCcontext* m_context = nullptr;
    std::vector<PlayingSound> m_playing;
};