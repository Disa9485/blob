// LlamaChat.hpp
#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

struct llama_model;
struct llama_context;
struct llama_vocab;
struct llama_sampler;

class LlamaChat {
public:
    struct Options {
        bool show_system_prompt = false;
        int n_ctx = 4096;
        int n_predict = 256;
        int n_threads = 8;
        int n_gpu_layers = 0;
        int history_limit = 12;
        bool greedy = true;
        int top_k = 40;
        float top_p = 0.95f;
        float temperature = 0.8f;
    };

    struct Message {
        std::string role;
        std::string text;
    };

    using TokenCallback = std::function<bool(const std::string& piece)>;

    LlamaChat();
    ~LlamaChat();

    LlamaChat(const LlamaChat&) = delete;
    LlamaChat& operator=(const LlamaChat&) = delete;

    bool initialize(
        const std::string& model_path,
        const std::string& static_system_prompt,
        const Options& options = {}
    );

    bool isInitialized() const;

    const std::string& getSystemPrompt() const;
    void setSystemPrompt(const std::string& system_prompt);

    void clearHistory();
    void addAssistantMessage(const std::string& text);
    void addUserMessage(const std::string& text);

    std::string generate(const std::string& user_message);
    bool generateStream(const std::string& user_message, const TokenCallback& on_token);

    std::string generateWithSystemPrompt(
        const std::string& user_message,
        const std::string& system_prompt_override
    );

    bool generateStreamWithSystemPrompt(
        const std::string& user_message,
        const std::string& system_prompt_override,
        const TokenCallback& on_token
    );

    const std::vector<Message>& history() const;

private:
    struct BackendGuard;

    struct ContextDeleter {
        void operator()(llama_context* ctx) const;
    };

    struct ModelDeleter {
        void operator()(llama_model* model) const;
    };

    struct SamplerDeleter {
        void operator()(llama_sampler* sampler) const;
    };

    std::string buildStaticPrefixPrompt() const;
    std::string buildTurnPrompt(
        const std::string& user_message,
        const std::string& dynamic_prompt_override
    ) const;
    std::string buildReplayPrompt(const Message& msg) const;

    bool createContextAndSampler();
    bool evalTokens(const std::vector<int32_t>& tokens);
    bool ensureCapacity(int32_t incoming_token_count);
    bool rebuildContext();

    bool tokenizePrompt(const std::string& text, std::vector<int32_t>& out_tokens) const;
    void trimHistory();

    std::unique_ptr<BackendGuard> backend_;
    std::unique_ptr<llama_model, ModelDeleter> model_;
    const llama_vocab* vocab_ = nullptr;

    std::unique_ptr<llama_context, ContextDeleter> ctx_;
    std::unique_ptr<llama_sampler, SamplerDeleter> sampler_;

    std::vector<int32_t> static_prefix_tokens_;
    int32_t n_past_ = 0;
    bool base_context_loaded_ = false;

    std::string static_system_prompt_;
    Options options_;
    std::vector<Message> history_;
};