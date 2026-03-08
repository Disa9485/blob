// SaveManager.cpp
#include "SaveManager.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <filesystem>
#include <iomanip>
#include <sstream>

namespace fs = std::filesystem;

namespace {
    std::string trim(const std::string& s) {
        std::size_t start = 0;
        while (start < s.size() && std::isspace(static_cast<unsigned char>(s[start]))) {
            ++start;
        }

        std::size_t end = s.size();
        while (end > start && std::isspace(static_cast<unsigned char>(s[end - 1]))) {
            --end;
        }

        return s.substr(start, end - start);
    }

    std::string joinPath(const fs::path& p) {
        return p.string();
    }

    std::string formatTimestampIso8601NowLocal() {
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
}

std::string SaveManager::sanitizeSaveName(const std::string& input) {
    std::string s = trim(input);
    std::string out;
    out.reserve(s.size());

    for (char ch : s) {
        const unsigned char u = static_cast<unsigned char>(ch);
        if (std::isalnum(u) || ch == '_' || ch == '-') {
            out.push_back(ch);
        } else if (std::isspace(u)) {
            out.push_back('_');
        }
    }

    while (!out.empty() && (out.front() == '_' || out.front() == '-')) {
        out.erase(out.begin());
    }
    while (!out.empty() && (out.back() == '_' || out.back() == '-')) {
        out.pop_back();
    }

    if (out.empty()) {
        out = "save";
    }

    return out;
}

std::string SaveManager::sanitizeDisplayName(const std::string& input) {
    std::string s = trim(input);
    std::string out;
    out.reserve(s.size());

    for (char ch : s) {
        const unsigned char u = static_cast<unsigned char>(ch);
        if (std::isprint(u) && ch != '\n' && ch != '\r' && ch != '\t') {
            out.push_back(ch);
        }
    }

    if (out.empty()) {
        out = "Unnamed";
    }

    return out;
}

bool SaveManager::ensureSavesDirectory(std::string& out_error) {
    try {
        fs::create_directories("saves");
        return true;
    } catch (const std::exception& e) {
        out_error = e.what();
        return false;
    }
}

bool SaveManager::listSaves(
    const std::string& saves_root,
    std::vector<SaveEntry>& out_saves,
    std::string& out_error)
{
    out_saves.clear();

    try {
        fs::create_directories(saves_root);

        for (const fs::directory_entry& entry : fs::directory_iterator(saves_root)) {
            if (!entry.is_directory()) {
                continue;
            }

            const fs::path dir = entry.path();
            const std::string safe_name = dir.filename().string();
            const fs::path config_path = dir / (safe_name + ".json");

            if (!fs::exists(config_path)) {
                continue;
            }

            SaveEntry save;
            save.safe_name = safe_name;
            save.display_name = safe_name;
            save.directory_path = joinPath(dir);
            save.config_path = joinPath(config_path);

            AppConfig cfg;
            std::string cfg_error;
            if (loadAppConfig(save.config_path, cfg, cfg_error)) {
                save.display_name = cfg.userName() + " / " + cfg.llmName() + " / " + safe_name;
                save.last_opened_iso8601 = cfg.last_opened_iso8601;
            }

            out_saves.push_back(std::move(save));
        }

        std::sort(out_saves.begin(), out_saves.end(),
            [](const SaveEntry& a, const SaveEntry& b) {
                // Newest opened first. Empty timestamps sink to the bottom.
                if (a.last_opened_iso8601.empty() && b.last_opened_iso8601.empty()) {
                    return a.safe_name < b.safe_name;
                }
                if (a.last_opened_iso8601.empty()) {
                    return false;
                }
                if (b.last_opened_iso8601.empty()) {
                    return true;
                }
                if (a.last_opened_iso8601 != b.last_opened_iso8601) {
                    return a.last_opened_iso8601 > b.last_opened_iso8601;
                }
                return a.safe_name < b.safe_name;
            });

        return true;
    } catch (const std::exception& e) {
        out_error = e.what();
        return false;
    }
}

bool SaveManager::createSaveFromTemplate(
    const std::string& template_config_path,
    const std::string& saves_root,
    const std::string& requested_save_name,
    const std::string& llm_name,
    const std::string& user_name,
    SaveEntry& out_entry,
    std::string& out_error)
{
    AppConfig config;
    if (!loadAppConfig(template_config_path, config, out_error)) {
        return false;
    }

    const std::string safe_name = sanitizeSaveName(requested_save_name);
    const std::string clean_llm_name = sanitizeDisplayName(llm_name);
    const std::string clean_user_name = sanitizeDisplayName(user_name);

    const fs::path save_dir = fs::path(saves_root) / safe_name;
    const fs::path config_path = save_dir / (safe_name + ".json");
    const fs::path db_path = save_dir / "conversation_memory.db";
    const fs::path faiss_path = save_dir / "conversation_memory.faiss";

    try {
        if (fs::exists(save_dir)) {
            out_error = "Save already exists: " + safe_name;
            return false;
        }

        fs::create_directories(save_dir);
    } catch (const std::exception& e) {
        out_error = e.what();
        return false;
    }

    config.setLlmName(clean_llm_name);
    config.setUserName(clean_user_name);
    config.last_opened_iso8601.clear();

    config.memory.database_path = joinPath(db_path);
    config.memory.faiss_index_path = joinPath(faiss_path);

    if (!saveAppConfig(joinPath(config_path), config, out_error)) {
        return false;
    }

    out_entry.display_name = safe_name;
    out_entry.safe_name = safe_name;
    out_entry.directory_path = joinPath(save_dir);
    out_entry.config_path = joinPath(config_path);
    out_entry.last_opened_iso8601.clear();
    return true;
}

bool SaveManager::loadSaveConfig(
    const SaveEntry& entry,
    AppConfig& out_config,
    std::string& out_error)
{
    return loadAppConfig(entry.config_path, out_config, out_error);
}

bool SaveManager::saveSaveConfig(
    const SaveEntry& entry,
    const AppConfig& config,
    std::string& out_error)
{
    return saveAppConfig(entry.config_path, config, out_error);
}

bool SaveManager::deleteSave(
    const SaveEntry& entry,
    std::string& out_error)
{
    try {
        if (!fs::exists(entry.directory_path)) {
            out_error = "Save directory does not exist.";
            return false;
        }

        fs::remove_all(entry.directory_path);
        return true;
    } catch (const std::exception& e) {
        out_error = e.what();
        return false;
    }
}

bool SaveManager::markSaveOpenedNow(
    const SaveEntry& entry,
    std::string& out_error)
{
    AppConfig config;
    if (!loadAppConfig(entry.config_path, config, out_error)) {
        return false;
    }

    config.last_opened_iso8601 = formatTimestampIso8601NowLocal();

    if (!saveAppConfig(entry.config_path, config, out_error)) {
        return false;
    }

    return true;
}