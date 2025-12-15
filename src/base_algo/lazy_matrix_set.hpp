#pragma once

#include "matrix_representation.hpp"
#include <cubool.h>
#include <vector>
#include <cmath>
#include <algorithm>

/**
 * Множество матриц с отложенным сложением (Оптимизация 3.5)
 * 
 * Идея: вместо хранения одной матрицы M храним множество {M₁, M₂, ..., Mₚ}
 * такое что ∑M̃ = M₁ ∪ M₂ ∪ ... ∪ Mₚ
 * 
 * Инвариант: b·nnz(M₁) < nnz(M₂) < ... < nnz(Mₚ)
 * где b = n^C₁, C₁ ∈ (0, 1]
 * 
 * Это позволяет отложить конкретное сложение до момента, когда оно станет выгодным.
 * 
 * Улучшение сложности (при использовании с incremental): O(n⁴) → O(n³)
 * 
 * Алгоритм 5 из отчёта Муравьева
 */
class LazyMatrixSet {
private:
    size_t matrix_size;
    double b_factor;  // Коэффициент b для инварианта
    
    // Множество матриц, удовлетворяющих инварианту
    std::vector<cuBool_Matrix> matrices;
    std::vector<cuBool_Index> nvals_cache;  // Кеш для nnz(matrices[i])
    
    // Поддержание инварианта после добавления новой матрицы
    void maintain_invariant() {
        if (matrices.size() < 2) return;
        
        bool changed = true;
        while (changed) {
            changed = false;
            
            // Ищем пару матриц, нарушающих инвариант
            for (size_t i = 0; i < matrices.size() - 1; ++i) {
                for (size_t j = i + 1; j < matrices.size(); ++j) {
                    // Проверяем инвариант: b·nnz(Mᵢ) < nnz(Mⱼ)
                    if (!(b_factor * nvals_cache[i] < nvals_cache[j])) {
                        // Инвариант нарушен, объединяем Mᵢ и Mⱼ
                        cuBool_Matrix combined;
                        cuBool_Matrix_New(&combined, matrix_size, matrix_size);
                        cuBool_Matrix_EWiseAdd(combined, matrices[i], matrices[j], 
                                             CUBOOL_HINT_NO);
                        
                        cuBool_Index combined_nvals;
                        cuBool_Matrix_Nvals(combined, &combined_nvals);
                        
                        // Освобождаем старые матрицы
                        cuBool_Matrix_Free(matrices[i]);
                        cuBool_Matrix_Free(matrices[j]);
                        
                        // Удаляем из векторов
                        if (i < j) {
                            matrices.erase(matrices.begin() + j);
                            nvals_cache.erase(nvals_cache.begin() + j);
                            matrices.erase(matrices.begin() + i);
                            nvals_cache.erase(nvals_cache.begin() + i);
                        } else {
                            matrices.erase(matrices.begin() + i);
                            nvals_cache.erase(nvals_cache.begin() + i);
                            matrices.erase(matrices.begin() + j);
                            nvals_cache.erase(nvals_cache.begin() + j);
                        }
                        
                        // Добавляем объединённую матрицу
                        matrices.push_back(combined);
                        nvals_cache.push_back(combined_nvals);
                        
                        changed = true;
                        break;
                    }
                }
                if (changed) break;
            }
        }
        
        // Сортируем по nnz для поддержания порядка
        std::vector<std::pair<cuBool_Index, cuBool_Matrix>> sorted;
        for (size_t i = 0; i < matrices.size(); ++i) {
            sorted.push_back({nvals_cache[i], matrices[i]});
        }
        std::sort(sorted.begin(), sorted.end());
        
        matrices.clear();
        nvals_cache.clear();
        for (auto& [nvals, matrix] : sorted) {
            matrices.push_back(matrix);
            nvals_cache.push_back(nvals);
        }
    }

public:
    LazyMatrixSet(size_t size, double b = 0.0) 
        : matrix_size(size) {
        if (b == 0.0) {
            // Автоматический выбор: b = n^0.5
            b_factor = std::sqrt(static_cast<double>(size));
        } else {
            b_factor = b;
        }
    }
    
    ~LazyMatrixSet() {
        for (auto matrix : matrices) {
            cuBool_Matrix_Free(matrix);
        }
    }
    
    // Добавить матрицу к множеству
    void add(cuBool_Matrix new_matrix) {
        cuBool_Index new_nvals;
        cuBool_Matrix_Nvals(new_matrix, &new_nvals);
        
        if (new_nvals == 0) {
            cuBool_Matrix_Free(new_matrix);
            return;
        }
        
        // Дублируем матрицу (чтобы владеть ей)
        cuBool_Matrix dup;
        cuBool_Matrix_Duplicate(new_matrix, &dup);
        
        matrices.push_back(dup);
        nvals_cache.push_back(new_nvals);
        
        // Поддерживаем инвариант
        maintain_invariant();
    }
    
