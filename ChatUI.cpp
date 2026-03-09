// ChatUI.cpp
#include "ChatUI.hpp"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <ctime>
#include <iomanip>
#include <sstream>

namespace {
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

        out << "\n\nCurrent datetime: " << current_display_time;

        if (!retrieved_memories.empty()) {
            out << "\n\nYour potential relevant memories:\n" << retrieved_memories;
        }

        if (!touch_summary.empty()) {
            out << "\n\n" << touch_summary;
        }

        out << " You should mention this to them.";

        return out.str();
    }
}

ChatUI::ChatUI(
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
    , session_id_(session_id)
{
    messages_.push_back({ "system", "", "", "Communications Established." });
}

ChatUI::~ChatUI() {
    if (worker_.joinable()) {
        worker_.join();
    }
}

void ChatUI::setSpeakerAnchorNormalized(const ImVec2& normalizedAnchor) {
    speaker_anchor_normalized_.x = (std::max)(0.0f, (std::min)(1.0f, normalizedAnchor.x));
    speaker_anchor_normalized_.y = (std::max)(0.0f, (std::min)(1.0f, normalizedAnchor.y));
}

void ChatUI::setBubblePlacement(float offsetX, float offsetY) {
    bubble_stack_offset_x_ = offsetX;
    bubble_stack_offset_y_ = offsetY;
}

void ChatUI::setTailShape(
    float attachX,
    float attachY,
    float width,
    float height
) {
    tail_attach_x_ = attachX;
    tail_attach_y_ = attachY;
    tail_width_ = width;
    tail_height_ = height;
}

void ChatUI::startGeneration(const std::string& user_text) {
    if (generating_) {
        return;
    }

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

        visible_bubbles_.clear();
        messages_.push_back({ "user", user_display_ts, user_iso_ts, user_text });
        messages_.push_back({ "assistant", assistant_display_ts, assistant_iso_ts, "" });
        scroll_history_to_bottom_ = true;
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
                    visible_bubbles_.push_back({ "[generation failed]" });
                    scroll_history_to_bottom_ = true;
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

void ChatUI::appendAssistantSentence(const std::string& sentence) {
    if (sentence.empty()) {
        return;
    }

    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!messages_.empty() && messages_.back().role == "assistant") {
            if (!messages_.back().text.empty()) {
                messages_.back().text += " ";
            }
            messages_.back().text += sentence;
            visible_bubbles_.push_back({ sentence });
            scroll_history_to_bottom_ = true;
        }
    }

    speech_.pushSentence(sentence);
}

std::string ChatUI::loadingDotsText() const {
    const int dots = static_cast<int>(ImGui::GetTime() * 2.0f) % 4;
    switch (dots) {
        case 0: return "";
        case 1: return ".";
        case 2: return "..";
        default: return "...";
    }
}

void ChatUI::drawTopClock() {
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    if (!viewport) {
        return;
    }

    const std::string now_text = formatTimestampNow();
    const ImVec2 text_size = ImGui::CalcTextSize(now_text.c_str());

    const float pad_x = 12.0f;
    const float pad_y = 6.0f;
    const float window_w = text_size.x + pad_x * 2.0f;
    const float window_h = text_size.y + pad_y * 2.0f;

    ImGui::SetNextWindowBgAlpha(0.0f);
    ImGui::SetNextWindowPos(
        ImVec2(
            viewport->WorkPos.x + viewport->WorkSize.x * 0.5f - window_w * 0.5f,
            viewport->WorkPos.y + 12.0f
        ),
        ImGuiCond_Always
    );
    ImGui::SetNextWindowSize(
        ImVec2(window_w, window_h),
        ImGuiCond_Always
    );

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(pad_x, pad_y));

    ImGui::Begin(
        "##TopClock",
        nullptr,
        ImGuiWindowFlags_NoDecoration |
        ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoSavedSettings |
        ImGuiWindowFlags_NoInputs |
        ImGuiWindowFlags_NoBackground
    );

    ImGui::TextUnformatted(now_text.c_str());
    ImGui::End();

    ImGui::PopStyleVar();
}

void ChatUI::drawInputBar() {
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    if (!viewport) {
        return;
    }

    const float input_width = (std::min)(500.0f, viewport->WorkSize.x - 140.0f);
    const float button_width = 80.0f;
    const float gap = 8.0f;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 10.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(12.0f, 10.0f));

    const float control_height = ImGui::GetFrameHeight();
    const float total_width = input_width + gap + button_width;
    const float total_height = control_height;

    const ImVec2 pos(
        viewport->WorkPos.x + viewport->WorkSize.x * 0.5f - total_width * 0.5f,
        viewport->WorkPos.y + viewport->WorkSize.y - total_height - 16.0f
    );

    ImGui::SetNextWindowPos(pos, ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(total_width, total_height), ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.0f);

    ImGui::Begin(
        "##ChatInputBar",
        nullptr,
        ImGuiWindowFlags_NoDecoration |
        ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoSavedSettings |
        ImGuiWindowFlags_NoBackground
    );

    static char buffer[2048] = {};
    std::memset(buffer, 0, sizeof(buffer));
    std::memcpy(buffer, input_.c_str(), (std::min)(input_.size(), sizeof(buffer) - 1));

    ImGui::PushItemWidth(input_width);
    const bool enter_pressed = ImGui::InputText(
        "##chat_input",
        buffer,
        sizeof(buffer),
        ImGuiInputTextFlags_EnterReturnsTrue
    );
    ImGui::PopItemWidth();

    input_ = buffer;

    ImGui::SameLine(0.0f, gap);
    const bool send_clicked = ImGui::Button("Send", ImVec2(button_width, control_height));

    if (!generating_ && (send_clicked || enter_pressed) && !input_.empty()) {
        const std::string user_text = input_;
        input_.clear();
        startGeneration(user_text);
    }

    ImGui::End();
    ImGui::PopStyleVar(4);
}

