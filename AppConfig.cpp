// AppConfig.cpp
#include "AppConfig.hpp"

#include <fstream>
#include <sstream>
#include <algorithm>

#include <nlohmann/json.hpp>

using json = nlohmann::json;

namespace {
    template <typename T>
    void readIfPresent(const json& j, const char* key, T& out_value) {
        auto it = j.find(key);
        if (it != j.end() && !it->is_null()) {
            out_value = it->get<T>();
        }
    }

    void replaceAllInPlace(std::string& text, const std::string& from, const std::string& to) {
        if (from.empty()) {
            return;
        }

        std::size_t pos = 0;
        while ((pos = text.find(from, pos)) != std::string::npos) {
            text.replace(pos, from.size(), to);
            pos += to.size();
        }
    }

    std::string expandPromptTags(
        const std::vector<std::string>& parts,
        const std::string& llm_name,
        const std::string& user_name)
    {
        std::ostringstream out;

        for (const std::string& part : parts) {
            if (part.empty()) {
                continue;
            }

            std::string expanded = part;
            replaceAllInPlace(expanded, "<llm_name>", llm_name);
            replaceAllInPlace(expanded, "<user_name>", user_name);
            out << expanded;
        }

        return out.str();
    }
}

std::string AppConfig::buildStaticSystemPrompt() const {
    return expandPromptTags(static_system_prompt, llm_name, user_name);
}

std::string AppConfig::buildDynamicSystemPrompt() const {
    return expandPromptTags(dynamic_system_prompt, llm_name, user_name);
}

std::string AppConfig::llmName() const {
    return llm_name.empty() ? "Assistant" : llm_name;
}

void AppConfig::setLlmName(const std::string& name) {
    llm_name = name;
}

std::string AppConfig::userName() const {
    return user_name.empty() ? "Creator" : user_name;
}

void AppConfig::setUserName(const std::string& name) {
    user_name = name;
}

namespace {
    json toJson(const AppConfig& config) {
        return json{
            { "model_path", config.model_path },
            { "last_opened_iso8601", config.last_opened_iso8601 },
            { "current_room", config.current_room },
            { "window", {
                { "width", config.window.width },
                { "height", config.window.height },
                { "glsl_version", config.window.glsl_version },
                { "vsync", config.window.vsync },
                { "fullscreen", config.window.fullscreen },
                { "anti_aliasing_samples", config.window.anti_aliasing_samples },
                { "debug_mode", config.window.debug_mode }
            }},
            { "lighting", {
                { "enabled", config.lighting.enabled },
                { "ambient_intensity", config.lighting.ambient_intensity },
                { "ambient_warmth", config.lighting.ambient_warmth },
                { "light_x", config.lighting.light_x },
                { "light_y", config.lighting.light_y },
                { "light_intensity", config.lighting.light_intensity },
                { "light_radius", config.lighting.light_radius },
                { "light_softness", config.lighting.light_softness },
                { "light_r", config.lighting.light_r },
                { "light_g", config.lighting.light_g },
                { "light_b", config.lighting.light_b },
                { "vignette_strength", config.lighting.vignette_strength }
            }},
            { "llm", {
                { "llm_name", config.llm_name },
                { "user_name", config.user_name },
                { "static_system_prompt", config.static_system_prompt },
                { "dynamic_system_prompt", config.dynamic_system_prompt },
                { "show_system_prompt", config.llm_options.show_system_prompt },
                { "n_ctx", config.llm_options.n_ctx },
                { "n_predict", config.llm_options.n_predict },
                { "n_threads", config.llm_options.n_threads },
                { "use_gpu", config.llm_options.use_gpu },
                { "n_gpu_layers", config.llm_options.n_gpu_layers },
                { "history_limit", config.llm_options.history_limit },
                { "greedy", config.llm_options.greedy },
                { "top_k", config.llm_options.top_k },
                { "top_p", config.llm_options.top_p },
                { "temperature", config.llm_options.temperature },
                { "talk_on_boot_chance", config.autonomous_speech.talk_on_boot_chance },
                { "talk_on_touch_chance", config.autonomous_speech.talk_on_touch_chance },
                { "talk_per_minute_chance", config.autonomous_speech.talk_per_minute_chance }
            }},
            { "memory", {
                { "enabled", config.memory.enabled },
                { "embedding_model_path", config.memory.embedding_model_path },
                { "database_path", config.memory.database_path },
                { "faiss_index_path", config.memory.faiss_index_path },
                { "n_threads", config.memory.n_threads },
                { "n_batch", config.memory.n_batch },
                { "n_ubatch", config.memory.n_ubatch },
                { "n_ctx", config.memory.n_ctx },
                { "normalize", config.memory.normalize },
                { "store_raw_text", config.memory.store_raw_text },
                { "retrieval_top_k", config.memory.retrieval_top_k },
                { "rebuild_faiss_if_missing", config.memory.rebuild_faiss_if_missing },
                { "retrieval_score_threshold", config.memory.retrieval_score_threshold },
                { "session_recent_exclusion_count", config.memory.session_recent_exclusion_count }
            }},
            { "tts", {
                { "enabled", config.tts.enabled },
                { "model_path", config.tts.model_path },
                { "model_config_path", config.tts.model_config_path },
                { "speaker_id", config.tts.speaker_id },
                { "length_scale", config.tts.length_scale },
                { "noise_scale", config.tts.noise_scale },
                { "noise_w_scale", config.tts.noise_w_scale },
                { "warp", {
                    { "enabled", config.tts.warp.enabled },
                    { "lower_sentence_end_enabled", config.tts.warp.lower_sentence_end_enabled },
                    { "tail_ms", config.tts.warp.tail_ms },
                    { "tail_drop", config.tts.warp.tail_drop },
                    { "ring_mod_enabled", config.tts.warp.ring_mod_enabled },
                    { "ring_mod_frequency_hz", config.tts.warp.ring_mod_frequency_hz },
                    { "ring_mod_mix", config.tts.warp.ring_mod_mix },
                    { "bitcrush_enabled", config.tts.warp.bitcrush_enabled },
                    { "bit_depth", config.tts.warp.bit_depth },
                    { "hard_clip_enabled", config.tts.warp.hard_clip_enabled },
                    { "clip_level", config.tts.warp.clip_level },
                    { "output_gain", config.tts.warp.output_gain }
                }}
            }},
        };
    }
}

