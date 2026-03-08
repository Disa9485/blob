// FaissMemoryIndex.cpp
#include "FaissMemoryIndex.hpp"

#include <faiss/IndexFlat.h>
#include <faiss/IndexIDMap.h>
#include <faiss/index_io.h>

#include <filesystem>
#include <memory>
#include <vector>

FaissMemoryIndex::FaissMemoryIndex() = default;
FaissMemoryIndex::~FaissMemoryIndex() = default;

bool FaissMemoryIndex::initialize(int dim, const std::string& index_path, std::string& out_error) {
    if (dim <= 0) {
        out_error = "FAISS dimension must be positive.";
        return false;
    }

    dim_ = dim;
    index_path_ = index_path;
    return true;
}

bool FaissMemoryIndex::loadOrCreate(std::string& out_error) {
    try {
        if (!index_path_.empty() && std::filesystem::exists(index_path_)) {
            faiss::Index* loaded = faiss::read_index(index_path_.c_str());
            if (!loaded) {
                out_error = "Failed to read FAISS index.";
                return false;
            }

            index_.reset(loaded);

            if (index_->d != dim_) {
                out_error = "FAISS index dimension does not match embedding model dimension.";
                index_.reset();
                return false;
            }

            return true;
        }

        auto flat = std::make_unique<faiss::IndexFlatIP>(dim_);
        auto mapped = std::make_unique<faiss::IndexIDMap2>(flat.release());
        index_ = std::move(mapped);
        return true;
    } catch (const std::exception& e) {
        out_error = e.what();
        return false;
    }
}

bool FaissMemoryIndex::save(std::string& out_error) const {
    if (!index_) {
        out_error = "FAISS index is not initialized.";
        return false;
    }

    try {
        faiss::write_index(index_.get(), index_path_.c_str());
        return true;
    } catch (const std::exception& e) {
        out_error = e.what();
        return false;
    }
}

bool FaissMemoryIndex::addVector(
    int64_t id,
    const std::vector<float>& embedding,
    std::string& out_error)
{
    if (!index_) {
        out_error = "FAISS index is not initialized.";
        return false;
    }

    if (static_cast<int>(embedding.size()) != dim_) {
        out_error = "Embedding dimension mismatch.";
        return false;
    }

    try {
        faiss::idx_t faiss_id = static_cast<faiss::idx_t>(id);
        index_->add_with_ids(1, embedding.data(), &faiss_id);
        return true;
    } catch (const std::exception& e) {
        out_error = e.what();
        return false;
    }
}

bool FaissMemoryIndex::searchTopK(
    const std::vector<float>& query,
    int top_k,
    std::vector<int64_t>& out_ids,
    std::vector<float>& out_scores,
    std::string& out_error) const
{
    if (!index_) {
        out_error = "FAISS index is not initialized.";
        return false;
    }

    if (static_cast<int>(query.size()) != dim_) {
        out_error = "Query dimension mismatch.";
        return false;
    }

    if (top_k <= 0) {
        out_error = "top_k must be positive.";
        return false;
    }

    std::vector<faiss::idx_t> ids(top_k, -1);
    std::vector<float> scores(top_k, 0.0f);

    try {
        index_->search(1, query.data(), top_k, scores.data(), ids.data());
    } catch (const std::exception& e) {
        out_error = e.what();
        return false;
    }

    out_ids.clear();
    out_scores.clear();

    for (int i = 0; i < top_k; ++i) {
        if (ids[i] >= 0) {
            out_ids.push_back(static_cast<int64_t>(ids[i]));
            out_scores.push_back(scores[i]);
        }
    }

    return true;
}

bool FaissMemoryIndex::isInitialized() const {
    return index_ != nullptr;
}

int FaissMemoryIndex::dim() const {
    return dim_;
}