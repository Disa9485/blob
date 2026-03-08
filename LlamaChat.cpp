// LlamaChat.cpp
#include "LlamaChat.hpp"
#include "llama.h"

#include <algorithm>
#include <sstream>
#include <string>
#include <vector>

namespace {
    void silent_log(ggml_log_level, const char*, void*) {
    }
}

struct LlamaChat::BackendGuard {
    BackendGuard() {
        llama_log_set(silent_log, nullptr);
        ggml_backend_load_all();
        llama_backend_init();
    }

    ~BackendGuard() {
        llama_backend_free();
    }
};

void LlamaChat::ContextDeleter::operator()(llama_context* ctx) const {
    if (ctx) {
        llama_free(ctx);
    }
}

void LlamaChat::ModelDeleter::operator()(llama_model* model) const {
    if (model) {
        llama_model_free(model);
    }
}

void LlamaChat::SamplerDeleter::operator()(llama_sampler* sampler) const {
    if (sampler) {
        llama_sampler_free(sampler);
    }
}

LlamaChat::LlamaChat() = default;
LlamaChat::~LlamaChat() = default;

bool LlamaChat::initialize(
    const std::string& model_path,
    const std::string& static_system_prompt,
    const Options& options)
{
    backend_ = std::make_unique<BackendGuard>();

    llama_model_params model_params = llama_model_default_params();
    model_params.n_gpu_layers = options.n_gpu_layers;

    model_.reset(llama_model_load_from_file(model_path.c_str(), model_params));
    if (!model_) {
        backend_.reset();
        return false;
    }

    vocab_ = llama_model_get_vocab(model_.get());
    if (!vocab_) {
        model_.reset();
        backend_.reset();
        return false;
    }

    static_system_prompt_ = static_system_prompt;
    options_ = options;
    history_.clear();
    static_prefix_tokens_.clear();
    n_past_ = 0;
    base_context_loaded_ = false;

    if (!createContextAndSampler()) {
        model_.reset();
        backend_.reset();
        return false;
    }

    std::vector<int32_t> base_tokens;
    if (!tokenizePrompt(buildStaticPrefixPrompt(), base_tokens)) {
        ctx_.reset();
        sampler_.reset();
        model_.reset();
        backend_.reset();
        return false;
    }

    static_prefix_tokens_ = std::move(base_tokens);

    if (!evalTokens(static_prefix_tokens_)) {
        ctx_.reset();
        sampler_.reset();
        model_.reset();
        backend_.reset();
        return false;
    }

    base_context_loaded_ = true;
    return true;
}

bool LlamaChat::isInitialized() const {
    return model_ != nullptr &&
           vocab_ != nullptr &&
           ctx_ != nullptr &&
           sampler_ != nullptr &&
           base_context_loaded_;
}

const std::string& LlamaChat::getSystemPrompt() const {
    return static_system_prompt_;
}

void LlamaChat::setSystemPrompt(const std::string& system_prompt) {
    static_system_prompt_ = system_prompt;
}

void LlamaChat::clearHistory() {
    history_.clear();

    if (!ctx_ || !sampler_) {
        return;
    }

    if (!createContextAndSampler()) {
        return;
    }

    n_past_ = 0;
    base_context_loaded_ = false;

    if (!static_prefix_tokens_.empty()) {
        if (evalTokens(static_prefix_tokens_)) {
            base_context_loaded_ = true;
        }
    }
}

void LlamaChat::addAssistantMessage(const std::string& text) {
    history_.push_back({ "assistant", text });
    trimHistory();
}

void LlamaChat::addUserMessage(const std::string& text) {
    history_.push_back({ "user", text });
    trimHistory();
}

const std::vector<LlamaChat::Message>& LlamaChat::history() const {
    return history_;
}

std::string LlamaChat::buildStaticPrefixPrompt() const {
    std::ostringstream oss;
    oss << "<|im_start|>system\n"
        << static_system_prompt_
        << "\n<|im_end|>\n";
    return oss.str();
}

