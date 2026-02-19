#include "matrix_store.hpp"
#include "../common.hpp"

MatrixStore::~MatrixStore() {
    for (auto& [_, m] : map_)
        if (m) cuBool_Matrix_Free(m);
}

// Получить матрицу по символу (nullptr если не существует)
cuBool_Matrix MatrixStore::get(const std::string& sym) const {
    auto it = map_.find(sym);
    return (it != map_.end()) ? it->second : nullptr;
}

// Получить или создать пустую матрицу n×n для символа
cuBool_Matrix MatrixStore::ensure(const std::string& sym) {
    auto& m = map_[sym];
    if (!m) CB_CHECK(cuBool_Matrix_New(&m, n_, n_));
    return m;
}

// Заменить матрицу для символа (старая освобождается)
void MatrixStore::replace(const std::string& sym, cuBool_Matrix new_m) {
    auto& m = map_[sym];
    if (m) cuBool_Matrix_Free(m);
    m = new_m;
}

// Количество ненулевых элементов матрицы символа
cuBool_Index MatrixStore::nvals_of(const std::string& sym) const {
    auto m = get(sym);
    if (!m) return 0;
    cuBool_Index nv = 0;
    cuBool_Matrix_Nvals(m, &nv);
    return nv;
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