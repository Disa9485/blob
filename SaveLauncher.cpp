// SaveLauncher.cpp
#include "SaveLauncher.hpp"

#include "imgui.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <string>
#include <ctime>
#include <iomanip>
#include <sstream>

namespace {
    template <typename T>
    void clampValue(T& value, T min_value, T max_value) {
        value = (std::max)(min_value, (std::min)(value, max_value));
    }

    struct ResolutionOption {
        int width;
        int height;
        const char* label;
    };

    constexpr std::array<ResolutionOption, 10> kCommonResolutions = {{
        { 1280,  720, "1280 x 720  (720p)" },
        { 1366,  768, "1366 x 768" },
        { 1600,  900, "1600 x 900" },
        { 1920, 1080, "1920 x 1080 (1080p)" },
        { 1920, 1200, "1920 x 1200" },
        { 2560, 1440, "2560 x 1440 (1440p)" },
        { 2560, 1600, "2560 x 1600" },
        { 3440, 1440, "3440 x 1440 (Ultrawide)" },
        { 3840, 2160, "3840 x 2160 (4K)" },
        { 7680, 4320, "7680 x 4320 (8K)" }
    }};

    struct AntiAliasingOption {
        int samples;
        const char* label;
    };

    constexpr std::array<AntiAliasingOption, 5> kAntiAliasingOptions = {{
        { 0,  "Off" },
        { 2,  "MSAA 2x" },
        { 4,  "MSAA 4x" },
        { 8,  "MSAA 8x" },
        { 16, "MSAA 16x" }
    }};

    int nearestPowerOfTwoIndex(int value, int min_power, int max_power) {
        int best_power = min_power;
        int best_diff = std::abs(value - (1 << min_power));

        for (int p = min_power + 1; p <= max_power; ++p) {
            const int candidate = 1 << p;
            const int diff = std::abs(value - candidate);
            if (diff < best_diff) {
                best_diff = diff;
                best_power = p;
            }
        }

        return best_power - min_power;
    }

    void drawDelayedTooltip(const char* hover_key, const char* text, float delay_seconds = 0.6f) {
        static std::string active_hover_key;
        static double hover_start_time = 0.0;

        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
            if (active_hover_key != hover_key) {
                active_hover_key = hover_key;
                hover_start_time = ImGui::GetTime();
            }

            if ((ImGui::GetTime() - hover_start_time) >= delay_seconds) {
                ImGui::BeginTooltip();
                ImGui::PushTextWrapPos(ImGui::GetFontSize() * 28.0f);
                ImGui::TextUnformatted(text);
                ImGui::PopTextWrapPos();
                ImGui::EndTooltip();
            }
        } else if (active_hover_key == hover_key) {
            active_hover_key.clear();
            hover_start_time = 0.0;
        }
    }

    bool checkboxWithTooltip(const char* label, bool* value, const char* tooltip) {
        const bool changed = ImGui::Checkbox(label, value);
        drawDelayedTooltip(label, tooltip);
        return changed;
    }

    bool sliderIntWithTooltip(
        const char* label,
        int* value,
        int min_value,
        int max_value,
        const char* tooltip,
        const char* format = "%d")
    {
        const bool changed = ImGui::SliderInt(label, value, min_value, max_value, format);
        drawDelayedTooltip(label, tooltip);
        return changed;
    }

    bool sliderFloatWithTooltip(
        const char* label,
        float* value,
        float min_value,
        float max_value,
        const char* tooltip,
        const char* format = "%.3f")
    {
        const bool changed = ImGui::SliderFloat(label, value, min_value, max_value, format);
        drawDelayedTooltip(label, tooltip);
        return changed;
    }

    bool parseIso8601Local(const std::string& iso, std::tm& out_tm) {
        std::istringstream in(iso);
        in >> std::get_time(&out_tm, "%Y-%m-%dT%H:%M:%S");
        return !in.fail();
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

    std::string formatLastOpenedLabel(const std::string& iso) {
        if (iso.empty()) {
            return "New";
        }
        return "Last Opened: " + formatHumanDateFromIso8601(iso);
    }
}

