// LlamaChatLoader.cpp
#include "LlamaChatLoader.hpp"

LlamaChatLoader::LlamaChatLoader() = default;

LlamaChatLoader::~LlamaChatLoader() {
    if (worker_.joinable()) {
        worker_.join();
    }
}

void LlamaChatLoader::setStatus(const std::string& text) {
    std::lock_guard<std::mutex> lock(mutex_);
    status_ = text;
}

void LlamaChatLoader::start(
    const std::string& model_path,
    const std::string& system_prompt,
    const LlamaChat::Options& options)
{
    if (worker_.joinable()) {
        worker_.join();
    }

    chat_.reset();
    {
        std::lock_guard<std::mutex> lock(mutex_);
        status_ = "Starting model load";
        error_message_.clear();
        load_started_at_ = std::chrono::steady_clock::now();
    }

    state_ = State::Loading;

    worker_ = std::thread([this, model_path, system_prompt, options]() {
        setStatus("Allocating chat instance");

        auto chat = std::make_unique<LlamaChat>();

        setStatus("Loading model");

        const bool ok = chat->initialize(model_path, system_prompt, options);

        if (!ok) {
            {
                std::lock_guard<std::mutex> lock(mutex_);
                error_message_ = "Failed to initialize LlamaChat.";
                status_ = "Model load failed.";
            }
            state_ = State::Failed;
            return;
        }

        {
            std::lock_guard<std::mutex> lock(mutex_);
            chat_ = std::move(chat);
            status_ = "Model ready.";
        }

        state_ = State::Ready;
    });
}

bool LlamaChatLoader::isLoading() const {
    return state_ == State::Loading;
}

bool LlamaChatLoader::isReady() const {
    return state_ == State::Ready;
}

bool LlamaChatLoader::hasFailed() const {
    return state_ == State::Failed;
}

LlamaChatLoader::State LlamaChatLoader::state() const {
    return state_.load();
}

std::string LlamaChatLoader::status() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return status_;
}

std::string LlamaChatLoader::errorMessage() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return error_message_;
}

float LlamaChatLoader::elapsedSeconds() const {
    if (state_ == State::Idle) {
        return 0.0f;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    if (load_started_at_ == std::chrono::steady_clock::time_point{}) {
        return 0.0f;
    }

    const auto now = std::chrono::steady_clock::now();
    return std::chrono::duration<float>(now - load_started_at_).count();
}

LlamaChat* LlamaChatLoader::getChat() {
    if (state_ != State::Ready) {
        return nullptr;
    }
    return chat_.get();
}