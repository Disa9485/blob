// EmbeddingModel.cpp
#include "EmbeddingModel.hpp"
#include "llama.h"

#include <cmath>
#include <cstring>
#include <vector>

namespace {
    void silent_log(ggml_log_level, const char*, void*) {
    }
}

struct EmbeddingModel::BackendGuard {
    BackendGuard() {
        llama_log_set(silent_log, nullptr);
        ggml_backend_load_all();
        llama_backend_init();
    }

    ~BackendGuard() {
        llama_backend_free();
    }
};

void EmbeddingModel::ModelDeleter::operator()(llama_model* model) const {
    if (model) {
        llama_model_free(model);
    }
}

void EmbeddingModel::ContextDeleter::operator()(llama_context* ctx) const {
    if (ctx) {
        llama_free(ctx);
    }
}

EmbeddingModel::EmbeddingModel() = default;
EmbeddingModel::~EmbeddingModel() = default;

bool EmbeddingModel::initialize(
    const std::string& model_path,
    const Options& options,
    std::string& out_error)
{
    backend_ = std::make_unique<BackendGuard>();

    llama_model_params model_params = llama_model_default_params();

    model_.reset(llama_model_load_from_file(model_path.c_str(), model_params));
    if (!model_) {
        out_error = "Failed to load embedding model: " + model_path;
        backend_.reset();
        return false;
    }

    vocab_ = llama_model_get_vocab(model_.get());
    if (!vocab_) {
        out_error = "Embedding model vocab is null.";
        model_.reset();
        backend_.reset();
        return false;
    }

    options_ = options;
    embedding_dim_ = llama_model_n_embd(model_.get());

    if (embedding_dim_ <= 0) {
        out_error = "Embedding dimension is invalid.";
        model_.reset();
        backend_.reset();
        return false;
    }

    return true;
}

bool EmbeddingModel::isInitialized() const {
    return model_ != nullptr && vocab_ != nullptr;
}

int EmbeddingModel::embeddingDim() const {
    return embedding_dim_;
}

bool EmbeddingModel::tokenize(
    const std::string& text,
    std::vector<int>& out_tokens,
    std::string& out_error) const
{
    if (!vocab_) {
        out_error = "Embedding model is not initialized.";
        return false;
    }

    const int needed = -llama_tokenize(
        vocab_,
        text.c_str(),
        static_cast<int32_t>(text.size()),
        nullptr,
        0,
        true,
        true
    );

    if (needed <= 0) {
        out_error = "Failed to tokenize embedding input.";
        return false;
    }

    out_tokens.resize(needed);

    const int rc = llama_tokenize(
        vocab_,
        text.c_str(),
        static_cast<int32_t>(text.size()),
        reinterpret_cast<llama_token*>(out_tokens.data()),
        static_cast<int32_t>(out_tokens.size()),
        true,
        true
    );

    if (rc < 0) {
        out_error = "Tokenization returned an error.";
        return false;
    }

    return true;
}

void EmbeddingModel::normalizeVector(std::vector<float>& v) {
    double sum = 0.0;
    for (float x : v) {
        sum += static_cast<double>(x) * static_cast<double>(x);
    }

    const double norm = std::sqrt(sum);
    if (norm <= 0.0) {
        return;
    }

    for (float& x : v) {
        x = static_cast<float>(x / norm);
    }
}

bool EmbeddingModel::embed(
    const std::string& text,
    std::vector<float>& out_embedding,
    std::string& out_error) const
{
    if (!isInitialized()) {
        out_error = "Embedding model is not initialized.";
        return false;
    }

    std::vector<int> raw_tokens;
    if (!tokenize(text, raw_tokens, out_error)) {
        return false;
    }

    std::vector<llama_token> tokens(raw_tokens.begin(), raw_tokens.end());

    llama_context_params ctx_params = llama_context_default_params();
    ctx_params.n_ctx = options_.n_ctx;
    ctx_params.n_batch = options_.n_batch;
    ctx_params.n_ubatch = options_.n_ubatch;
    ctx_params.embeddings = true;
    ctx_params.pooling_type = LLAMA_POOLING_TYPE_MEAN;   // for sequence embeddings
    ctx_params.attention_type = LLAMA_ATTENTION_TYPE_NON_CAUSAL; // for embedding models
    ctx_params.no_perf = true;

    std::unique_ptr<llama_context, ContextDeleter> ctx(
        llama_init_from_model(model_.get(), ctx_params)
    );
    if (!ctx) {
        out_error = "Failed to create embedding context.";
        return false;
    }

    llama_set_n_threads(ctx.get(), options_.n_threads, options_.n_threads);

    llama_batch batch = llama_batch_get_one(
        tokens.data(),
        static_cast<int32_t>(tokens.size())
    );

    if (llama_decode(ctx.get(), batch) != 0) {
        out_error = "Embedding decode failed.";
        return false;
    }

    // Assumes your llama.cpp build exposes pooled sequence embeddings here.
    const float* emb = llama_get_embeddings_seq(ctx.get(), 0);
    if (!emb) {
        out_error = "No sequence embedding returned. Your llama.cpp build may require pooling configuration or a slightly different embedding API.";
        return false;
    }

    out_embedding.assign(emb, emb + embedding_dim_);

    if (options_.normalize) {
        normalizeVector(out_embedding);
    }

    return true;
}