SaveLauncher::SaveLauncher(
    std::string template_config_path,
    std::string saves_root)
    : template_config_path_(std::move(template_config_path))
    , saves_root_(std::move(saves_root)) {
    std::memset(create_save_name_, 0, sizeof(create_save_name_));
    std::memset(create_llm_name_, 0, sizeof(create_llm_name_));
    std::memset(create_user_name_, 0, sizeof(create_user_name_));
}

bool SaveLauncher::initialize(std::string& out_error) {
    if (!SaveManager::ensureSavesDirectory(out_error)) {
        return false;
    }

    refreshSaveList();
    return true;
}

bool SaveLauncher::hasSelectedSave() const {
    return selected_ready_;
}

const SaveEntry& SaveLauncher::selectedSave() const {
    return selected_save_;
}

const AppConfig& SaveLauncher::selectedConfig() const {
    return selected_config_;
}

void SaveLauncher::refreshSaveList() {
    std::string error;
    if (!SaveManager::listSaves(saves_root_, saves_, error)) {
        last_error_ = error;
    } else {
        last_error_.clear();
    }

    if (selected_index_ >= static_cast<int>(saves_.size())) {
        selected_index_ = -1;
    }

    if (settings_index_ >= static_cast<int>(saves_.size())) {
        closeSettingsEditor();
    }
}

void SaveLauncher::drawDeleteSavePopup() {
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    if (viewport) {
        ImGui::SetNextWindowPos(
            ImVec2(
                viewport->WorkPos.x + viewport->WorkSize.x * 0.5f,
                viewport->WorkPos.y + viewport->WorkSize.y * 0.5f
            ),
            ImGuiCond_Always,
            ImVec2(0.5f, 0.5f)
        );
    }

    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 10.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);

    if (ImGui::BeginPopupModal("Are you sure?", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        if (pending_delete_index_ < 0 || pending_delete_index_ >= static_cast<int>(saves_.size())) {
            pending_delete_index_ = -1;
            ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
            ImGui::PopStyleVar(2);
            return;
        }

        const SaveEntry& save = saves_[pending_delete_index_];
        ImGui::TextWrapped(
            "This will permanently delete \"%s\" and all of its files.",
            save.safe_name.c_str()
        );
        ImGui::Spacing();

        if (ImGui::Button("Yes", ImVec2(120.0f, 0.0f))) {
            std::string error;
            if (!SaveManager::deleteSave(save, error)) {
                last_error_ = error;
            } else {
                last_error_.clear();
                refreshSaveList();
            }

            pending_delete_index_ = -1;
            ImGui::CloseCurrentPopup();
        }

        ImGui::SameLine();

        if (ImGui::Button("No", ImVec2(120.0f, 0.0f))) {
            pending_delete_index_ = -1;
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
    }

    ImGui::PopStyleVar(2);
}

bool SaveLauncher::beginSettingsEdit(int index) {
    if (index < 0 || index >= static_cast<int>(saves_.size())) {
        last_error_ = "Invalid save selection.";
        return false;
    }

    AppConfig config;
    std::string error;
    if (!SaveManager::loadSaveConfig(saves_[index], config, error)) {
        last_error_ = error;
        return false;
    }

    settings_index_ = index;
    settings_config_ = config;
    settings_loaded_ = true;
    in_settings_ = true;
    last_error_.clear();
    return true;
}

void SaveLauncher::closeSettingsEditor() {
    in_settings_ = false;
    settings_index_ = -1;
    settings_loaded_ = false;
    settings_config_ = AppConfig{};
}

bool SaveLauncher::drawPowerOfTwoCtxSelector(
    const char* label,
    int& value,
    int min_power,
    int max_power,
    const char* tooltip)
{
    const int count = max_power - min_power + 1;
    int current_index = nearestPowerOfTwoIndex(value, min_power, max_power);

    std::string preview = std::to_string(1 << (min_power + current_index));
    bool changed = false;

    if (ImGui::BeginCombo(label, preview.c_str())) {
        for (int i = 0; i < count; ++i) {
            const int pow = min_power + i;
            const int candidate = 1 << pow;
            const bool selected = (i == current_index);

            std::string option = std::to_string(candidate);
            if (ImGui::Selectable(option.c_str(), selected)) {
                value = candidate;
                changed = true;
            }

            if (selected) {
                ImGui::SetItemDefaultFocus();
            }
        }
        ImGui::EndCombo();
    }

    drawDelayedTooltip(label, tooltip);
    return changed;
}

bool SaveLauncher::drawResolutionCombo(
    const char* label,
    int& width,
    int& height,
    const char* tooltip)
{
    int current_index = -1;
    for (int i = 0; i < static_cast<int>(kCommonResolutions.size()); ++i) {
        if (kCommonResolutions[i].width == width &&
            kCommonResolutions[i].height == height) {
            current_index = i;
            break;
        }
    }

    std::string preview = current_index >= 0
        ? kCommonResolutions[current_index].label
        : (std::to_string(width) + " x " + std::to_string(height));

    bool changed = false;

    if (ImGui::BeginCombo(label, preview.c_str())) {
        for (int i = 0; i < static_cast<int>(kCommonResolutions.size()); ++i) {
            const bool selected = (i == current_index);
            if (ImGui::Selectable(kCommonResolutions[i].label, selected)) {
                width = kCommonResolutions[i].width;
                height = kCommonResolutions[i].height;
                changed = true;
            }

            if (selected) {
                ImGui::SetItemDefaultFocus();
            }
        }
        ImGui::EndCombo();
    }

    drawDelayedTooltip(label, tooltip);
    return changed;
}

void SaveLauncher::draw() {
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos, ImGuiCond_Always);
    ImGui::SetNextWindowSize(viewport->WorkSize, ImGuiCond_Always);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);

    ImGui::Begin("Save Launcher", nullptr,
        ImGuiWindowFlags_NoTitleBar |
        ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove);

    if (!last_error_.empty()) {
        ImGui::TextWrapped("Error: %s", last_error_.c_str());
        ImGui::Separator();
    }

    if (!in_settings_) {
        drawSaveList();
    } else {
        drawSettingsEditor();
    }

    drawCreateSavePopup();
    drawDeleteSavePopup();

    ImGui::End();

    ImGui::PopStyleVar(2);
}

