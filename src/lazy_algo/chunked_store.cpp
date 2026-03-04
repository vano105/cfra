#include "../common.hpp"
#include "chunked_store.hpp"

#include <algorithm>

const std::vector<Chunk> ChunkedStore::empty_chunks_;

ChunkedStore::~ChunkedStore() {
    for (auto& [_, chunks] : map_)
        for (auto& c : chunks)
            if (c.matrix) cuBool_Matrix_Free(c.matrix);
 }

cuBool_Matrix ChunkedStore::ensure_single(const std::string& sym) {
    auto& chunks = map_[sym];
    if (chunks.empty()) {
        cuBool_Matrix m;
        CB_CHECK(cuBool_Matrix_New(&m, n_, n_));
        chunks.push_back({m, 0});
    }
    return chunks[0].matrix;
}

void ChunkedStore::invalidate(const std::string& sym) {
    auto it = map_.find(sym);
    if (it == map_.end()) return;
    for (auto& c : it->second) {
        CB_CHECK(cuBool_Matrix_Nvals(c.matrix, &c.nvals));
    }
}

void ChunkedStore::ensure_empty(const std::string& sym) {
    map_[sym];}

const std::vector<Chunk>& ChunkedStore::chunks(const std::string& sym) const {
    auto it = map_.find(sym);
    if (it != map_.end()) return it->second;
    return empty_chunks_;
}

void ChunkedStore::lazy_add(const std::string& sym, cuBool_Matrix delta, cuBool_Index delta_nv) {
    if (delta_nv == 0) {
        cuBool_Matrix_Free(delta);
        return;
    }

    auto& ch = map_[sym];
    ch.push_back({delta, delta_nv});

    std::sort(ch.begin(), ch.end(),
              [](const Chunk& a, const Chunk& b) { return a.nvals < b.nvals; });

    bool changed = true;
    while (changed) {
        changed = false;
        for (size_t i = 0; i + 1 < ch.size(); i++) {
            if (b_ * ch[i].nvals >= ch[i + 1].nvals) {
                cuBool_Matrix merged;
                CB_CHECK(cuBool_Matrix_New(&merged, n_, n_));
                CB_CHECK(cuBool_Matrix_EWiseAdd(merged,
                         ch[i].matrix, ch[i + 1].matrix, CUBOOL_HINT_NO));

                cuBool_Index merged_nv = 0;
                CB_CHECK(cuBool_Matrix_Nvals(merged, &merged_nv));

                cuBool_Matrix_Free(ch[i].matrix);
                cuBool_Matrix_Free(ch[i + 1].matrix);

                ch.erase(ch.begin() + i, ch.begin() + i + 2);
                Chunk mc = {merged, merged_nv};
                auto pos = std::lower_bound(ch.begin(), ch.end(), mc,
                    [](const Chunk& a, const Chunk& b) { return a.nvals < b.nvals; });
                ch.insert(pos, mc);

                changed = true;
                break;
            }
        }
    }
}

cuBool_Index ChunkedStore::nvals_of(const std::string& sym) const {
    auto it = map_.find(sym);
    if (it == map_.end()) return 0;
    cuBool_Index total = 0;
    for (auto& c : it->second) total += c.nvals;
    return total;
}

bool ChunkedStore::is_empty(const std::string& sym) const {
    return nvals_of(sym) == 0;
}

uint64_t ChunkedStore::total_nvals() const {
    uint64_t total = 0;
    for (auto& [sym, chunks] : map_)
        for (auto& c : chunks) total += c.nvals;
    return total;
}
