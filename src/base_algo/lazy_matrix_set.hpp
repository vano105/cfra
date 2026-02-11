#pragma once

#include <cubool.h>
#include <vector>
#include <map>
#include <string>
#include <algorithm>
#include <cmath>
#include <iostream>
#include "matrix_representation.hpp"

/**
 * LazyMatrixSet - множество матриц с отложенным сложением (Оптимизация 3.5)
 * 
 * Реализует Алгоритм 5 из отчёта Муравьева (стр. 31):
 * Поддерживает инвариант разреженности для уменьшения операций сложения
 * 
 * Инвариант (10): ∀i ∈ [1, p-1]: b^(p-i) * nnz(M_i) < nnz(M_{i+1})
 * где b = n^C1, C1 ∈ (0, 1] - параметр алгоритма
 */
class LazyMatrixSet {
private:
    size_t matrix_size;
    double b_factor;
    std::vector<cuBool_Matrix> matrices;
    std::vector<cuBool_Index> nvals_cache;
    
    /**
     * Поддержание инварианта (10) согласно Алгоритму 5
     * 
     * Алгоритм 5 (Муравьев, стр. 31):
     * while ∃A, B ∈ M̃' нарушающие инвариант (10):
     *     M̃' ← (M̃' \ {A, B}) ∪ {A +∪ B}
     */
    void maintain_invariant() {
        if (matrices.size() <= 1) return;
        
        while (true) {
            bool found_violation = false;
            
            // Ищем ЛЮБУЮ пару (i, j), нарушающую инвариант
            for (size_t i = 0; i < matrices.size() && !found_violation; ++i) {
                for (size_t j = i + 1; j < matrices.size() && !found_violation; ++j) {
                    // Инвариант требует: для отсортированных по nvals
                    // b * nvals[меньшей] >= nvals[большей] означает нарушение
                    
                    cuBool_Index nvals_i = nvals_cache[i];
                    cuBool_Index nvals_j = nvals_cache[j];
                    
                    cuBool_Index smaller_nvals = std::min(nvals_i, nvals_j);
                    cuBool_Index larger_nvals = std::max(nvals_i, nvals_j);
                    
                    // Проверка нарушения: если b * smaller >= larger, то нарушение
                    if (smaller_nvals > 0 && 
                        b_factor * static_cast<double>(smaller_nvals) >= static_cast<double>(larger_nvals)) {
                        
                        // Нашли нарушение! Объединяем матрицы i и j
                        cuBool_Matrix merged;
                        cuBool_Matrix_New(&merged, matrix_size, matrix_size);
                        cuBool_Matrix_EWiseAdd(merged, matrices[i], matrices[j], CUBOOL_HINT_NO);
                        
                        cuBool_Index merged_nvals;
                        cuBool_Matrix_Nvals(merged, &merged_nvals);
                        
                        // Освобождаем старые матрицы
                        cuBool_Matrix_Free(matrices[i]);
                        cuBool_Matrix_Free(matrices[j]);
                        
                        // Удаляем из векторов (сначала больший индекс!)
                        matrices.erase(matrices.begin() + j);
                        matrices.erase(matrices.begin() + i);
                        nvals_cache.erase(nvals_cache.begin() + j);
                        nvals_cache.erase(nvals_cache.begin() + i);
                        
                        // Добавляем объединённую
                        matrices.push_back(merged);
                        nvals_cache.push_back(merged_nvals);
                        
                        found_violation = true;
                    }
                }
            }
            
            // Если нарушений не найдено, инвариант выполнен
            if (!found_violation) {
                break;
            }
        }
        
        // После восстановления инварианта, сортируем для эффективности
        sort_by_nvals();
    }
    
