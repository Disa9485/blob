// ChatPanel.cpp
#include "ChatPanel.hpp"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <ctime>
#include <sstream>
#include <iomanip>

namespace {
    ImVec2 clampWindowPosToViewport(
        const ImVec2& pos,
        const ImVec2& size,
        const ImGuiViewport* viewport,
        float outer_buffer)
    {
        if (!viewport) {
            return pos;
        }

        const float min_x = viewport->WorkPos.x + outer_buffer;
        const float min_y = viewport->WorkPos.y + outer_buffer;
        const float max_x = viewport->WorkPos.x + viewport->WorkSize.x - outer_buffer - size.x;
        const float max_y = viewport->WorkPos.y + viewport->WorkSize.y - outer_buffer - size.y;

        ImVec2 clamped = pos;
        clamped.x = (std::max)(min_x, (std::min)(clamped.x, max_x));
        clamped.y = (std::max)(min_y, (std::min)(clamped.y, max_y));

        return clamped;
    }

    ImVec2 computeInitialChatPanelSize(const ImGuiViewport* viewport, float outer_buffer) {
        if (!viewport) {
            return ImVec2(420.0f, 700.0f);
        }

        const float width = (std::max)(320.0f, viewport->WorkSize.x * 0.333f - outer_buffer);
        const float height = (std::max)(360.0f, viewport->WorkSize.y - outer_buffer * 2.0f);
        return ImVec2(width, height);
    }

    const char* ordinalSuffix(int day) {
        if (day % 100 >= 11 && day % 100 <= 13) {
            return "th";
        }

        switch (day % 10) {
            case 1: return "st";
            case 2: return "nd";
            case 3: return "rd";
            default: return "th";
        }
    }

    std::string formatTimestampIso8601Now() {
        const auto now = std::chrono::system_clock::now();
        const std::time_t now_time = std::chrono::system_clock::to_time_t(now);

        std::tm local_tm{};
    #ifdef _WIN32
        localtime_s(&local_tm, &now_time);
    #else
        localtime_r(&now_time, &local_tm);
    #endif

        char buffer[32];
        std::strftime(buffer, sizeof(buffer), "%Y-%m-%dT%H:%M:%S", &local_tm);
        return std::string(buffer);
    }

    std::string formatTimestampNow() {
        const auto now = std::chrono::system_clock::now();
        const std::time_t now_time = std::chrono::system_clock::to_time_t(now);

        std::tm local_tm{};
    #ifdef _WIN32
        localtime_s(&local_tm, &now_time);
    #else
        localtime_r(&now_time, &local_tm);
    #endif

        static const char* months[] = {
            "January", "February", "March", "April", "May", "June",
            "July", "August", "September", "October", "November", "December"
        };

        const int month_index = local_tm.tm_mon;
        const int day = local_tm.tm_mday;
        const int year = local_tm.tm_year + 1900;

        const int minute = local_tm.tm_min;
        const bool is_pm = local_tm.tm_hour >= 12;

        int hour12 = local_tm.tm_hour % 12;
        if (hour12 == 0) {
            hour12 = 12;
        }

        std::ostringstream out;
        out << months[month_index] << " "
            << day << ordinalSuffix(day) << " "
            << year << " at "
            << hour12 << ":"
            << (minute < 10 ? "0" : "") << minute
            << (is_pm ? "pm" : "am");

        return out.str();
    }

    bool parseIso8601Local(const std::string& iso, std::tm& out_tm) {
        std::istringstream in(iso);
        in >> std::get_time(&out_tm, "%Y-%m-%dT%H:%M:%S");
        return !in.fail();
    }

    std::string formatHumanDateFromIso8601(const std::string& iso) {
        std::tm tm{};
        if (!parseIso8601Local(iso, tm)) {
            return iso;
        }

        static const char* months[] = {
            "January", "February", "March", "April", "May", "June",
            "July", "August", "September", "October", "November", "December"
        };

        const int month_index = tm.tm_mon;
        const int day = tm.tm_mday;
        const int year = tm.tm_year + 1900;

        const int minute = tm.tm_min;
        const bool is_pm = tm.tm_hour >= 12;

        int hour12 = tm.tm_hour % 12;
        if (hour12 == 0) {
            hour12 = 12;
        }

        std::ostringstream out;
        out << months[month_index] << " "
            << day << ordinalSuffix(day) << " "
            << year << " at "
            << hour12 << ":"
            << (minute < 10 ? "0" : "") << minute
            << (is_pm ? "pm" : "am");

        return out.str();
    }