std::string LlamaChat::buildTurnPrompt(
    const std::string& user_message,
    const std::string& dynamic_prompt_override) const
{
    std::ostringstream oss;

    if (!dynamic_prompt_override.empty()) {
        oss << "<|im_start|>system\n"
            << dynamic_prompt_override
            << "\n<|im_end|>\n";
    }

    oss << "<|im_start|>user\n"
        << user_message
        << "\n<|im_end|>\n"
        << "<|im_start|>assistant\n";

    return oss.str();
}

std::string LlamaChat::buildReplayPrompt(const Message& msg) const {
    std::ostringstream oss;
    oss << "<|im_start|>" << msg.role << "\n"
        << msg.text
        << "\n<|im_end|>\n";
    return oss.str();
}

bool LlamaChat::tokenizePrompt(const std::string& text, std::vector<int32_t>& out_tokens) const {
    if (!vocab_) {
        return false;
    }

    const int needed = -llama_tokenize(
        vocab_,
        text.c_str(),
        static_cast<int32_t>(text.size()),
        nullptr,
        0,
        false,
        true
    );

    if (needed <= 0) {
        return false;
    }

    out_tokens.resize(needed);

    const int rc = llama_tokenize(
        vocab_,
        text.c_str(),
        static_cast<int32_t>(text.size()),
        reinterpret_cast<llama_token*>(out_tokens.data()),
        static_cast<int32_t>(out_tokens.size()),
        false,
        true
    );

    return rc >= 0;
}

bool LlamaChat::createContextAndSampler() {
    ctx_.reset();
    sampler_.reset();
    n_past_ = 0;

    llama_context_params ctx_params = llama_context_default_params();
    ctx_params.n_ctx = options_.n_ctx;
    ctx_params.n_batch = options_.n_ctx;
    ctx_params.n_ubatch = options_.n_ctx;
    ctx_params.no_perf = true;

    ctx_.reset(llama_init_from_model(model_.get(), ctx_params));
    if (!ctx_) {
        return false;
    }

    llama_set_n_threads(ctx_.get(), options_.n_threads, options_.n_threads);

    auto chain_params = llama_sampler_chain_default_params();
    chain_params.no_perf = true;

    sampler_.reset(llama_sampler_chain_init(chain_params));
    if (!sampler_) {
        ctx_.reset();
        return false;
    }

    if (options_.greedy) {
        llama_sampler_chain_add(sampler_.get(), llama_sampler_init_greedy());
    } else {
        llama_sampler_chain_add(sampler_.get(), llama_sampler_init_top_k(options_.top_k));
        llama_sampler_chain_add(sampler_.get(), llama_sampler_init_top_p(options_.top_p, 1));
        llama_sampler_chain_add(sampler_.get(), llama_sampler_init_temp(options_.temperature));
    }

    return true;
}

bool LlamaChat::evalTokens(const std::vector<int32_t>& tokens) {
    if (!ctx_) {
        return false;
    }

    if (tokens.empty()) {
        return true;
    }

    std::vector<llama_token> llama_tokens(tokens.begin(), tokens.end());

    llama_batch batch = llama_batch_get_one(
        llama_tokens.data(),
        static_cast<int32_t>(llama_tokens.size())
    );

    if (llama_decode(ctx_.get(), batch) != 0) {
        return false;
    }

    n_past_ += static_cast<int32_t>(llama_tokens.size());
    return true;
}

bool LlamaChat::ensureCapacity(int32_t incoming_token_count) {
    const int32_t safety_margin = 64;
    const int32_t required = n_past_ + incoming_token_count + options_.n_predict + safety_margin;

    if (required <= options_.n_ctx) {
        return true;
    }

    return rebuildContext();
}

