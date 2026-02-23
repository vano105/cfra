#pragma once

#include <cubool/cubool.h>
#include <unordered_map>
#include <iostream>
#include <chrono>
#include <stdexcept>

class MatrixStore {
public:
    explicit MatrixStore(cuBool_Index n) : n_(n) {}
    ~MatrixStore();

    MatrixStore(const MatrixStore&) = delete;
    MatrixStore& operator=(const MatrixStore&) = delete;

    MatrixStore(MatrixStore&& o) noexcept;
    MatrixStore& operator=(MatrixStore&& o) noexcept;

    void swap(MatrixStore& other) noexcept;

    void clear();

    cuBool_Matrix get(const std::string& sym) const;

    cuBool_Matrix ensure(const std::string& sym);

    void replace(const std::string& sym, cuBool_Matrix new_m);

    cuBool_Index nvals_of(const std::string& sym) const;

    uint64_t total_nvals() const;

    cuBool_Index n() const { return n_; }

private:
    cuBool_Index n_;
    std::unordered_map<std::string, cuBool_Matrix> map_;
};
