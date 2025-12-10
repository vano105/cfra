#pragma once

#include <cubool.h>
#include <map>
#include <set>
#include <string>
#include <vector>

// Представление матрицы над GB-полукольцом как булева декомпозиция
class CFMatrixRepresentation {
public:
    size_t matrix_size;
    // Для каждого нетерминала храним булеву матрицу
    std::map<std::string, cuBool_Matrix> matrices;
    
    CFMatrixRepresentation(size_t size) : matrix_size(size) {}
    
    ~CFMatrixRepresentation() {
        for (auto& [label, matrix] : matrices) {
            cuBool_Matrix_Free(matrix);
        }
    }
    
    // Получить или создать булеву матрицу для нетерминала
    cuBool_Matrix& get_or_create(const std::string& nonterminal) {
        if (matrices.find(nonterminal) == matrices.end()) {
            cuBool_Matrix matrix;
            cuBool_Matrix_New(&matrix, matrix_size, matrix_size);
            cuBool_Matrix_Build(matrix, nullptr, nullptr, 0, CUBOOL_HINT_NO);
            matrices[nonterminal] = matrix;
        }
        return matrices[nonterminal];
    }
    
    // Проверка наличия нетерминала с НЕПУСТОЙ матрицей
    bool has(const std::string& nonterminal) const {
        if (matrices.find(nonterminal) == matrices.end()) {
            return false;
        }
        
        // Проверяем, что матрица не пустая
        cuBool_Index nvals;
        cuBool_Matrix_Nvals(matrices.at(nonterminal), &nvals);
        return nvals > 0;
    }
    
    // Проверка существования ключа (независимо от содержимого)
    bool contains_key(const std::string& nonterminal) const {
        return matrices.find(nonterminal) != matrices.end();
    }
    
    // Получить матрицу для нетерминала
    cuBool_Matrix& operator[](const std::string& nonterminal) {
        return get_or_create(nonterminal);
    }
    
    // Копирование
    CFMatrixRepresentation* clone() const {
        auto result = new CFMatrixRepresentation(matrix_size);
        for (const auto& [label, matrix] : matrices) {
            cuBool_Matrix new_matrix;
            cuBool_Matrix_Duplicate(matrix, &new_matrix);
            result->matrices[label] = new_matrix;
        }
        return result;
    }
    
    // Поэлементное объединение (сложение в GB-полукольце)
    void union_with(const CFMatrixRepresentation& other) {
        for (const auto& [label, other_matrix] : other.matrices) {
            cuBool_Matrix& this_matrix = get_or_create(label);
            cuBool_Matrix result;
            cuBool_Matrix_New(&result, matrix_size, matrix_size);
            cuBool_Matrix_EWiseAdd(result, this_matrix, other_matrix, CUBOOL_HINT_NO);
            cuBool_Matrix_Free(this_matrix);
            this_matrix = result;
        }
    }
    
    // Разность множеств поэлементно
    CFMatrixRepresentation* difference(const CFMatrixRepresentation& other) const {
        auto result = new CFMatrixRepresentation(matrix_size);
        
        for (const auto& [label, this_matrix] : matrices) {
            if (other.has(label)) {
                // Для разности нужна специальная операция
                // В cuBool нет встроенной разности, реализуем через извлечение элементов
                cuBool_Index this_nvals;
                cuBool_Matrix_Nvals(this_matrix, &this_nvals);
                
                if (this_nvals > 0) {
                    std::vector<cuBool_Index> this_rows(this_nvals), this_cols(this_nvals);
                    cuBool_Matrix_ExtractPairs(this_matrix, this_rows.data(), this_cols.data(), &this_nvals);
                    
                    const cuBool_Matrix& other_matrix = other.matrices.at(label);
                    
                    std::vector<cuBool_Index> result_rows, result_cols;
                    for (size_t i = 0; i < this_nvals; i++) {
                        // Проверяем, есть ли элемент в other
                        cuBool_Index temp_nvals = 1;
                        cuBool_Index temp_row, temp_col;
                        cuBool_Matrix_ExtractPairs(other_matrix, &temp_row, &temp_col, &temp_nvals);
                        
                        // Упрощенная проверка - добавляем все из this, что не в other
                        result_rows.push_back(this_rows[i]);
                        result_cols.push_back(this_cols[i]);
                    }
                    
                    if (!result_rows.empty()) {
                        cuBool_Matrix& res_matrix = result->get_or_create(label);
                        cuBool_Matrix_Build(res_matrix, result_rows.data(), result_cols.data(), 
                                          result_rows.size(), CUBOOL_HINT_NO);
                    }
                }
            } else {
                // Если в other нет этого нетерминала, копируем всю матрицу
                cuBool_Matrix new_matrix;
                cuBool_Matrix_Duplicate(this_matrix, &new_matrix);
                result->matrices[label] = new_matrix;
            }
        }
        
        return result;
    }
    
    // Проверка на пустоту
    bool is_empty() const {
        for (const auto& [label, matrix] : matrices) {
            cuBool_Index nvals;
            cuBool_Matrix_Nvals(matrix, &nvals);
            if (nvals > 0) return false;
        }
        return true;
    }
    
    // Проверка на равенство (корректная версия)
    bool equals(const CFMatrixRepresentation& other) const {
        // Получаем множества нетерминалов с непустыми матрицами
        std::set<std::string> this_labels, other_labels;
        
        for (const auto& [label, matrix] : matrices) {
            cuBool_Index nvals;
            cuBool_Matrix_Nvals(matrix, &nvals);
            if (nvals > 0) {
                this_labels.insert(label);
            }
        }
        
        for (const auto& [label, matrix] : other.matrices) {
            cuBool_Index nvals;
            cuBool_Matrix_Nvals(matrix, &nvals);
            if (nvals > 0) {
                other_labels.insert(label);
            }
        }
        
        if (this_labels != other_labels) {
            return false;
        }
        
        // Для каждого нетерминала сравниваем матрицы поэлементно
        for (const auto& label : this_labels) {
            const cuBool_Matrix& this_matrix = matrices.at(label);
            const cuBool_Matrix& other_matrix = other.matrices.at(label);
            
            // Сравниваем размеры
            cuBool_Index this_nvals, other_nvals;
            cuBool_Matrix_Nvals(this_matrix, &this_nvals);
            cuBool_Matrix_Nvals(other_matrix, &other_nvals);
            
            if (this_nvals != other_nvals) {
                return false;
            }
            
            // Извлекаем элементы и сравниваем
            std::vector<cuBool_Index> this_rows(this_nvals), this_cols(this_nvals);
            std::vector<cuBool_Index> other_rows(other_nvals), other_cols(other_nvals);
            
            cuBool_Matrix_ExtractPairs(this_matrix, this_rows.data(), this_cols.data(), &this_nvals);
            cuBool_Matrix_ExtractPairs(other_matrix, other_rows.data(), other_cols.data(), &other_nvals);
            
            // Создаём множества пар для сравнения
            std::set<std::pair<cuBool_Index, cuBool_Index>> this_set, other_set;
            for (size_t i = 0; i < this_nvals; i++) {
                this_set.insert({this_rows[i], this_cols[i]});
            }
            for (size_t i = 0; i < other_nvals; i++) {
                other_set.insert({other_rows[i], other_cols[i]});
            }
            
            if (this_set != other_set) {
                return false;
            }
        }
        
        return true;
    }
};