void SaveLauncher::drawSaveList() {
    ImGui::Text("Saves");
    ImGui::Separator();

    ImGui::BeginChild("SaveListRegion", ImVec2(0.0f, -ImGui::GetFrameHeightWithSpacing() * 2.0f), true);

    for (int i = 0; i < static_cast<int>(saves_.size()); ++i) {
        const SaveEntry& save = saves_[i];

        ImGui::PushID(i);

        ImGui::TextUnformatted(save.safe_name.c_str());
        ImGui::SameLine();
        ImGui::TextDisabled("(%s)", formatLastOpenedLabel(save.last_opened_iso8601).c_str());

        const float button_width = 90.0f;
        const float spacing = ImGui::GetStyle().ItemSpacing.x;
        const float total_button_width = button_width * 3.0f + spacing * 2.0f;

        float x = ImGui::GetCursorPosX();
        const float avail = ImGui::GetContentRegionAvail().x;
        if (avail > total_button_width) {
            x += avail - total_button_width;
        }
        ImGui::SetCursorPosX(x);

        if (ImGui::Button("Load", ImVec2(button_width, 0.0f))) {
            AppConfig cfg;
            std::string error;
            if (SaveManager::loadSaveConfig(save, cfg, error)) {
                selected_save_ = save;
                selected_config_ = cfg;
                selected_ready_ = true;
            } else {
                last_error_ = error;
            }
        }

        ImGui::SameLine();

        if (ImGui::Button("Settings", ImVec2(button_width, 0.0f))) {
            beginSettingsEdit(i);
        }

        ImGui::SameLine();

        if (ImGui::Button("Delete", ImVec2(button_width, 0.0f))) {
            pending_delete_index_ = i;
            pending_open_delete_popup_ = true;
        }

        ImGui::Separator();
        ImGui::PopID();
    }

    ImGui::EndChild();

    if (pending_open_delete_popup_) {
        ImGui::OpenPopup("Are you sure?");
        pending_open_delete_popup_ = false;
    }

    if (ImGui::Button("Create Save")) {
        ImGui::OpenPopup("Create Save");
    }

    ImGui::SameLine();

    if (ImGui::Button("Refresh")) {
        refreshSaveList();
    }
}

