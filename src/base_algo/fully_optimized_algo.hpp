#pragma once

#include "../cnf_grammar/cnf_grammar.hpp"
#include "../label_decomposed_graph/label_decomposed_graph.hpp"
#include "matrix_representation.hpp"
#include "optimization_config.hpp"
#include "lazy_matrix_set.hpp"
#include <cubool.h>
#include <iostream>
#include <chrono>
#include <cmath>

/**
 * Полностью оптимизированный алгоритм КС-достижимости
 * 
 * Комбинирует все оптимизации Муравьева:
 * 1. Инкрементальные вычисления (3.1): O(n⁵) → O(n⁴)
 * 2. Проверка тривиальных операций (3.3)
 * 3. Динамический выбор формата (3.4) - в cuBool ограничено
 * 4. Отложенное сложение (3.5): O(n⁴) → O(n³)
 * 
 * Итоговая сложность: O(n³)
 */
class FullyOptimizedAlgo {
private:
    cnf_grammar grammar;
    label_decomposed_graph graph;
    size_t matrix_size;
    OptimizationConfig config;
    AlgoStats stats;
    
    using symbol = cnf_grammar::symbol;
    using complex_rule = std::tuple<symbol, symbol, symbol>;
    
    std::vector<complex_rule> cnf_rules;
    std::vector<complex_rule> extended_left_rules;
    std::vector<complex_rule> extended_right_rules;
    std::vector<complex_rule> double_terminal_rules;
    
    double b_factor;  // Параметр для lazy addition
    
    void classify_rules() {
        std::set<std::string> nonterminals;
        nonterminals.insert(grammar.start_nonterm_.label_);
        
        for (const auto& rule : grammar.complex_rules_) {
            nonterminals.insert(std::get<0>(rule));
        }
        for (const auto& rule : grammar.simple_rules_) {
            nonterminals.insert(std::get<0>(rule));
        }
        for (const auto& eps : grammar.epsilon_rules_) {
            nonterminals.insert(eps);
        }
        
        for (const auto& rule : grammar.complex_rules_) {
            symbol X = std::get<0>(rule);
            symbol Y = std::get<1>(rule);
            symbol Z = std::get<2>(rule);
            
            bool Y_is_nonterminal = (nonterminals.find(Y) != nonterminals.end());
            bool Z_is_nonterminal = (nonterminals.find(Z) != nonterminals.end());
            
            if (Y_is_nonterminal && Z_is_nonterminal) {
                cnf_rules.push_back(rule);
            } else if (Y_is_nonterminal && !Z_is_nonterminal) {
                extended_left_rules.push_back(rule);
            } else if (!Y_is_nonterminal && Z_is_nonterminal) {
                extended_right_rules.push_back(rule);
            } else {
                double_terminal_rules.push_back(rule);
            }
        }
    }
    
    bool is_empty(cuBool_Matrix matrix) {
        if (!config.use_trivial_checks) return false;
        
        cuBool_Index nvals;
        cuBool_Matrix_Nvals(matrix, &nvals);
        return nvals == 0;
    }
    
    // Умножение с lazy addition
    void multiply_and_add_lazy(cuBool_Matrix A, cuBool_Matrix B,
                               const std::string& result_label,
                               LazyCFMatrixRepresentation& lazy_result) {
        if (is_empty(A) || is_empty(B)) {
            stats.skipped_multiplications++;
            return;
        }
        
        cuBool_Matrix product;
        cuBool_Matrix_New(&product, matrix_size, matrix_size);
        cuBool_MxM(product, A, B, CUBOOL_HINT_NO);
        stats.total_multiplications++;
        
        cuBool_Index nvals;
        cuBool_Matrix_Nvals(product, &nvals);
        
        if (nvals > 0) {
            if (config.use_lazy_add) {
                lazy_result.add(result_label, product);
                stats.lazy_additions++;
            } else {
                // Без lazy add - сразу конкретное сложение
                cuBool_Matrix materialized = lazy_result.materialize(result_label);
                cuBool_Matrix temp;
                cuBool_Matrix_New(&temp, matrix_size, matrix_size);
                cuBool_Matrix_EWiseAdd(temp, materialized, product, CUBOOL_HINT_NO);
                cuBool_Matrix_Free(materialized);
                cuBool_Matrix_Free(product);
                product = temp;
                
                // Создаём новый lazy set с одной матрицей
                lazy_result.add(result_label, product);
                stats.concrete_additions++;
            }
        }
        
        cuBool_Matrix_Free(product);
    }
    