bool loadAppConfig(const std::string& path, AppConfig& out_config, std::string& out_error) {
    std::ifstream file(path);
    if (!file.is_open()) {
        out_error = "Could not open config file: " + path;
        return false;
    }

    json j;
    try {
        file >> j;
    } catch (const std::exception& e) {
        out_error = "Failed to parse JSON config: " + std::string(e.what());
        return false;
    }

    try {
        readIfPresent(j, "model_path", out_config.model_path);
        readIfPresent(j, "last_opened_iso8601", out_config.last_opened_iso8601);
        readIfPresent(j, "current_room", out_config.current_room);

        if (auto it = j.find("window"); it != j.end() && it->is_object()) {
            const json& w = *it;
            readIfPresent(w, "width", out_config.window.width);
            readIfPresent(w, "height", out_config.window.height);
            readIfPresent(w, "glsl_version", out_config.window.glsl_version);
            readIfPresent(w, "vsync", out_config.window.vsync);
            readIfPresent(w, "fullscreen", out_config.window.fullscreen);
            readIfPresent(w, "anti_aliasing_samples", out_config.window.anti_aliasing_samples);
            readIfPresent(w, "debug_mode", out_config.window.debug_mode);
        }

        if (auto it = j.find("lighting"); it != j.end() && it->is_object()) {
            const json& l = *it;
            readIfPresent(l, "enabled", out_config.lighting.enabled);
            readIfPresent(l, "ambient_intensity", out_config.lighting.ambient_intensity);
            readIfPresent(l, "ambient_warmth", out_config.lighting.ambient_warmth);
            readIfPresent(l, "light_x", out_config.lighting.light_x);
            readIfPresent(l, "light_y", out_config.lighting.light_y);
            readIfPresent(l, "light_intensity", out_config.lighting.light_intensity);
            readIfPresent(l, "light_radius", out_config.lighting.light_radius);
            readIfPresent(l, "light_softness", out_config.lighting.light_softness);
            readIfPresent(l, "light_r", out_config.lighting.light_r);
            readIfPresent(l, "light_g", out_config.lighting.light_g);
            readIfPresent(l, "light_b", out_config.lighting.light_b);
            readIfPresent(l, "vignette_strength", out_config.lighting.vignette_strength);
        }

        if (auto it = j.find("llm"); it != j.end() && it->is_object()) {
            const json& l = *it;

            readIfPresent(l, "llm_name", out_config.llm_name);
            readIfPresent(l, "user_name", out_config.user_name);

            if (auto sp = l.find("static_system_prompt"); sp != l.end() && sp->is_array()) {
                out_config.static_system_prompt = sp->get<std::vector<std::string>>();
            }
            if (auto sp = l.find("dynamic_system_prompt"); sp != l.end() && sp->is_array()) {
                out_config.dynamic_system_prompt = sp->get<std::vector<std::string>>();
            }

            readIfPresent(l, "show_system_prompt", out_config.llm_options.show_system_prompt);
            readIfPresent(l, "n_ctx", out_config.llm_options.n_ctx);
            readIfPresent(l, "n_predict", out_config.llm_options.n_predict);
            readIfPresent(l, "n_threads", out_config.llm_options.n_threads);
            readIfPresent(l, "use_gpu", out_config.llm_options.use_gpu);
            readIfPresent(l, "n_gpu_layers", out_config.llm_options.n_gpu_layers);
            readIfPresent(l, "history_limit", out_config.llm_options.history_limit);
            readIfPresent(l, "greedy", out_config.llm_options.greedy);
            readIfPresent(l, "top_k", out_config.llm_options.top_k);
            readIfPresent(l, "top_p", out_config.llm_options.top_p);
            readIfPresent(l, "temperature", out_config.llm_options.temperature);
            readIfPresent(l, "talk_on_boot_chance", out_config.autonomous_speech.talk_on_boot_chance);
            readIfPresent(l, "talk_on_touch_chance", out_config.autonomous_speech.talk_on_touch_chance);
            readIfPresent(l, "talk_per_minute_chance", out_config.autonomous_speech.talk_per_minute_chance);
        }

        if (auto it = j.find("memory"); it != j.end() && it->is_object()) {
            const json& m = *it;
            readIfPresent(m, "enabled", out_config.memory.enabled);
            readIfPresent(m, "embedding_model_path", out_config.memory.embedding_model_path);
            readIfPresent(m, "database_path", out_config.memory.database_path);
            readIfPresent(m, "faiss_index_path", out_config.memory.faiss_index_path);
            readIfPresent(m, "n_threads", out_config.memory.n_threads);
            readIfPresent(m, "n_batch", out_config.memory.n_batch);
            readIfPresent(m, "n_ubatch", out_config.memory.n_ubatch);
            readIfPresent(m, "n_ctx", out_config.memory.n_ctx);
            readIfPresent(m, "normalize", out_config.memory.normalize);
            readIfPresent(m, "store_raw_text", out_config.memory.store_raw_text);
            readIfPresent(m, "retrieval_top_k", out_config.memory.retrieval_top_k);
            readIfPresent(m, "rebuild_faiss_if_missing", out_config.memory.rebuild_faiss_if_missing);
            readIfPresent(m, "retrieval_score_threshold", out_config.memory.retrieval_score_threshold);
            readIfPresent(m, "session_recent_exclusion_count", out_config.memory.session_recent_exclusion_count);
        }

        if (auto it = j.find("tts"); it != j.end() && it->is_object()) {
            const json& t = *it;
            readIfPresent(t, "enabled", out_config.tts.enabled);
            readIfPresent(t, "model_path", out_config.tts.model_path);
            readIfPresent(t, "model_config_path", out_config.tts.model_config_path);
            readIfPresent(t, "speaker_id", out_config.tts.speaker_id);
            readIfPresent(t, "length_scale", out_config.tts.length_scale);
            readIfPresent(t, "noise_scale", out_config.tts.noise_scale);
            readIfPresent(t, "noise_w_scale", out_config.tts.noise_w_scale);

            if (auto wit = t.find("warp"); wit != t.end() && wit->is_object()) {
                const json& w = *wit;
                readIfPresent(w, "enabled", out_config.tts.warp.enabled);
                readIfPresent(w, "lower_sentence_end_enabled", out_config.tts.warp.lower_sentence_end_enabled);
                readIfPresent(w, "tail_ms", out_config.tts.warp.tail_ms);
                readIfPresent(w, "tail_drop", out_config.tts.warp.tail_drop);
                readIfPresent(w, "ring_mod_enabled", out_config.tts.warp.ring_mod_enabled);
                readIfPresent(w, "ring_mod_frequency_hz", out_config.tts.warp.ring_mod_frequency_hz);
                readIfPresent(w, "ring_mod_mix", out_config.tts.warp.ring_mod_mix);
                readIfPresent(w, "bitcrush_enabled", out_config.tts.warp.bitcrush_enabled);
                readIfPresent(w, "bit_depth", out_config.tts.warp.bit_depth);
                readIfPresent(w, "hard_clip_enabled", out_config.tts.warp.hard_clip_enabled);
                readIfPresent(w, "clip_level", out_config.tts.warp.clip_level);
                readIfPresent(w, "output_gain", out_config.tts.warp.output_gain);
            }
        }
    } catch (const std::exception& e) {
        out_error = "Invalid config value: " + std::string(e.what());
        return false;
    }

    if (out_config.model_path.empty()) {
        out_error = "model_path must not be empty.";
        return false;
    }

    if (out_config.current_room.empty()) {
        out_error = "current_room must not be empty.";
        return false;
    }

    out_config.window.anti_aliasing_samples =
        (out_config.window.anti_aliasing_samples == 0 ||
        out_config.window.anti_aliasing_samples == 2 ||
        out_config.window.anti_aliasing_samples == 4 ||
        out_config.window.anti_aliasing_samples == 8 ||
        out_config.window.anti_aliasing_samples == 16)
            ? out_config.window.anti_aliasing_samples
            : 4;

    out_config.lighting.ambient_intensity = std::clamp(out_config.lighting.ambient_intensity, 0.0f, 2.0f);
    out_config.lighting.ambient_warmth = std::clamp(out_config.lighting.ambient_warmth, 0.0f, 1.0f);
    out_config.lighting.light_x = std::clamp(out_config.lighting.light_x, 0.0f, 1.0f);
    out_config.lighting.light_y = std::clamp(out_config.lighting.light_y, 0.0f, 1.0f);
    out_config.lighting.light_intensity = std::clamp(out_config.lighting.light_intensity, 0.0f, 3.0f);
    out_config.lighting.light_radius = std::clamp(out_config.lighting.light_radius, 0.05f, 2.0f);
    out_config.lighting.light_softness = std::clamp(out_config.lighting.light_softness, 0.1f, 4.0f);
    out_config.lighting.light_r = std::clamp(out_config.lighting.light_r, 0.0f, 2.0f);
    out_config.lighting.light_g = std::clamp(out_config.lighting.light_g, 0.0f, 2.0f);
    out_config.lighting.light_b = std::clamp(out_config.lighting.light_b, 0.0f, 2.0f);
    out_config.lighting.vignette_strength = std::clamp(out_config.lighting.vignette_strength, 0.0f, 1.0f);

    if (out_config.window.width <= 0 || out_config.window.height <= 0) {
        out_error = "Window width and height must be positive.";
        return false;
    }

    if (out_config.llm_name.empty()) {
        out_error = "llm.llm_name must not be empty.";
        return false;
    }

    if (out_config.user_name.empty()) {
        out_error = "llm.user_name must not be empty.";
        return false;
    }

    if (out_config.llm_options.n_ctx <= 0 ||
        out_config.llm_options.n_predict <= 0 ||
        out_config.llm_options.n_threads <= 0) {
        out_error = "n_ctx, n_predict, and n_threads must be positive.";
        return false;
    }

    out_config.autonomous_speech.talk_on_boot_chance =
        std::clamp(out_config.autonomous_speech.talk_on_boot_chance, 0.0f, 1.0f);
    out_config.autonomous_speech.talk_on_touch_chance =
        std::clamp(out_config.autonomous_speech.talk_on_touch_chance, 0.0f, 1.0f);
    out_config.autonomous_speech.talk_per_minute_chance =
        std::clamp(out_config.autonomous_speech.talk_per_minute_chance, 0.0f, 1.0f);

    if (out_config.memory.enabled) {
        if (out_config.memory.embedding_model_path.empty()) {
            out_error = "memory.embedding_model_path must not be empty.";
            return false;
        }

        if (out_config.memory.database_path.empty()) {
            out_error = "memory.database_path must not be empty.";
            return false;
        }

        if (out_config.memory.faiss_index_path.empty()) {
            out_error = "memory.faiss_index_path must not be empty.";
            return false;
        }

        if (out_config.memory.n_threads <= 0 ||
            out_config.memory.n_batch <= 0 ||
            out_config.memory.n_ubatch <= 0 ||
            out_config.memory.n_ctx <= 0) {
            out_error = "memory.n_threads, n_batch, n_ubatch, and n_ctx must be positive.";
            return false;
        }

        if (out_config.memory.retrieval_top_k <= 0) {
            out_error = "memory.retrieval_top_k must be positive.";
            return false;
        }

        if (out_config.memory.retrieval_score_threshold < -1.0f ||
            out_config.memory.retrieval_score_threshold > 1.0f) {
            out_error = "memory.retrieval_score_threshold must be between -1.0 and 1.0.";
            return false;
        }

        if (out_config.memory.session_recent_exclusion_count <= 0) {
            out_error = "memory.session_recent_exclusion_count must be positive.";
            return false;
        }
    }

    return true;
}

bool saveAppConfig(const std::string& path, const AppConfig& config, std::string& out_error) {
    std::ofstream file(path);
    if (!file.is_open()) {
        out_error = "Could not open config file for writing: " + path;
        return false;
    }

    try {
        file << toJson(config).dump(2) << "\n";
    } catch (const std::exception& e) {
        out_error = "Failed to write JSON config: " + std::string(e.what());
        return false;
    }

    return true;
}