    // Материализовать в одну матрицу
    cuBool_Matrix materialize() {
        if (matrices.empty()) {
            cuBool_Matrix empty;
            cuBool_Matrix_New(&empty, matrix_size, matrix_size);
            cuBool_Matrix_Build(empty, nullptr, nullptr, 0, CUBOOL_HINT_NO);
            return empty;
        }
        
        if (matrices.size() == 1) {
            cuBool_Matrix result;
            cuBool_Matrix_Duplicate(matrices[0], &result);
            return result;
        }
        
        // Объединяем все матрицы
        cuBool_Matrix result;
        cuBool_Matrix_Duplicate(matrices[0], &result);
        
        for (size_t i = 1; i < matrices.size(); ++i) {
            cuBool_Matrix temp;
            cuBool_Matrix_New(&temp, matrix_size, matrix_size);
            cuBool_Matrix_EWiseAdd(temp, result, matrices[i], CUBOOL_HINT_NO);
            cuBool_Matrix_Free(result);
            result = temp;
        }
        
        return result;
    }
    
    // Получить суммарное количество элементов
    cuBool_Index total_nvals() const {
        cuBool_Index total = 0;
        for (auto nvals : nvals_cache) {
            total += nvals;
        }
        return total;
    }
    
    // Количество матриц в множестве
    size_t size() const {
        return matrices.size();
    }
    
    // Пустое ли множество
    bool empty() const {
        return matrices.empty();
    }
    
    // Очистить множество
    void clear() {
        for (auto matrix : matrices) {
            cuBool_Matrix_Free(matrix);
        }
        matrices.clear();
        nvals_cache.clear();
    }
};

/**
 * CFMatrixRepresentation с поддержкой отложенного сложения
 * 
 * Вместо хранения одной матрицы для каждого нетерминала,
 * храним множество матриц (LazyMatrixSet)
 */
class LazyCFMatrixRepresentation {
private:
    size_t matrix_size;
    double b_factor;
    std::map<std::string, LazyMatrixSet*> lazy_matrices;
    
public:
    LazyCFMatrixRepresentation(size_t size, double b = 0.0) 
        : matrix_size(size), b_factor(b) {}
    
    ~LazyCFMatrixRepresentation() {
        for (auto& [label, lazy_set] : lazy_matrices) {
            delete lazy_set;
        }
    }
    
    // Добавить матрицу к нетерминалу
    void add(const std::string& label, cuBool_Matrix matrix) {
        if (lazy_matrices.find(label) == lazy_matrices.end()) {
            lazy_matrices[label] = new LazyMatrixSet(matrix_size, b_factor);
        }
        lazy_matrices[label]->add(matrix);
    }
    
    // Материализовать нетерминал в одну матрицу
    cuBool_Matrix materialize(const std::string& label) {
        if (lazy_matrices.find(label) == lazy_matrices.end()) {
            cuBool_Matrix empty;
            cuBool_Matrix_New(&empty, matrix_size, matrix_size);
            cuBool_Matrix_Build(empty, nullptr, nullptr, 0, CUBOOL_HINT_NO);
            return empty;
        }
        return lazy_matrices[label]->materialize();
    }
    
    // Проверить наличие нетерминала
    bool has(const std::string& label) const {
        return lazy_matrices.find(label) != lazy_matrices.end() && 
               !lazy_matrices.at(label)->empty();
    }
    
    // Получить все метки
    std::vector<std::string> labels() const {
        std::vector<std::string> result;
        for (const auto& [label, _] : lazy_matrices) {
            result.push_back(label);
        }
        return result;
    }
    
    // Статистика
    void print_stats() const {
        std::cout << "Lazy matrix statistics:" << std::endl;
        size_t total_matrices = 0;
        cuBool_Index total_nvals = 0;
        
        for (const auto& [label, lazy_set] : lazy_matrices) {
            if (!lazy_set->empty()) {
                std::cout << "  " << label << ": " 
                          << lazy_set->size() << " matrices, "
                          << lazy_set->total_nvals() << " total nvals" << std::endl;
                total_matrices += lazy_set->size();
                total_nvals += lazy_set->total_nvals();
            }
        }
        
        std::cout << "Total: " << total_matrices << " matrices, " 
                  << total_nvals << " nvals" << std::endl;
    }
    
    // Конвертировать в обычное представление
    CFMatrixRepresentation* to_normal() {
        CFMatrixRepresentation* result = new CFMatrixRepresentation(matrix_size);
        
        for (const auto& [label, lazy_set] : lazy_matrices) {
            if (!lazy_set->empty()) {
                cuBool_Matrix& m = result->get_or_create(label);
                cuBool_Matrix materialized = lazy_set->materialize();
                cuBool_Matrix_Free(m);
                m = materialized;
            }
        }
        
        return result;
    }
};