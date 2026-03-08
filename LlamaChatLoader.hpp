// LlamaChatLoader.hpp
#pragma once

#include "LlamaChat.hpp"

#include <atomic>
#include <chrono>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

class LlamaChatLoader {
public:
    enum class State {
        Idle,
        Loading,
        Ready,
        Failed
    };

    LlamaChatLoader();
    ~LlamaChatLoader();

    LlamaChatLoader(const LlamaChatLoader&) = delete;
    LlamaChatLoader& operator=(const LlamaChatLoader&) = delete;

    void start(
        const std::string& model_path,
        const std::string& system_prompt,
        const LlamaChat::Options& options
    );

    bool isLoading() const;
    bool isReady() const;
    bool hasFailed() const;
    State state() const;

    std::string status() const;
    std::string errorMessage() const;
    float elapsedSeconds() const;

    LlamaChat* getChat();

private:
    void setStatus(const std::string& text);

    std::unique_ptr<LlamaChat> chat_;
    std::thread worker_;

    std::atomic<State> state_{State::Idle};

    mutable std::mutex mutex_;
    std::string status_ = "Idle";
    std::string error_message_;
    std::chrono::steady_clock::time_point load_started_at_{};
};