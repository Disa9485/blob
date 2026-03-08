// ConversationMemoryService.hpp
#pragma once

#include "AppConfig.hpp"
#include "ConversationMemoryStore.hpp"
#include "EmbeddingModel.hpp"
#include "FaissMemoryIndex.hpp"

#include <mutex>
#include <string>

class ConversationMemoryService {
public:
    struct SearchResult {
        bool found = false;
        int64_t id = 0;
        int64_t session_id = 0;
        int64_t message_index = 0;
        float score = 0.0f;
        std::string role;
        std::string timestamp_iso8601;
        std::string text;
        std::string embedded_text;
    };

    ConversationMemoryService();

    bool initialize(const MemoryConfig& config, std::string& out_error);

    bool ingestMessage(
        int64_t session_id,
        int64_t message_index,
        const std::string& role,
        const std::string& timestamp_iso8601,
        const std::string& text,
        std::string& out_error
    );

    bool searchTopMessages(
        int64_t current_session_id,
        int64_t current_message_index,
        const std::string& role,
        const std::string& timestamp_iso8601,
        const std::string& text,
        std::vector<SearchResult>& out_results,
        std::string& out_error
    );

    bool isEnabled() const;

private:
    static std::string buildEmbeddedText(
        const std::string& role,
        const std::string& timestamp_iso8601,
        const std::string& text
    );

    bool enabled_ = false;
    bool store_raw_text_ = true;
    int retrieval_top_k_ = 3;
    float retrieval_score_threshold_ = 0.45f;
    int recent_session_exclusion_count_ = 20;
    std::string faiss_index_path_;

    mutable std::mutex mutex_;
    EmbeddingModel embedding_model_;
    ConversationMemoryStore store_;
    FaissMemoryIndex faiss_index_;
};