bool LlamaChat::rebuildContext() {
    // Keep only the newest messages that can fit with the static prefix.
    std::vector<Message> kept_history;
    std::vector<std::vector<int32_t>> kept_token_chunks;

    int32_t budget = options_.n_ctx - options_.n_predict - 64;
    if (budget <= 0) {
        return false;
    }

    int32_t used = static_cast<int32_t>(static_prefix_tokens_.size());
    if (used >= budget) {
        return false;
    }

    // Walk backward so we keep the newest messages.
    for (int i = static_cast<int>(history_.size()) - 1; i >= 0; --i) {
        std::vector<int32_t> tokens;
        if (!tokenizePrompt(buildReplayPrompt(history_[i]), tokens)) {
            return false;
        }

        if (used + static_cast<int32_t>(tokens.size()) > budget) {
            continue;
        }

        used += static_cast<int32_t>(tokens.size());
        kept_history.push_back(history_[i]);
        kept_token_chunks.push_back(std::move(tokens));
    }

    std::reverse(kept_history.begin(), kept_history.end());
    std::reverse(kept_token_chunks.begin(), kept_token_chunks.end());

    if (!createContextAndSampler()) {
        return false;
    }

    n_past_ = 0;
    base_context_loaded_ = false;

    if (!evalTokens(static_prefix_tokens_)) {
        return false;
    }

    for (const std::vector<int32_t>& chunk : kept_token_chunks) {
        if (!evalTokens(chunk)) {
            return false;
        }
    }

    history_ = std::move(kept_history);
    trimHistory();
    base_context_loaded_ = true;
    return true;
}

void LlamaChat::trimHistory() {
    while (static_cast<int>(history_.size()) > options_.history_limit) {
        history_.erase(history_.begin());
    }
}

std::string LlamaChat::generate(const std::string& user_message) {
    std::string result;
    generateStream(user_message, [&](const std::string& piece) {
        result += piece;
        return true;
    });
    return result;
}

bool LlamaChat::generateStream(const std::string& user_message, const TokenCallback& on_token) {
    return generateStreamWithSystemPrompt(user_message, "", on_token);
}

std::string LlamaChat::generateWithSystemPrompt(
    const std::string& user_message,
    const std::string& system_prompt_override)
{
    std::string result;
    generateStreamWithSystemPrompt(
        user_message,
        system_prompt_override,
        [&](const std::string& piece) {
            result += piece;
            return true;
        }
    );
    return result;
}

bool LlamaChat::generateStreamWithSystemPrompt(
    const std::string& user_message,
    const std::string& system_prompt_override,
    const TokenCallback& on_token)
{
    if (!isInitialized()) {
        return false;
    }

    const std::string prompt = buildTurnPrompt(user_message, system_prompt_override);

    std::vector<int32_t> prompt_tokens_raw;
    if (!tokenizePrompt(prompt, prompt_tokens_raw)) {
        return false;
    }

    if (!ensureCapacity(static_cast<int32_t>(prompt_tokens_raw.size()))) {
        return false;
    }

    if (!evalTokens(prompt_tokens_raw)) {
        return false;
    }

    std::string reply;
    reply.reserve(1024);

    int generated = 0;
    while (generated < options_.n_predict) {
        if (n_past_ + 1 >= options_.n_ctx) {
            break;
        }

        const llama_token token = llama_sampler_sample(sampler_.get(), ctx_.get(), -1);

        if (llama_token_is_eog(vocab_, token)) {
            break;
        }

        char piece[256];
        const int n_piece = llama_token_to_piece(
            vocab_,
            token,
            piece,
            sizeof(piece),
            0,
            true
        );

        if (n_piece < 0) {
            break;
        }

        std::string token_piece(piece, piece + n_piece);
        reply += token_piece;

        if (on_token && !on_token(token_piece)) {
            break;
        }

        std::vector<int32_t> next_token = {
            static_cast<int32_t>(token)
        };

        if (!evalTokens(next_token)) {
            break;
        }

        ++generated;
    }

    history_.push_back({ "user", user_message });
    history_.push_back({ "assistant", reply });
    trimHistory();

    return true;
}