    std::string memorySpeakerLabel(const std::string& role, const AppConfig& config) {
        if (role == "user") {
            return config.userName();
        }
        if (role == "assistant") {
            return "you";
        }
        return role;
    }

    std::string formatMemoryLine(
        const ConversationMemoryService::SearchResult& result,
        const AppConfig& config)
    {
        std::ostringstream out;
        out << "On "
            << formatHumanDateFromIso8601(result.timestamp_iso8601)
            << " "
            << memorySpeakerLabel(result.role, config)
            << " said \""
            << result.text
            << "\"";
        return out.str();
    }

    std::string joinFormattedMemories(
        const std::vector<ConversationMemoryService::SearchResult>& results,
        const AppConfig& config)
    {
        if (results.empty()) {
            return "";
        }

        std::ostringstream out;
        for (std::size_t i = 0; i < results.size(); ++i) {
            out << formatMemoryLine(results[i], config);
            if (i + 1 < results.size()) {
                out << "\n";
            }
        }
        return out.str();
    }

    std::string buildAugmentedSystemPrompt(
        const std::string& dynamic_base_prompt,
        const std::string& current_display_time,
        const std::string& retrieved_memories,
        const std::string& touch_summary)
    {
        std::ostringstream out;

        if (!dynamic_base_prompt.empty()) {
            out << dynamic_base_prompt;
        }

        out << "\n\nCurrent datetime: "
            << current_display_time;

        if (!retrieved_memories.empty()) {
            out << "\n\nYour potential relevant memories:\n"
                << retrieved_memories;
        }

        if (!touch_summary.empty()) {
            out << "\n\n"
                << touch_summary;
        }

        return out.str();
    }

    bool isUsableViewport(const ImGuiViewport* viewport, float outer_buffer) {
        if (!viewport) {
            return false;
        }

        const float usable_width = viewport->WorkSize.x - outer_buffer * 2.0f;
        const float usable_height = viewport->WorkSize.y - outer_buffer * 2.0f;

        return usable_width > 64.0f && usable_height > 64.0f;
    }
}

ChatPanel::ChatPanel(
    LlamaChat& chat,
    SpeechPipeline& speech,
    AppConfig& config,
    ConversationMemoryService* memory,
    physics::SoftBodyInteractor* soft_body_interactor,
    int64_t session_id)
    : chat_(chat)
    , speech_(speech)
    , config_(config)
    , memory_(memory)
    , soft_body_interactor_(soft_body_interactor)
    , session_id_(session_id) {
    messages_.push_back({ "system", "", "", "Communications Established." });
}

ChatPanel::~ChatPanel() {
    if (worker_.joinable()) {
        worker_.join();
    }
}

