// EmbeddingModel.hpp
#pragma once

#include <memory>
#include <string>
#include <vector>

struct llama_model;
struct llama_context;
struct llama_vocab;

class EmbeddingModel {
public:
    struct Options {
        int n_threads = 4;
        int n_batch = 512;
        int n_ubatch = 512;
        int n_ctx = 512;
        bool normalize = true;
    };

    EmbeddingModel();
    ~EmbeddingModel();

    EmbeddingModel(const EmbeddingModel&) = delete;
    EmbeddingModel& operator=(const EmbeddingModel&) = delete;

    bool initialize(
        const std::string& model_path,
        const Options& options,
        std::string& out_error
    );

    bool isInitialized() const;
    int embeddingDim() const;

    bool embed(
        const std::string& text,
        std::vector<float>& out_embedding,
        std::string& out_error
    ) const;

private:
    struct BackendGuard;

    struct ModelDeleter {
        void operator()(llama_model* model) const;
    };

    struct ContextDeleter {
        void operator()(llama_context* ctx) const;
    };

    bool tokenize(const std::string& text, std::vector<int>& out_tokens, std::string& out_error) const;
    static void normalizeVector(std::vector<float>& v);

    std::unique_ptr<BackendGuard> backend_;
    std::unique_ptr<llama_model, ModelDeleter> model_;
    const llama_vocab* vocab_ = nullptr;
    Options options_{};
    int embedding_dim_ = 0;
};