    void apply_cnf_incremental_lazy(const CFMatrixRepresentation& M,
                                     const CFMatrixRepresentation& delta,
                                     LazyCFMatrixRepresentation& lazy_result) {
        for (const auto& rule : cnf_rules) {
            symbol X = std::get<0>(rule);
            symbol Y = std::get<1>(rule);
            symbol Z = std::get<2>(rule);
            
            // КРИТИЧЕСКОЕ ИСПРАВЛЕНИЕ: добавлен случай delta[Y] · delta[Z]!
            // Иначе правила типа V2_V3 ← V2 · V3 не применятся, если оба в delta
            
            // 1. delta[Y] · delta[Z] (оба новые)
            if (delta.has(Y) && delta.has(Z)) {
                multiply_and_add_lazy(delta.matrices.at(Y), delta.matrices.at(Z), X, lazy_result);
            }
            // ВАЖНО: проверяем "else if" чтобы не дублировать, если Y==Z
            // 2. M[Y] · delta[Z] (Y существует, Z новый)
            else if (M.has(Y) && delta.has(Z)) {
                multiply_and_add_lazy(M.matrices.at(Y), delta.matrices.at(Z), X, lazy_result);
            }
            // 3. delta[Y] · M[Z] (Y новый, Z существует)
            else if (delta.has(Y) && M.has(Z)) {
                multiply_and_add_lazy(delta.matrices.at(Y), M.matrices.at(Z), X, lazy_result);
            }
        }
    }
    
    void apply_extended_left_incremental_lazy(const CFMatrixRepresentation& M,
                                              const CFMatrixRepresentation& delta,
                                              LazyCFMatrixRepresentation& lazy_result) {
        // Применяем для delta
        for (const auto& rule : extended_left_rules) {
            symbol X = std::get<0>(rule);
            symbol Y = std::get<1>(rule);
            symbol Z = std::get<2>(rule);
            
            if (!delta.has(Y)) continue;
            if (graph.matrices.find(Z) == graph.matrices.end()) continue;
            
            multiply_and_add_lazy(delta.matrices.at(Y), graph.matrices.at(Z), X, lazy_result);
        }
        
        // Применяем для M
        for (const auto& rule : extended_left_rules) {
            symbol X = std::get<0>(rule);
            symbol Y = std::get<1>(rule);
            symbol Z = std::get<2>(rule);
            
            if (!M.has(Y)) continue;
            if (graph.matrices.find(Z) == graph.matrices.end()) continue;
            
            multiply_and_add_lazy(M.matrices.at(Y), graph.matrices.at(Z), X, lazy_result);
        }
    }
    
