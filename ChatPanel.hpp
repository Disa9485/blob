// ChatPanel.hpp
#pragma once

#include "AppConfig.hpp"
#include "ConversationMemoryService.hpp"
#include "LlamaChat.hpp"
#include "SoftBodyInteractor.hpp"
#include "SpeechPipeline.hpp"
#include "SentenceDetector.hpp"

#include "imgui.h"

#include <atomic>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

class ChatPanel {
public:
    ChatPanel(
        LlamaChat& chat,
        SpeechPipeline& speech,
        AppConfig& config,
        ConversationMemoryService* memory,
        physics::SoftBodyInteractor* soft_body_interactor,
        int64_t session_id);

    ~ChatPanel();

    ChatPanel(const ChatPanel&) = delete;
    ChatPanel& operator=(const ChatPanel&) = delete;

    void draw();

private:
    struct Entry {
        std::string role;
        std::string timestamp;
        std::string timestamp_iso8601;
        std::string text;
    };

    void startGeneration(const std::string& user_text);
    void appendAssistantSentence(const std::string& sentence);

    LlamaChat& chat_;
    SpeechPipeline& speech_;
    AppConfig& config_;
    ConversationMemoryService* memory_ = nullptr;
    physics::SoftBodyInteractor* soft_body_interactor_ = nullptr;

    std::vector<Entry> messages_;

    int64_t session_id_ = 0;
    int64_t next_message_index_ = 0;

    std::string last_retrieved_memory_;
    std::string last_augmented_system_prompt_;

    std::string input_;
    std::thread worker_;
    std::mutex mutex_;

    std::atomic<bool> generating_{false};
    bool scroll_to_bottom_ = false;
    bool initial_window_layout_set_ = false;
    bool have_last_window_rect_ = false;
    ImVec2 last_window_pos_ = ImVec2(0.0f, 0.0f);
    ImVec2 last_window_size_ = ImVec2(0.0f, 0.0f);
};