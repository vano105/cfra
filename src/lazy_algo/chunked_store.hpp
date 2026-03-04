#pragma once

#include <cubool/cubool.h>
#include <string>
#include <vector>
#include <unordered_map>

struct Chunk {
    cuBool_Matrix matrix;
    cuBool_Index nvals;
};

class ChunkedStore {
public:
    ChunkedStore(cuBool_Index n, double b) : n_(n), b_(b) {}

    ~ChunkedStore();

    ChunkedStore(const ChunkedStore&) = delete;
    ChunkedStore& operator=(const ChunkedStore&) = delete;

    cuBool_Matrix ensure_single(const std::string& sym);
    void invalidate(const std::string& sym);
    void ensure_empty(const std::string& sym);
    const std::vector<Chunk>& chunks(const std::string& sym) const;
    void lazy_add(const std::string& sym, cuBool_Matrix delta, cuBool_Index delta_nv);
    cuBool_Index nvals_of(const std::string& sym) const;
    bool is_empty(const std::string& sym) const;
    uint64_t total_nvals() const;
    cuBool_Index n() const { return n_; }

private:
    cuBool_Index n_;
    double b_;
    std::unordered_map<std::string, std::vector<Chunk>> map_;
    static const std::vector<Chunk> empty_chunks_;
};