bool SaveLauncher::drawAntiAliasingCombo(
    const char* label,
    int& samples,
    const char* tooltip)
{
    int current_index = 0;
    for (int i = 0; i < static_cast<int>(kAntiAliasingOptions.size()); ++i) {
        if (kAntiAliasingOptions[i].samples == samples) {
            current_index = i;
            break;
        }
    }

    const char* preview = kAntiAliasingOptions[current_index].label;
    bool changed = false;

    if (ImGui::BeginCombo(label, preview)) {
        for (int i = 0; i < static_cast<int>(kAntiAliasingOptions.size()); ++i) {
            const bool selected = (i == current_index);
            if (ImGui::Selectable(kAntiAliasingOptions[i].label, selected)) {
                samples = kAntiAliasingOptions[i].samples;
                changed = true;
            }
            if (selected) {
                ImGui::SetItemDefaultFocus();
            }
        }
        ImGui::EndCombo();
    }

    drawDelayedTooltip(label, tooltip);
    return changed;
}

void SaveLauncher::drawSettingsEditor() {
    if (!settings_loaded_ ||
        settings_index_ < 0 ||
        settings_index_ >= static_cast<int>(saves_.size())) {
        closeSettingsEditor();
        return;
    }

    const SaveEntry& save = saves_[settings_index_];
    AppConfig& config = settings_config_;

    ImGui::Text("Settings: %s", save.safe_name.c_str());
    ImGui::Separator();

    ImGui::BeginChild("SaveSettingsRegion", ImVec2(0.0f, -ImGui::GetFrameHeightWithSpacing() * 2.5f), true);

    if (ImGui::CollapsingHeader("Window", ImGuiTreeNodeFlags_DefaultOpen)) {
        drawResolutionCombo(
            "Resolution",
            config.window.width,
            config.window.height,
            "Window size used when fullscreen is off."
        );

        checkboxWithTooltip(
            "Fullscreen",
            &config.window.fullscreen,
            "Launches the game in fullscreen mode."
        );

        checkboxWithTooltip(
            "VSync",
            &config.window.vsync,
            "Synchronizes frame presentation to your monitor refresh rate to reduce tearing."
        );

        drawAntiAliasingCombo(
            "Anti-Aliasing",
            config.window.anti_aliasing_samples,
            "Controls multisample anti-aliasing. Higher values smooth edges better but may reduce performance. Applied when launching the save."
        );

        checkboxWithTooltip(
            "Debug Overlay",
            &config.window.debug_mode,
            "Shows real-time performance information in the top-right corner during gameplay."
        );
    }

    if (ImGui::CollapsingHeader("LLM (Advanced)")) {
        sliderFloatWithTooltip(
            "Talk On Boot Chance",
            &config.autonomous_speech.talk_on_boot_chance,
            0.0f,
            1.0f,
            "Chance that the robot speaks on its own when the save first loads.",
            "%.2f"
        );

        sliderFloatWithTooltip(
            "Talk On Touch Chance",
            &config.autonomous_speech.talk_on_touch_chance,
            0.0f,
            1.0f,
            "Chance that the robot speaks on its own when the user starts touching it.",
            "%.2f"
        );

        sliderFloatWithTooltip(
            "Talk Per Minute Chance",
            &config.autonomous_speech.talk_per_minute_chance,
            0.0f,
            1.0f,
            "Chance per minute that the robot speaks on its own while the user is present but silent.",
            "%.2f"
        );

        checkboxWithTooltip(
            "Greedy",
            &config.llm_options.greedy,
            "Makes responses more deterministic and repeatable. Usually less creative, but more stable."
        );

        sliderIntWithTooltip(
            "History Limit",
            &config.llm_options.history_limit,
            2,
            64,
            "How many recent chat messages stay in the model's active conversation history. Higher values improve continuity but use more context."
        );

        drawPowerOfTwoCtxSelector(
            "n_ctx",
            config.llm_options.n_ctx,
            8,
            15,
            "Maximum context window size. Larger values let the model consider more prior text, but use more memory and can be slower."
        );

        checkboxWithTooltip(
            "Use GPU",
            &config.llm_options.use_gpu,
            "Enables Vulkan GPU acceleration for llama.cpp if available. Disable this to force true CPU-only inference."
        );

        if (!config.llm_options.use_gpu) {
            ImGui::BeginDisabled();
        }

        sliderIntWithTooltip(
            "n_gpu_layers",
            &config.llm_options.n_gpu_layers,
            0,
            200,
            "How many model layers to offload to the GPU when GPU usage is enabled. Higher can be faster if your GPU has enough VRAM."
        );

        if (!config.llm_options.use_gpu) {
            ImGui::EndDisabled();
        }

        sliderIntWithTooltip(
            "n_predict",
            &config.llm_options.n_predict,
            16,
            2048,
            "Maximum number of tokens the model may generate for one reply. Higher values allow longer responses."
        );

        sliderIntWithTooltip(
            "n_threads",
            &config.llm_options.n_threads,
            1,
            64,
            "CPU threads used for inference. More threads may improve speed up to a point, depending on your processor."
        );

        checkboxWithTooltip(
            "Show System Prompt",
            &config.llm_options.show_system_prompt,
            "Shows the static and dynamic system prompt in the chat panel for debugging."
        );

        sliderFloatWithTooltip(
            "Temperature",
            &config.llm_options.temperature,
            0.0f,
            2.0f,
            "Controls randomness when greedy mode is off. Lower is more focused and predictable. Higher is more varied.",
            "%.2f"
        );

        sliderIntWithTooltip(
            "Top K",
            &config.llm_options.top_k,
            1,
            200,
            "Limits token selection to the top K most likely choices when greedy mode is off. Lower values are more conservative."
        );

        sliderFloatWithTooltip(
            "Top P",
            &config.llm_options.top_p,
            0.05f,
            1.0f,
            "Nucleus sampling cutoff when greedy mode is off. Lower values are more selective, higher values allow more variety.",
            "%.2f"
        );
    }

    if (ImGui::CollapsingHeader("TTS (Advanced)")) {
        checkboxWithTooltip(
            "TTS Enabled",
            &config.tts.enabled,
            "Enables spoken voice output."
        );

        checkboxWithTooltip(
            "Warp Enabled",
            &config.tts.warp.enabled,
            "Enables the robotic voice effect processing after speech synthesis."
        );
    }

    if (ImGui::CollapsingHeader("Memory (Advanced)")) {
        checkboxWithTooltip(
            "Memory Enabled",
            &config.memory.enabled,
            "Enables semantic memory retrieval from prior conversation history."
        );

        sliderIntWithTooltip(
            "Memory n_threads",
            &config.memory.n_threads,
            1,
            64,
            "CPU threads used for embedding generation in the memory system."
        );

        sliderIntWithTooltip(
            "Memory n_batch",
            &config.memory.n_batch,
            1,
            4096,
            "Batch size used by the embedding model. Larger values may improve throughput but use more memory."
        );

        sliderIntWithTooltip(
            "Memory n_ubatch",
            &config.memory.n_ubatch,
            1,
            4096,
            "Micro-batch size used by the embedding model internals."
        );

        drawPowerOfTwoCtxSelector(
            "Memory n_ctx",
            config.memory.n_ctx,
            6,
            12,
            "Context window used by the embedding model. Larger values allow longer memory text chunks."
        );

        sliderIntWithTooltip(
            "Retrieval Top K",
            &config.memory.retrieval_top_k,
            1,
            32,
            "Maximum number of candidate memories to retrieve before filtering."
        );

        sliderFloatWithTooltip(
            "Retrieval Score Threshold",
            &config.memory.retrieval_score_threshold,
            -1.0f,
            1.0f,
            "Minimum similarity score required for a memory to be included. Higher values are stricter.",
            "%.2f"
        );

        sliderIntWithTooltip(
            "Session Recent Exclusion Count",
            &config.memory.session_recent_exclusion_count,
            1,
            200,
            "Skips very recent messages from the current session during retrieval, since they are likely still in active context."
        );
    }

    ImGui::EndChild();

    clampValue(config.llm_options.history_limit, 2, 64);
    clampValue(config.llm_options.n_ctx, 256, 32768);
    clampValue(config.llm_options.n_gpu_layers, 0, 200);
    clampValue(config.llm_options.n_predict, 16, 2048);
    clampValue(config.llm_options.n_threads, 1, 64);
    clampValue(config.llm_options.temperature, 0.0f, 2.0f);
    clampValue(config.llm_options.top_k, 1, 200);
    clampValue(config.llm_options.top_p, 0.05f, 1.0f);
    clampValue(config.autonomous_speech.talk_on_boot_chance, 0.0f, 1.0f);
    clampValue(config.autonomous_speech.talk_on_touch_chance, 0.0f, 1.0f);
    clampValue(config.autonomous_speech.talk_per_minute_chance, 0.0f, 1.0f);

    clampValue(config.memory.n_threads, 1, 64);
    clampValue(config.memory.n_batch, 1, 4096);
    clampValue(config.memory.n_ubatch, 1, 4096);
    clampValue(config.memory.n_ctx, 64, 4096);
    clampValue(config.memory.retrieval_top_k, 1, 32);
    clampValue(config.memory.retrieval_score_threshold, -1.0f, 1.0f);
    clampValue(config.memory.session_recent_exclusion_count, 1, 200);

    if (ImGui::Button("Save Settings")) {
        std::string save_error;
        if (!SaveManager::saveSaveConfig(save, config, save_error)) {
            last_error_ = save_error;
        } else {
            last_error_.clear();
            refreshSaveList();
        }
    }

    ImGui::SameLine();

    if (ImGui::Button("Back")) {
        closeSettingsEditor();
    }
}

