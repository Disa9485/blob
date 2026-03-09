// AppConfig.hpp
#pragma once

#include "LlamaChat.hpp"

#include <string>
#include <vector>

struct WindowConfig {
    int width = 1280;
    int height = 720;
    std::string glsl_version = "#version 330";
    bool vsync = true;
    bool fullscreen = false;
};

struct TtsWarpConfig {
    bool enabled = true;

    bool lower_sentence_end_enabled = true;
    int tail_ms = 150;
    float tail_drop = 0.95f;

    bool ring_mod_enabled = true;
    float ring_mod_frequency_hz = 30.0f;
    float ring_mod_mix = 1.0f; // 1.0 = full effect, 0.0 = dry

    bool bitcrush_enabled = true;
    int bit_depth = 8;

    bool hard_clip_enabled = true;
    float clip_level = 0.61f; // normalized float space, ~20000/32768

    float output_gain = 1.0f;
};

struct TtsConfig {
    bool enabled = true;
    std::string model_path = "voices/en_GB-jenny_dioco-medium.onnx";
    std::string model_config_path = "voices/en_GB-jenny_dioco-medium.onnx.json";

    int speaker_id = 0;
    float length_scale = 1.0f;
    float noise_scale = -1.0f;
    float noise_w_scale = -1.0f;

    TtsWarpConfig warp;
};

struct MemoryConfig {
    bool enabled = true;
    std::string embedding_model_path = "BAAI/bge-small-en-v1.5.gguf";
    std::string database_path = "conversation_memory.db";
    std::string faiss_index_path = "conversation_memory.faiss";
    int n_threads = 4;
    int n_batch = 512;
    int n_ubatch = 512;
    int n_ctx = 512;
    bool normalize = true;
    bool store_raw_text = true;
    int retrieval_top_k = 3;
    bool rebuild_faiss_if_missing = true;
    float retrieval_score_threshold = 0.7f;
    int session_recent_exclusion_count = 20;
};

struct AppConfig {
    static constexpr std::size_t kSystemPromptNameIndex = 1;
    static constexpr std::size_t kSystemPromptUserNameIndex = 6;

    std::string model_path = "model.gguf";
    std::string last_opened_iso8601;
    std::string current_room = "lab";
    WindowConfig window;
    std::vector<std::string> static_system_prompt = {
        "You are named ",
        "Assistant",
        ".",
        "You are talking to ",
        "",
        "",
        "John",
        "."
    };

    std::vector<std::string> dynamic_system_prompt = {
    };
    LlamaChat::Options llm_options;
    TtsConfig tts;
    MemoryConfig memory;

    std::string buildStaticSystemPrompt() const;
    std::string buildDynamicSystemPrompt() const;
    std::string llmName() const;
    void setLlmName(const std::string& name);
    std::string userName() const;
    void setUserName(const std::string& name);
};

bool loadAppConfig(const std::string& path, AppConfig& out_config, std::string& out_error);
bool saveAppConfig(const std::string& path, const AppConfig& config, std::string& out_error);