void ChatPanel::startGeneration(const std::string& user_text) {
    if (generating_) return;

    if (worker_.joinable()) {
        worker_.join();
    }

    const std::string user_display_ts = formatTimestampNow();
    const std::string user_iso_ts = formatTimestampIso8601Now();
    const std::string assistant_display_ts = formatTimestampNow();
    const std::string assistant_iso_ts = formatTimestampIso8601Now();

    const int64_t user_message_index = next_message_index_++;
    const int64_t assistant_message_index = next_message_index_++;

    std::string retrieved_memory_text;

    if (memory_ && memory_->isEnabled()) {
        std::vector<ConversationMemoryService::SearchResult> search_results;
        std::string search_error;

        if (memory_->searchTopMessages(
                session_id_,
                user_message_index,
                "user",
                user_iso_ts,
                user_text,
                search_results,
                search_error)) {
            retrieved_memory_text = joinFormattedMemories(search_results, config_);
        }
    }

    std::string touch_summary;
    if (soft_body_interactor_) {
        touch_summary = soft_body_interactor_->consumeTouchedPartsSentence(config_.userName());
    }

    const std::string augmented_system_prompt = buildAugmentedSystemPrompt(
        config_.buildDynamicSystemPrompt(),
        user_display_ts,
        retrieved_memory_text,
        touch_summary
    );

    {
        std::lock_guard<std::mutex> lock(mutex_);
        last_retrieved_memory_ = retrieved_memory_text;
        last_augmented_system_prompt_ = augmented_system_prompt;

        messages_.push_back({ "user", user_display_ts, user_iso_ts, user_text });
        messages_.push_back({ "assistant", assistant_display_ts, assistant_iso_ts, "" });
        scroll_to_bottom_ = true;
    }

    if (memory_ && memory_->isEnabled()) {
        std::string memory_error;
        memory_->ingestMessage(
            session_id_,
            user_message_index,
            "user",
            user_iso_ts,
            user_text,
            memory_error
        );
    }

    generating_ = true;

    worker_ = std::thread([this, user_text, augmented_system_prompt, assistant_message_index]() {
        SentenceDetector detector;

        const bool ok = chat_.generateStreamWithSystemPrompt(
            user_text,
            augmented_system_prompt,
            [this, &detector](const std::string& piece) {
                detector.pushToken(piece);

                while (detector.hasSentence()) {
                    appendAssistantSentence(detector.popSentence());
                }

                return true;
            }
        );

        const std::string tail = detector.flushRemainder();
        if (!tail.empty()) {
            appendAssistantSentence(tail);
        }

        std::string assistant_text;
        std::string assistant_iso_ts;

        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (!ok) {
                if (!messages_.empty()) {
                    if (!messages_.back().text.empty()) {
                        messages_.back().text += "\n";
                    }
                    messages_.back().text += "[generation failed]";
                    scroll_to_bottom_ = true;
                }
            }

            if (!messages_.empty() && messages_.back().role == "assistant") {
                assistant_text = messages_.back().text;
                assistant_iso_ts = messages_.back().timestamp_iso8601;
            }
        }

        if (ok && memory_ && memory_->isEnabled() && !assistant_text.empty()) {
            std::string memory_error;
            memory_->ingestMessage(
                session_id_,
                assistant_message_index,
                "assistant",
                assistant_iso_ts,
                assistant_text,
                memory_error
            );
        }

        generating_ = false;
    });
}

void ChatPanel::appendAssistantSentence(const std::string& sentence) {
    if (sentence.empty()) {
        return;
    }

    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!messages_.empty() && messages_.back().role == "assistant") {
            messages_.back().text = messages_.back().text + " " + sentence;
            scroll_to_bottom_ = true;
        }
    }

    speech_.pushSentence(sentence);
}