void ChatUI::drawHistoryPanel() {
    ImGuiViewport* viewport = ImGui::GetMainViewport();
    if (!viewport) {
        return;
    }

    const float edge_buffer = 16.0f;

    const ImVec2 panel_pos(
        viewport->WorkPos.x + edge_buffer,
        viewport->WorkPos.y + edge_buffer
    );

    const ImVec2 panel_size(
        (std::max)(280.0f, viewport->WorkSize.x * 0.15f),
        (std::max)(240.0f, viewport->WorkSize.y - edge_buffer * 2.0f)
    );

    ImGui::SetNextWindowPos(panel_pos, ImGuiCond_Always);
    ImGui::SetNextWindowSize(panel_size, ImGuiCond_Always);
    ImGui::SetNextWindowCollapsed(true, ImGuiCond_FirstUseEver);

    ImGui::Begin(
        "Chat History",
        nullptr,
        ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoSavedSettings
    );

    if (config_.llm_options.show_system_prompt) {
        std::string static_prompt;
        std::string dynamic_prompt;

        {
            std::lock_guard<std::mutex> lock(mutex_);
            static_prompt = config_.buildStaticSystemPrompt();
            dynamic_prompt = last_augmented_system_prompt_.empty()
                ? config_.buildDynamicSystemPrompt()
                : last_augmented_system_prompt_;
        }

        ImGui::Text("System Prompt");
        ImGui::BeginChild("SystemPromptRegion", ImVec2(0.0f, 110.0f), true);
        ImGui::TextWrapped("%s", static_prompt.c_str());
        ImGui::Separator();
        ImGui::TextWrapped("%s", dynamic_prompt.c_str());
        ImGui::EndChild();

        ImGui::Separator();
    }

    ImGui::BeginChild("HistoryScrollRegion", ImVec2(0.0f, 0.0f), true);

    {
        std::lock_guard<std::mutex> lock(mutex_);

        for (const Entry& msg : messages_) {
            if (msg.role == "user") {
                ImGui::TextWrapped("%s: %s", config_.userName().c_str(), msg.text.c_str());
            } else if (msg.role == "assistant") {
                ImGui::TextWrapped("%s: %s", config_.llmName().c_str(), msg.text.c_str());
            } else {
                ImGui::TextWrapped("%s", msg.text.c_str());
            }

            ImGui::Spacing();
        }

        if (scroll_history_to_bottom_) {
            ImGui::SetScrollHereY(1.0f);
            scroll_history_to_bottom_ = false;
        }
    }

    ImGui::EndChild();
    ImGui::End();
}

void ChatUI::drawDialogueBubbles() {
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    if (!viewport) {
        return;
    }

    std::vector<DialogueBubble> bubbles_copy;
    bool generating_now = generating_.load();

    {
        std::lock_guard<std::mutex> lock(mutex_);
        bubbles_copy = visible_bubbles_;
    }

    if (generating_now) {
        bubbles_copy.push_back({ loadingDotsText() });
    }

    if (bubbles_copy.empty()) {
        return;
    }

    ImDrawList* draw = ImGui::GetForegroundDrawList();

    const ImVec2 anchor(
        viewport->WorkPos.x + viewport->WorkSize.x * speaker_anchor_normalized_.x,
        viewport->WorkPos.y + viewport->WorkSize.y * speaker_anchor_normalized_.y
    );

    const float bubble_max_width = (std::min)(420.0f, viewport->WorkSize.x * 0.33f);
    const float pad_x = 16.0f;
    const float pad_y = 12.0f;
    const float spacing_y = 10.0f;
    const float rounding = 18.0f;
    const float wrap_width = bubble_max_width - pad_x * 2.0f;

    float current_y = anchor.y + bubble_stack_offset_y_;
    const float start_x = anchor.x + bubble_stack_offset_x_;

    for (std::size_t i = 0; i < bubbles_copy.size(); ++i) {
        const bool draw_tail = (i == 0);

        const ImVec2 text_size = ImGui::CalcTextSize(
            bubbles_copy[i].text.c_str(),
            nullptr,
            false,
            wrap_width
        );

        const float bubble_w = text_size.x + pad_x * 2.0f;
        const float bubble_h = text_size.y + pad_y * 2.0f;

        const ImVec2 rect_min(start_x, current_y);
        const ImVec2 rect_max(start_x + bubble_w, current_y + bubble_h);

        const ImU32 fill_col = IM_COL32(248, 248, 252, 255);
        const ImU32 text_col = IM_COL32(20, 20, 24, 255);

        draw->AddRectFilled(rect_min, rect_max, fill_col, rounding);

        if (draw_tail) {
            const ImVec2 tail_a(
                rect_min.x + tail_attach_x_,
                rect_min.y + tail_attach_y_
            );

            const ImVec2 tail_b(
                rect_min.x + tail_attach_x_,
                rect_min.y + tail_attach_y_ + tail_height_
            );

            const ImVec2 tail_c(
                rect_min.x + tail_attach_x_ - tail_width_,
                rect_min.y + tail_attach_y_
            );

            draw->AddTriangleFilled(tail_a, tail_b, tail_c, fill_col);
        }

        const ImVec2 text_pos(rect_min.x + pad_x, rect_min.y + pad_y);

        draw->AddText(
            nullptr,
            0.0f,
            text_pos,
            text_col,
            bubbles_copy[i].text.c_str(),
            nullptr,
            wrap_width
        );

        current_y += bubble_h + spacing_y;
    }
}

void ChatUI::draw() {
    drawTopClock();
    drawDialogueBubbles();
    drawInputBar();
    drawHistoryPanel();
}