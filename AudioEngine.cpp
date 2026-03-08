// AudioEngine.cpp
#include "AudioEngine.hpp"

#include <AL/al.h>
#include <AL/alc.h>
#include <AL/alext.h>

#include <utility>

AudioEngine::~AudioEngine() {
    shutdown();
}

AudioEngine::AudioEngine(AudioEngine&& other) noexcept
    : m_device(other.m_device), m_context(other.m_context) {
    other.m_device = nullptr;
    other.m_context = nullptr;
}

AudioEngine& AudioEngine::operator=(AudioEngine&& other) noexcept {
    if (this != &other) {
        shutdown();
        m_device = other.m_device;
        m_context = other.m_context;
        other.m_device = nullptr;
        other.m_context = nullptr;
    }
    return *this;
}

bool AudioEngine::initialize(std::string& error) {
    shutdown();

    m_device = alcOpenDevice(nullptr); // default output device
    if (!m_device) {
        error = "OpenAL: failed to open default audio device.";
        return false;
    }

    m_context = alcCreateContext(m_device, nullptr);
    if (!m_context) {
        error = "OpenAL: failed to create audio context.";
        alcCloseDevice(m_device);
        m_device = nullptr;
        return false;
    }

    if (alcMakeContextCurrent(m_context) != ALC_TRUE) {
        error = "OpenAL: failed to make audio context current.";
        alcDestroyContext(m_context);
        alcCloseDevice(m_device);
        m_context = nullptr;
        m_device = nullptr;
        return false;
    }

    // Optional listener defaults for later 3D audio work
    alListener3f(AL_POSITION, 0.0f, 0.0f, 0.0f);
    alListener3f(AL_VELOCITY, 0.0f, 0.0f, 0.0f);

    const float orientation[] = {
        0.0f, 0.0f, -1.0f, // forward
        0.0f, 1.0f,  0.0f  // up
    };
    alListenerfv(AL_ORIENTATION, orientation);

    return true;
}

void AudioEngine::shutdown() {
    if (m_context) {
        alcMakeContextCurrent(m_context);

        for (auto& sound : m_playing) {
            if (sound.source != 0) {
                alSourceStop(sound.source);
                alDeleteSources(1, &sound.source);
            }
            if (sound.buffer != 0) {
                alDeleteBuffers(1, &sound.buffer);
            }
        }
        m_playing.clear();

        alcMakeContextCurrent(nullptr);
        alcDestroyContext(m_context);
        m_context = nullptr;
    }

    if (m_device) {
        alcCloseDevice(m_device);
        m_device = nullptr;
    }
}

bool AudioEngine::isPlaying() const {
    return !m_playing.empty();
}

bool AudioEngine::playPcmMonoFloat(
    const std::vector<float>& samples,
    int sample_rate,
    std::string& error
) {
    if (!isInitialized()) {
        error = "AudioEngine: not initialized.";
        return false;
    }

    if (samples.empty()) {
        error = "AudioEngine: empty sample buffer.";
        return false;
    }

    ALuint buffer = 0;
    ALuint source = 0;

    alGenBuffers(1, &buffer);
    if (alGetError() != AL_NO_ERROR) {
        error = "AudioEngine: alGenBuffers failed.";
        return false;
    }

    alBufferData(
        buffer,
        AL_FORMAT_MONO_FLOAT32,
        samples.data(),
        static_cast<ALsizei>(samples.size() * sizeof(float)),
        sample_rate
    );

    if (alGetError() != AL_NO_ERROR) {
        alDeleteBuffers(1, &buffer);
        error = "AudioEngine: alBufferData failed.";
        return false;
    }

    alGenSources(1, &source);
    if (alGetError() != AL_NO_ERROR) {
        alDeleteBuffers(1, &buffer);
        error = "AudioEngine: alGenSources failed.";
        return false;
    }

    alSourcei(source, AL_BUFFER, static_cast<ALint>(buffer));
    alSourcef(source, AL_GAIN, 1.0f);
    alSourcef(source, AL_PITCH, 1.0f);
    alSourcePlay(source);

    if (alGetError() != AL_NO_ERROR) {
        alDeleteSources(1, &source);
        alDeleteBuffers(1, &buffer);
        error = "AudioEngine: alSourcePlay failed.";
        return false;
    }

    m_playing.push_back({ source, buffer });
    return true;
}

void AudioEngine::update() {
    auto it = m_playing.begin();
    while (it != m_playing.end()) {
        ALint state = 0;
        alGetSourcei(it->source, AL_SOURCE_STATE, &state);

        if (state == AL_STOPPED) {
            alDeleteSources(1, &it->source);
            alDeleteBuffers(1, &it->buffer);
            it = m_playing.erase(it);
        } else {
            ++it;
        }
    }
}