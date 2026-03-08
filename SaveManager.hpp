// SaveManager.hpp
#pragma once

#include "AppConfig.hpp"

#include <string>
#include <vector>

struct SaveEntry {
    std::string display_name;
    std::string safe_name;
    std::string directory_path;
    std::string config_path;
    std::string last_opened_iso8601;
};

class SaveManager {
public:
    static std::string sanitizeSaveName(const std::string& input);
    static std::string sanitizeDisplayName(const std::string& input);

    static bool ensureSavesDirectory(std::string& out_error);

    static bool listSaves(
        const std::string& saves_root,
        std::vector<SaveEntry>& out_saves,
        std::string& out_error
    );

    static bool createSaveFromTemplate(
        const std::string& template_config_path,
        const std::string& saves_root,
        const std::string& requested_save_name,
        const std::string& llm_name,
        const std::string& user_name,
        SaveEntry& out_entry,
        std::string& out_error
    );

    static bool loadSaveConfig(
        const SaveEntry& entry,
        AppConfig& out_config,
        std::string& out_error
    );

    static bool saveSaveConfig(
        const SaveEntry& entry,
        const AppConfig& config,
        std::string& out_error
    );

    static bool deleteSave(
        const SaveEntry& entry,
        std::string& out_error
    );

    static bool markSaveOpenedNow(
        const SaveEntry& entry,
        std::string& out_error
    );
};