void ChatPanel::draw() {
    ImGuiViewport* viewport = ImGui::GetMainViewport();
    const float outer_buffer = 16.0f;
    const bool viewport_usable = isUsableViewport(viewport, outer_buffer);

    if (viewport_usable) {
        ImGui::SetNextWindowSizeConstraints(
            ImVec2(320.0f, 360.0f),
            ImVec2(
                (std::max)(320.0f, viewport->WorkSize.x - outer_buffer * 2.0f),
                (std::max)(360.0f, viewport->WorkSize.y - outer_buffer * 2.0f)
            )
        );
    }

    if (!initial_window_layout_set_ && viewport_usable) {
        const ImVec2 initial_size = computeInitialChatPanelSize(viewport, outer_buffer);
        const ImVec2 initial_pos(
            viewport->WorkPos.x + outer_buffer,
            viewport->WorkPos.y + outer_buffer
        );

        ImGui::SetNextWindowPos(initial_pos, ImGuiCond_Always);
        ImGui::SetNextWindowSize(initial_size, ImGuiCond_Always);

        last_window_pos_ = initial_pos;
        last_window_size_ = initial_size;
        have_last_window_rect_ = true;
        initial_window_layout_set_ = true;
    } else if (viewport_usable && have_last_window_rect_) {
        const ImVec2 clamped_pos = clampWindowPosToViewport(
            last_window_pos_,
            last_window_size_,
            viewport,
            outer_buffer
        );

        const bool needs_clamp =
            clamped_pos.x != last_window_pos_.x ||
            clamped_pos.y != last_window_pos_.y;

        if (needs_clamp) {
            ImGui::SetNextWindowPos(clamped_pos, ImGuiCond_Always);
            last_window_pos_ = clamped_pos;
        }
    }

    const std::string chat_header = "Chatting with " + config_.llmName();
    ImGui::Begin(chat_header.c_str());

    if (viewport_usable) {
        const ImVec2 current_pos = ImGui::GetWindowPos();
        const ImVec2 current_size = ImGui::GetWindowSize();

        if (current_size.x > 64.0f && current_size.y > 64.0f) {
            last_window_pos_ = current_pos;
            last_window_size_ = current_size;
            have_last_window_rect_ = true;
        }
    }

    if (config_.llm_options.show_system_prompt) {
        std::string static_prompt;
        std::string dynamic_prompt;
        std::string retrieved;

        {
            std::lock_guard<std::mutex> lock(mutex_);
            static_prompt = config_.buildStaticSystemPrompt();
            dynamic_prompt = last_augmented_system_prompt_.empty()
                ? config_.buildDynamicSystemPrompt()
                : last_augmented_system_prompt_;
            retrieved = last_retrieved_memory_;
        }

        ImGui::Text("System Prompt");
        ImGui::BeginChild("SystemPromptRegion", ImVec2(0.0f, 90.0f), true);
        ImGui::TextWrapped("%s", static_prompt.c_str());
        ImGui::TextWrapped("%s", dynamic_prompt.c_str());
        ImGui::EndChild();

        ImGui::Separator();
    }

    const std::string current_display_time = formatTimestampNow();
    ImGui::Text("Current Datetime: %s", current_display_time.c_str());
    ImGui::Separator();

    float t = ImGui::GetTime();
    int dots = static_cast<int>(t * 2.0f) % 4;
    const char* anim[] = { "", ".", "..", "..." };

    if (generating_)
        ImGui::Text("Status: Generating%s", anim[dots]);
    else
        ImGui::Text("Status: Idle");

    const float footer_height =
        ImGui::GetFrameHeight() +
        ImGui::GetStyle().ItemSpacing.y;

    ImGui::BeginChild(
        "ChatScrollRegion",
        ImVec2(0.0f, -footer_height),
        true
    );

    {
        std::lock_guard<std::mutex> lock(mutex_);

        for (const Entry& msg : messages_) {
            if (msg.role == "user") {
                ImGui::TextWrapped("%s: %s", config_.userName().c_str(), msg.text.c_str());
            } else if (msg.role == "assistant") {
                const std::string current_name = config_.llmName();
                ImGui::TextWrapped("%s: %s", current_name.c_str(), msg.text.c_str());
            } else {
                ImGui::TextWrapped("%s", msg.text.c_str());
            }

            ImGui::Spacing();
        }

        if (scroll_to_bottom_) {
            ImGui::SetScrollHereY(1.0f);
            scroll_to_bottom_ = false;
        }
    }

    ImGui::EndChild();

    static char buffer[2048] = {};
    std::memset(buffer, 0, sizeof(buffer));
    std::memcpy(buffer, input_.c_str(), (std::min)(input_.size(), sizeof(buffer) - 1));

    ImGui::PushItemWidth(-110.0f);
    const bool enter_pressed = ImGui::InputText(
        "##chat_input",
        buffer,
        sizeof(buffer),
        ImGuiInputTextFlags_EnterReturnsTrue
    );
    ImGui::PopItemWidth();

    input_ = buffer;

    ImGui::SameLine();
    const bool send_clicked = ImGui::Button("Send");

    if (!generating_ && (send_clicked || enter_pressed) && !input_.empty()) {
        const std::string user_text = input_;
        input_.clear();
        startGeneration(user_text);
    }

    ImGui::End();
}