void SaveLauncher::drawCreateSavePopup() {
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    if (viewport) {
        ImGui::SetNextWindowPos(
            ImVec2(
                viewport->WorkPos.x + viewport->WorkSize.x * 0.5f,
                viewport->WorkPos.y + viewport->WorkSize.y * 0.5f
            ),
            ImGuiCond_Always,
            ImVec2(0.5f, 0.5f)
        );
    }

    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 10.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);

    if (ImGui::BeginPopupModal("Create Save", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::InputText("Save Name", create_save_name_, sizeof(create_save_name_));
        ImGui::InputText("LLM Name", create_llm_name_, sizeof(create_llm_name_));
        ImGui::InputText("User Name", create_user_name_, sizeof(create_user_name_));

        if (ImGui::Button("Create")) {
            SaveEntry new_entry;
            std::string error;
            if (SaveManager::createSaveFromTemplate(
                    template_config_path_,
                    saves_root_,
                    create_save_name_,
                    create_llm_name_,
                    create_user_name_,
                    new_entry,
                    error)) {
                std::memset(create_save_name_, 0, sizeof(create_save_name_));
                std::memset(create_llm_name_, 0, sizeof(create_llm_name_));
                std::memset(create_user_name_, 0, sizeof(create_user_name_));
                refreshSaveList();
                ImGui::CloseCurrentPopup();
            } else {
                last_error_ = error;
            }
        }

        ImGui::SameLine();

        if (ImGui::Button("Cancel")) {
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
    }

    ImGui::PopStyleVar(2);
}