    void apply_extended_right_incremental_lazy(const CFMatrixRepresentation& M,
                                               const CFMatrixRepresentation& delta,
                                               LazyCFMatrixRepresentation& lazy_result) {
        // Применяем для delta (новые элементы)
        for (const auto& rule : extended_right_rules) {
            symbol X = std::get<0>(rule);
            symbol Y = std::get<1>(rule); // терминал
            symbol Z = std::get<2>(rule); // нетерминал
            
            if (!delta.has(Z)) continue;
            if (graph.matrices.find(Y) == graph.matrices.end()) continue;
            
            multiply_and_add_lazy(graph.matrices.at(Y), delta.matrices.at(Z), X, lazy_result);
        }
        
        // КРИТИЧЕСКОЕ ИСПРАВЛЕНИЕ: применяем для M тоже!
        // Иначе правила типа S → d_r V_d не применятся если V_d уже в M
        for (const auto& rule : extended_right_rules) {
            symbol X = std::get<0>(rule);
            symbol Y = std::get<1>(rule); // терминал
            symbol Z = std::get<2>(rule); // нетерминал
            
            // Проверяем M, но пропускаем если уже в delta (чтобы не дублировать)
            if (delta.has(Z)) continue; // Уже обработано выше
            if (!M.has(Z)) continue;
            if (graph.matrices.find(Y) == graph.matrices.end()) continue;
            
            multiply_and_add_lazy(graph.matrices.at(Y), M.matrices.at(Z), X, lazy_result);
        }
    }
    
    void apply_double_terminal_lazy(LazyCFMatrixRepresentation& lazy_result) {
        for (const auto& rule : double_terminal_rules) {
            symbol X = std::get<0>(rule);
            symbol Y = std::get<1>(rule);
            symbol Z = std::get<2>(rule);
            
            if (graph.matrices.find(Y) == graph.matrices.end()) continue;
            if (graph.matrices.find(Z) == graph.matrices.end()) continue;
            
            multiply_and_add_lazy(graph.matrices.at(Y), graph.matrices.at(Z), 
                                 X, lazy_result);
        }
    }

public:
    FullyOptimizedAlgo(const cnf_grammar& gr, 
                      const label_decomposed_graph& g,
                      const OptimizationConfig& cfg = OptimizationConfig())
        : grammar(gr), graph(g), matrix_size(g.matrix_size), config(cfg) {
        classify_rules();
        
        // Вычисляем параметр b для lazy addition
        b_factor = std::pow(static_cast<double>(matrix_size), config.lazy_add_exponent);
    }
    
