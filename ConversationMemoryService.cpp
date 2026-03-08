// ConversationMemoryService.cpp
#include "ConversationMemoryService.hpp"

#include <sstream>
#include <vector>

ConversationMemoryService::ConversationMemoryService() = default;

bool ConversationMemoryService::initialize(const MemoryConfig& config, std::string& out_error) {
    enabled_ = config.enabled;
    store_raw_text_ = config.store_raw_text;
    retrieval_top_k_ = config.retrieval_top_k;
    faiss_index_path_ = config.faiss_index_path;
    retrieval_score_threshold_ = config.retrieval_score_threshold;
    recent_session_exclusion_count_ = config.session_recent_exclusion_count;

    if (!enabled_) {
        return true;
    }

    EmbeddingModel::Options embed_options;
    embed_options.n_threads = config.n_threads;
    embed_options.n_batch = config.n_batch;
    embed_options.n_ubatch = config.n_ubatch;
    embed_options.n_ctx = config.n_ctx;
    embed_options.normalize = config.normalize;

    if (!embedding_model_.initialize(config.embedding_model_path, embed_options, out_error)) {
        enabled_ = false;
        return false;
    }

    if (!store_.open(config.database_path, out_error)) {
        enabled_ = false;
        return false;
    }

    if (!faiss_index_.initialize(embedding_model_.embeddingDim(), config.faiss_index_path, out_error)) {
        enabled_ = false;
        return false;
    }

    if (!faiss_index_.loadOrCreate(out_error)) {
        enabled_ = false;
        return false;
    }

    return true;
}

bool ConversationMemoryService::isEnabled() const {
    return enabled_;
}

std::string ConversationMemoryService::buildEmbeddedText(
    const std::string& role,
    const std::string& timestamp_iso8601,
    const std::string& text)
{
    std::ostringstream out;
        out
        // These add too much to the semantic search weight
        // << "role: " << role << "\n"
        // << "timestamp: " << timestamp_iso8601 << "\n"
        << "message: " << text;
    return out.str();
}

bool ConversationMemoryService::ingestMessage(
    int64_t session_id,
    int64_t message_index,
    const std::string& role,
    const std::string& timestamp_iso8601,
    const std::string& text,
    std::string& out_error)
{
    if (!enabled_) {
        return true;
    }

    const std::string embedded_text = buildEmbeddedText(role, timestamp_iso8601, text);

    std::vector<float> embedding;
    int64_t row_id = 0;

    std::lock_guard<std::mutex> lock(mutex_);

    if (!embedding_model_.embed(embedded_text, embedding, out_error)) {
        return false;
    }

    const std::string raw_text = store_raw_text_ ? text : "";
    if (!store_.insertMessage(
            session_id,
            message_index,
            role,
            timestamp_iso8601,
            raw_text,
            embedded_text,
            embedding,
            row_id,
            out_error)) {
        return false;
    }

    if (!faiss_index_.addVector(row_id, embedding, out_error)) {
        return false;
    }

    if (!faiss_index_.save(out_error)) {
        return false;
    }

    return true;
}

bool ConversationMemoryService::searchTopMessages(
    int64_t current_session_id,
    int64_t current_message_index,
    const std::string& role,
    const std::string& timestamp_iso8601,
    const std::string& text,
    std::vector<SearchResult>& out_results,
    std::string& out_error)
{
    out_results.clear();

    if (!enabled_) {
        return true;
    }

    const std::string embedded_text = buildEmbeddedText(role, timestamp_iso8601, text);
    std::vector<float> query_embedding;

    std::lock_guard<std::mutex> lock(mutex_);

    if (!embedding_model_.embed(embedded_text, query_embedding, out_error)) {
        return false;
    }

    const int desired_results = retrieval_top_k_;
    const int candidate_k = (std::max)(desired_results * 4, desired_results + recent_session_exclusion_count_);

    std::vector<int64_t> ids;
    std::vector<float> scores;
    if (!faiss_index_.searchTopK(query_embedding, candidate_k, ids, scores, out_error)) {
        return false;
    }

    const int64_t recent_cutoff =
        (std::max<int64_t>)(0, current_message_index - recent_session_exclusion_count_);

    for (std::size_t i = 0; i < ids.size() && i < scores.size(); ++i) {
        if (scores[i] < retrieval_score_threshold_) {
            continue;
        }

        ConversationMemoryStore::StoredMessage msg;
        if (!store_.getMessageById(ids[i], msg, out_error)) {
            return false;
        }

        if (msg.session_id == current_session_id &&
            msg.message_index >= recent_cutoff) {
            continue;
        }

        SearchResult result;
        result.found = true;
        result.id = msg.id;
        result.session_id = msg.session_id;
        result.message_index = msg.message_index;
        result.score = scores[i];
        result.role = msg.role;
        result.timestamp_iso8601 = msg.timestamp_iso8601;
        result.text = msg.raw_text.empty() ? msg.embedded_text : msg.raw_text;
        result.embedded_text = msg.embedded_text;

        out_results.push_back(std::move(result));

        if (static_cast<int>(out_results.size()) >= desired_results) {
            break;
        }
    }

    return true;
}