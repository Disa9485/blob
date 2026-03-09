// ChatUI.hpp
#pragma once

#include "AppConfig.hpp"
#include "ConversationMemoryService.hpp"
#include "LlamaChat.hpp"
#include "SentenceDetector.hpp"
#include "SoftBodyInteractor.hpp"
#include "SpeechPipeline.hpp"

#include "imgui.h"

#include <atomic>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

class ChatUI {
public:
    ChatUI(
        LlamaChat& chat,
        SpeechPipeline& speech,
        AppConfig& config,
        ConversationMemoryService* memory,
        physics::SoftBodyInteractor* soft_body_interactor,
        int64_t session_id);

    ~ChatUI();

    ChatUI(const ChatUI&) = delete;
    ChatUI& operator=(const ChatUI&) = delete;

    void draw();

    void setSpeakerAnchorNormalized(const ImVec2& normalizedAnchor);
    void setBubblePlacement(float offsetX, float offsetY);
    void setTailShape(
        float attachX,
        float attachY,
        float width,
        float height
    );

private:
    struct Entry {
        std::string role;
        std::string timestamp;
        std::string timestamp_iso8601;
        std::string text;
    };

    struct DialogueBubble {
        std::string text;
    };

    void startGeneration(const std::string& user_text);
    void appendAssistantSentence(const std::string& sentence);

    void drawTopClock();
    void drawInputBar();
    void drawHistoryPanel();
    void drawDialogueBubbles();

    std::string loadingDotsText() const;

private:
    LlamaChat& chat_;
    SpeechPipeline& speech_;
    AppConfig& config_;
    ConversationMemoryService* memory_ = nullptr;
    physics::SoftBodyInteractor* soft_body_interactor_ = nullptr;

    std::vector<Entry> messages_;
    std::vector<DialogueBubble> visible_bubbles_;

    int64_t session_id_ = 0;
    int64_t next_message_index_ = 0;

    std::string last_retrieved_memory_;
    std::string last_augmented_system_prompt_;

    std::string input_;
    std::thread worker_;
    std::mutex mutex_;

    std::atomic<bool> generating_{false};
    bool scroll_history_to_bottom_ = false;

    ImVec2 speaker_anchor_normalized_ = ImVec2(0.5f, 0.5f);

    float bubble_stack_offset_x_ = 22.0f;
    float bubble_stack_offset_y_ = -120.0f;

    float tail_attach_x_ = 15.0f;   // x position on bubble from left edge
    float tail_attach_y_ = 0.0f;    // y position on bubble from top edge
    float tail_width_ = 24.0f;      // how far tail extends left
    float tail_height_ = 18.0f;     // vertical size of tail
};