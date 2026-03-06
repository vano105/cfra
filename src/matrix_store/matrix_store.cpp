#include "matrix_store.hpp"
#include "common.hpp"

MatrixStore::MatrixStore(MatrixStore&& o) noexcept : n_(o.n_), map_(std::move(o.map_)) {
    o.map_.clear();
}

MatrixStore& MatrixStore::operator=(MatrixStore&& o) noexcept {
    if (this != &o) {
        clear();
        n_ = o.n_;
        map_ = std::move(o.map_);
        o.map_.clear();
    }
    return *this;
}

MatrixStore::~MatrixStore() {
    for (auto& [_, m] : map_)
        if (m) cuBool_Matrix_Free(m);
}

void MatrixStore::swap(MatrixStore& other) noexcept {
    std::swap(n_, other.n_);
    map_.swap(other.map_);
}

void MatrixStore::clear() {
    for (auto& [_, m] : map_)
        if (m) cuBool_Matrix_Free(m);
    map_.clear();
    cache_.clear();
}

cuBool_Matrix MatrixStore::get(const std::string& sym) const {
    auto it = map_.find(sym);
    return (it != map_.end()) ? it->second : nullptr;
}

cuBool_Matrix MatrixStore::get_ensure(const std::string& sym) {
    auto& m = map_[sym];
    if (!m) {
        CB_CHECK(cuBool_Matrix_New(&m, n_, n_));
        cache_[sym] = 0;
    }
    return m;
}

void MatrixStore::replace(const std::string& sym, cuBool_Matrix new_m) {
    auto& m = map_[sym];
    if (m) cuBool_Matrix_Free(m);
    m = new_m;
    cache_.erase(sym);
}

cuBool_Index MatrixStore::nvals_of(const std::string& sym) const {
    auto ci = cache_.find(sym);
    if (ci != cache_.end()) return ci->second;

    auto m = get(sym);
    if (!m) return 0;
    cuBool_Index nv = 0;
    cuBool_Matrix_Nvals(m, &nv);
    cache_[sym] = nv;
    return nv;
}

bool MatrixStore::is_empty(const std::string& sym) const {
    return nvals_of(sym) == 0;
}

void MatrixStore::invalidate(const std::string& sym) {
    cache_.erase(sym);
}

uint64_t MatrixStore::total_nvals() const {
    uint64_t total = 0;
    for (auto& [_, m] : map_) {
        if (!m) continue;
        cuBool_Index nv = 0;
        cuBool_Matrix_Nvals(m, &nv);
        total += nv;
    }
    return total;
}