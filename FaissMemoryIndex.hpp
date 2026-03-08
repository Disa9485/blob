// FaissMemoryIndex.hpp
#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace faiss {
    struct Index;
}

class FaissMemoryIndex {
public:
    FaissMemoryIndex();
    ~FaissMemoryIndex();

    FaissMemoryIndex(const FaissMemoryIndex&) = delete;
    FaissMemoryIndex& operator=(const FaissMemoryIndex&) = delete;

    bool initialize(int dim, const std::string& index_path, std::string& out_error);
    bool loadOrCreate(std::string& out_error);
    bool save(std::string& out_error) const;

    bool addVector(
        int64_t id,
        const std::vector<float>& embedding,
        std::string& out_error
    );

    bool searchTopK(
        const std::vector<float>& query,
        int top_k,
        std::vector<int64_t>& out_ids,
        std::vector<float>& out_scores,
        std::string& out_error
    ) const;

    bool isInitialized() const;
    int dim() const;

private:
    int dim_ = 0;
    std::string index_path_;
    std::unique_ptr<faiss::Index> index_;
};