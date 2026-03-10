// ChatUI.hpp
#pragma once

#include "AppConfig.hpp"
#include "ConversationMemoryService.hpp"
#include "LlamaChat.hpp"
#include "SentenceDetector.hpp"
#include "SoftBodyInteractor.hpp"
#include "SpeechPipeline.hpp"
#include "RuntimeCancellation.hpp"

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

    struct GenerationOptions {
        bool consume_touch_summary = false;
        bool ingest_prompt_memory = true;
        bool ingest_response_memory = true;
    };

    void draw();

    bool isGenerating() const;
    void requestStopGeneration();   // add this
    void startUserGeneration(const std::string& user_text);
    void startAutonomousGeneration(const std::string& event_text);
    void startAutonomousGenerationAndConsumeTouches(const std::string& event_text);

    void setSpeakerAnchorNormalized(const ImVec2& normalizedAnchor);
    void setBubblePlacement(float offsetX, float offsetY);
    void setTailShape(
        float attachX,
        float attachY,
        float width,
        float height
    );

    void setCancellation(RuntimeCancellation* cancellation);

private:
    struct Entry {
        std::string role;
        std::string timestamp;
        std::string timestamp_iso8601;
        std::string text;
    };

    struct DisplayItem {
        enum class Kind {
            Dialogue,
            Action
        };

        Kind kind = Kind::Dialogue;
        std::string text;
    };

    void startGeneration(const std::string& user_text, const GenerationOptions& options);
    void appendAssistantDialogue(const std::string& sentence);
    void appendAssistantAction(const std::string& action);

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
    std::vector<DisplayItem> visible_items_;

    int64_t session_id_ = 0;
    int64_t next_message_index_ = 0;

    std::string last_retrieved_memory_;
    std::string last_augmented_system_prompt_;

    std::string input_;
    std::thread worker_;
    std::mutex mutex_;

    std::atomic<bool> generating_{false};
    std::atomic<bool> stop_requested_{false};   // add this
    bool scroll_history_to_bottom_ = false;
    bool refocus_input_ = true;

    ImVec2 speaker_anchor_normalized_ = ImVec2(0.5f, 0.5f);

    float bubble_stack_offset_x_ = 25.0f;
    float bubble_stack_offset_y_ = 0.0f;

    float tail_attach_x_ = 15.0f;
    float tail_attach_y_ = 0.0f;
    float tail_width_ = 24.0f;
    float tail_height_ = 18.0f;

    RuntimeCancellation* cancellation_ = nullptr;
};