    cuBool_Matrix solve() {
        auto start_time = std::chrono::high_resolution_clock::now();
        
        std::cout << "\n=== Fully Optimized Algorithm ===" << std::endl;
        std::cout << config.to_string() << std::endl;
        std::cout << "Matrix size: " << matrix_size << std::endl;
        if (config.use_lazy_add) {
            std::cout << "Lazy add parameter b: " << b_factor 
                      << " (n^" << config.lazy_add_exponent << ")" << std::endl;
        }
        
        // Инициализация ΔM
        CFMatrixRepresentation delta(matrix_size);
        
        // Простые правила
        for (const auto& rule : grammar.simple_rules_) {
            symbol lhs = std::get<0>(rule);
            symbol rhs = std::get<1>(rule);
            
            if (graph.matrices.find(rhs) != graph.matrices.end()) {
                cuBool_Index graph_nvals;
                cuBool_Matrix_Nvals(graph.matrices.at(rhs), &graph_nvals);
                
                if (graph_nvals > 0) {
                    cuBool_Matrix& d_lhs = delta.get_or_create(lhs);
                    cuBool_Matrix temp;
                    cuBool_Matrix_New(&temp, matrix_size, matrix_size);
                    cuBool_Matrix_EWiseAdd(temp, d_lhs, graph.matrices.at(rhs), 
                                         CUBOOL_HINT_NO);
                    cuBool_Matrix_Free(d_lhs);
                    d_lhs = temp;
                }
            }
        }
        
        // Эпсилон-правила
        std::vector<cuBool_Index> diag_rows, diag_cols;
        for (size_t i = 0; i < matrix_size; i++) {
            diag_rows.push_back(i);
            diag_cols.push_back(i);
        }
        
        for (const auto& eps_rule : grammar.epsilon_rules_) {
            cuBool_Matrix& d_eps = delta.get_or_create(eps_rule);
            cuBool_Matrix identity;
            cuBool_Matrix_New(&identity, matrix_size, matrix_size);
            cuBool_Matrix_Build(identity, diag_rows.data(), diag_cols.data(), 
                              matrix_size, CUBOOL_HINT_NO);
            
            cuBool_Matrix temp;
            cuBool_Matrix_New(&temp, matrix_size, matrix_size);
            cuBool_Matrix_EWiseAdd(temp, d_eps, identity, CUBOOL_HINT_NO);
            cuBool_Matrix_Free(d_eps);
            cuBool_Matrix_Free(identity);
            d_eps = temp;
        }
        
        // Double-terminal правила
        if (config.use_lazy_add) {
            LazyCFMatrixRepresentation lazy_init(matrix_size, b_factor);
            apply_double_terminal_lazy(lazy_init);
            
            // Материализуем в delta
            for (const auto& label : lazy_init.labels()) {
                cuBool_Matrix mat = lazy_init.materialize(label);
                cuBool_Matrix& d_label = delta.get_or_create(label);
                cuBool_Matrix temp;
                cuBool_Matrix_New(&temp, matrix_size, matrix_size);
                cuBool_Matrix_EWiseAdd(temp, d_label, mat, CUBOOL_HINT_NO);
                cuBool_Matrix_Free(d_label);
                cuBool_Matrix_Free(mat);
                d_label = temp;
            }
        } else {
            // Без lazy add - просто применяем double terminal к delta напрямую
            for (const auto& rule : double_terminal_rules) {
                symbol X = std::get<0>(rule);
                symbol Y = std::get<1>(rule);
                symbol Z = std::get<2>(rule);
                
                if (graph.matrices.find(Y) == graph.matrices.end()) continue;
                if (graph.matrices.find(Z) == graph.matrices.end()) continue;
                
                if (!is_empty(graph.matrices.at(Y)) && 
                    !is_empty(graph.matrices.at(Z))) {
                    cuBool_Matrix product;
                    cuBool_Matrix_New(&product, matrix_size, matrix_size);
                    cuBool_MxM(product, graph.matrices.at(Y), 
                             graph.matrices.at(Z), CUBOOL_HINT_NO);
                    
                    cuBool_Matrix& d_x = delta.get_or_create(X);
                    cuBool_Matrix temp;
                    cuBool_Matrix_New(&temp, matrix_size, matrix_size);
                    cuBool_Matrix_EWiseAdd(temp, d_x, product, CUBOOL_HINT_NO);
                    cuBool_Matrix_Free(d_x);
                    cuBool_Matrix_Free(product);
                    d_x = temp;
                }
            }
        }
        
        // M ← ∅
        CFMatrixRepresentation M(matrix_size);
        
        // Основной цикл
        std::cout << "\n=== Main loop ===" << std::endl;
        stats.iterations = 0;
        
        while (true) {
            stats.iterations++;
            
            // Проверка: ΔM пустой?
            cuBool_Index delta_nvals = 0;
            for (const auto& [label, matrix] : delta.matrices) {
                cuBool_Index nvals;
                cuBool_Matrix_Nvals(matrix, &nvals);
                delta_nvals += nvals;
            }
            
            if (delta_nvals == 0) {
                std::cout << "Iteration " << stats.iterations 
                          << ": ΔM empty, converged" << std::endl;
                break;
            }
            
            std::cout << "Iteration " << stats.iterations << ": |ΔM| = " << delta_nvals;
            
            // ΔMₜₘₚ ← M ·Gr ΔM + ΔM ·Gr M (с lazy addition)
            LazyCFMatrixRepresentation lazy_delta_tmp(matrix_size, b_factor);
            
            apply_cnf_incremental_lazy(M, delta, lazy_delta_tmp);
            apply_extended_left_incremental_lazy(M, delta, lazy_delta_tmp);
            apply_extended_right_incremental_lazy(M, delta, lazy_delta_tmp);
            
            // M ← M ∪ ΔM
            M.union_with(delta);
            
            // Материализуем lazy_delta_tmp в delta_tmp
            CFMatrixRepresentation* delta_tmp = lazy_delta_tmp.to_normal();
            
            // ΔM ← ΔMₜₘₚ \ M
            CFMatrixRepresentation* diff = delta_tmp->difference(M);
            
            // Очищаем старый delta и копируем новый
            for (auto& [label, matrix] : delta.matrices) {
                cuBool_Matrix_Free(matrix);
            }
            delta.matrices.clear();
            
            for (const auto& [label, matrix] : diff->matrices) {
                cuBool_Matrix dup;
                cuBool_Matrix_Duplicate(matrix, &dup);
                delta.matrices[label] = dup;
            }
            
            delete diff;
            delete delta_tmp;
            
            cuBool_Index m_nvals = 0;
            for (const auto& [label, matrix] : M.matrices) {
                cuBool_Index nvals;
                cuBool_Matrix_Nvals(matrix, &nvals);
                m_nvals += nvals;
            }
            std::cout << ", |M| = " << m_nvals << std::endl;
            
            if (stats.iterations > 100) {
                std::cerr << "WARNING: Too many iterations!" << std::endl;
                break;
            }
        }
        
        auto end_time = std::chrono::high_resolution_clock::now();
        std::chrono::duration<double> elapsed = end_time - start_time;
        stats.total_time_seconds = elapsed.count();
        
        std::cout << "\n=== Converged ===" << std::endl;
        if (config.enable_stats) {
            stats.print();
        }
        
        // Возврат результата
        std::cout << "\n=== Checking result ===" << std::endl;
        std::cout << "Start nonterminal: '" << grammar.start_nonterm_.label_ << "'" << std::endl;
        std::cout << "M has " << M.matrices.size() << " nonterminals" << std::endl;
        
        // Выведем первые нетерминалы в M
        std::cout << "Nonterminals in M: ";
        int count = 0;
        for (const auto& [label, matrix] : M.matrices) {
            cuBool_Index nvals;
            cuBool_Matrix_Nvals(matrix, &nvals);
            if (nvals > 0) {
                std::cout << label << "(" << nvals << ") ";
                count++;
                if (count > 10) {
                    std::cout << "...";
                    break;
                }
            }
        }
        std::cout << std::endl;
        
        if (M.has(grammar.start_nonterm_)) {
            cuBool_Matrix result;
            cuBool_Matrix_Duplicate(M.matrices.at(grammar.start_nonterm_), &result);
            
            cuBool_Index result_nvals;
            cuBool_Matrix_Nvals(result, &result_nvals);
            std::cout << "Result: " << result_nvals << " reachable pairs" << std::endl;
            
            return result;
        } else {
            cuBool_Matrix empty;
            cuBool_Matrix_New(&empty, matrix_size, matrix_size);
            cuBool_Matrix_Build(empty, nullptr, nullptr, 0, CUBOOL_HINT_NO);
            std::cout << "Warning: Start nonterminal '" << grammar.start_nonterm_.label_ 
                      << "' not found in result!" << std::endl;
            return empty;
        }
    }
    
    const AlgoStats& get_stats() const {
        return stats;
    }
    
    // Автоматический выбор оптимизаций
    cuBool_Matrix solve_auto() {
        // Анализируем входные данные
        size_t num_rules = grammar.complex_rules_.size();
        
        // Автоматически настраиваем конфигурацию
        config = OptimizationConfig::automatic(matrix_size, num_rules);
        config.enable_stats = true;
        
        std::cout << "Auto-selected configuration for n=" << matrix_size 
                  << ", rules=" << num_rules << std::endl;
        
        // Пересчитываем b_factor с новой конфигурацией
        b_factor = std::pow(static_cast<double>(matrix_size), config.lazy_add_exponent);
        
        return solve();
    }
};