    void sort_by_nvals() {
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
            // Автоматический выбор: b = n^0.5 (C1 = 0.5)
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
    
    /**
     * Добавить матрицу к множеству с поддержанием инварианта
     * Соответствует операции +∪ с отложенным вычислением
     */
    void add(cuBool_Matrix new_matrix) {
        cuBool_Index new_nvals;
        cuBool_Matrix_Nvals(new_matrix, &new_nvals);
        
        if (new_nvals == 0) {
            // Не добавляем пустые матрицы
            return;
        }
        
        // Дублируем матрицу (LazyMatrixSet владеет памятью)
        cuBool_Matrix dup;
        cuBool_Matrix_Duplicate(new_matrix, &dup);
        
        matrices.push_back(dup);
        nvals_cache.push_back(new_nvals);
        
        // Поддерживаем инвариант после добавления
        maintain_invariant();
    }
    
    /**
     * Материализовать (конкретизировать) множество в одну матрицу
     * Выполняет фактическое сложение всех матриц
     */
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
    
    cuBool_Index total_nvals() const {
        cuBool_Index total = 0;
        for (auto nvals : nvals_cache) {
            total += nvals;
        }
        return total;
    }
    
    size_t size() const {
        return matrices.size();
    }
    
    bool empty() const {
        return matrices.empty();
    }
    
    void clear() {
        for (auto matrix : matrices) {
            cuBool_Matrix_Free(matrix);
        }
        matrices.clear();
        nvals_cache.clear();
    }
    
    void print_stats() const {
        std::cout << "  LazyMatrixSet: " << matrices.size() << " matrices, "
                  << total_nvals() << " total nvals [";
        for (size_t i = 0; i < std::min(size_t(5), nvals_cache.size()); ++i) {
            std::cout << nvals_cache[i];
            if (i < std::min(size_t(5), nvals_cache.size()) - 1) std::cout << ", ";
        }
        if (nvals_cache.size() > 5) std::cout << "...";
        std::cout << "]" << std::endl;
    }
};

/**
 * CFMatrixRepresentation с поддержкой отложенного сложения (Lazy Addition)
 * 
 * Вместо хранения одной матрицы для каждого нетерминала,
 * храним LazyMatrixSet (множество матриц с отложенным сложением)
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
    
    /**
     * Добавить матрицу к нетерминалу (символьное сложение)
     * Матрица будет добавлена в LazyMatrixSet с поддержанием инварианта
     */
    void add(const std::string& label, cuBool_Matrix matrix) {
        if (lazy_matrices.find(label) == lazy_matrices.end()) {
            lazy_matrices[label] = new LazyMatrixSet(matrix_size, b_factor);
        }
        lazy_matrices[label]->add(matrix);
    }
    
    /**
     * Материализовать нетерминал в одну матрицу (конкретное сложение)
     */
    cuBool_Matrix materialize(const std::string& label) {
        if (lazy_matrices.find(label) == lazy_matrices.end()) {
            cuBool_Matrix empty;
            cuBool_Matrix_New(&empty, matrix_size, matrix_size);
            cuBool_Matrix_Build(empty, nullptr, nullptr, 0, CUBOOL_HINT_NO);
            return empty;
        }
        return lazy_matrices[label]->materialize();
    }
    
    bool has(const std::string& label) const {
        return lazy_matrices.find(label) != lazy_matrices.end() && 
               !lazy_matrices.at(label)->empty();
    }
    
    std::vector<std::string> labels() const {
        std::vector<std::string> result;
        for (const auto& [label, _] : lazy_matrices) {
            result.push_back(label);
        }
        return result;
    }
    
    void print_stats() const {
        std::cout << "Lazy matrix representation statistics:" << std::endl;
        size_t total_matrices = 0;
        cuBool_Index total_nvals = 0;
        
        for (const auto& [label, lazy_set] : lazy_matrices) {
            if (!lazy_set->empty()) {
                std::cout << "  " << label << ": ";
                lazy_set->print_stats();
                total_matrices += lazy_set->size();
                total_nvals += lazy_set->total_nvals();
            }
        }
        
        std::cout << "Total: " << lazy_matrices.size() << " labels, " 
                  << total_matrices << " matrices, " 
                  << total_nvals << " nvals" << std::endl;
    }
    
    /**
     * Конвертировать в обычное представление (материализовать все нетерминалы)
     */
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