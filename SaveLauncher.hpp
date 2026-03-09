// SaveLauncher.hpp
#pragma once

#include "AppConfig.hpp"
#include "SaveManager.hpp"

#include <string>
#include <vector>

class SaveLauncher {
public:
    SaveLauncher(
        std::string template_config_path,
        std::string saves_root
    );

    bool initialize(std::string& out_error);
    void draw();

    bool hasSelectedSave() const;
    const SaveEntry& selectedSave() const;
    const AppConfig& selectedConfig() const;

private:
    void refreshSaveList();
    void drawSaveList();
    void drawSettingsEditor();
    void drawCreateSavePopup();
    void drawDeleteSavePopup();

    bool beginSettingsEdit(int index);
    void closeSettingsEditor();

    bool drawPowerOfTwoCtxSelector(
        const char* label,
        int& value,
        int min_power,
        int max_power,
        const char* tooltip
    );
    bool drawResolutionCombo(const char* label, int& width, int& height, const char* tooltip);

private:
    std::string template_config_path_;
    std::string saves_root_;

    std::vector<SaveEntry> saves_;
    int selected_index_ = -1;

    bool in_settings_ = false;
    int settings_index_ = -1;
    AppConfig settings_config_{};
    bool settings_loaded_ = false;

    bool selected_ready_ = false;
    SaveEntry selected_save_;
    AppConfig selected_config_;

    std::string status_message_;
    std::string last_error_;

    int pending_delete_index_ = -1;
    bool pending_open_delete_popup_ = false;

    static constexpr int kBufferSize = 256;
    char create_save_name_[kBufferSize]{};
    char create_llm_name_[kBufferSize]{};
    char create_user_name_[kBufferSize]{};
};