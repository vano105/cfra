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
 * 3. Динамический выбор формата (3.4) - ограничено в cuBool
 * 4. Отложенное сложение (3.5): O(n⁴) → O(n³)
 * 
 * Итоговая сложность: O(n³)
 * 
 * ИСПРАВЛЕНИЯ:
 * - Убраны else if в apply_cnf (все случаи независимы)
 * - Добавлены простые правила в основной цикл
 * - Исправлено управление памятью в multiply_and_add_lazy
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
    
    /**
     * Умножение с lazy addition
     * 
     * ИСПРАВЛЕНИЕ: правильное управление памятью
     * - product освобождается только если не добавлен в lazy_result
     * - при !use_lazy_add освобождается temp после add
     */
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
                // Lazy add: добавляем символически (add делает dup)
                lazy_result.add(result_label, product);
                stats.lazy_additions++;
                cuBool_Matrix_Free(product);  // Освобождаем оригинал
            } else {
                // Конкретное сложение: материализуем и складываем
                cuBool_Matrix materialized = lazy_result.materialize(result_label);
                cuBool_Matrix temp;
                cuBool_Matrix_New(&temp, matrix_size, matrix_size);
                cuBool_Matrix_EWiseAdd(temp, materialized, product, CUBOOL_HINT_NO);
                cuBool_Matrix_Free(materialized);
                cuBool_Matrix_Free(product);
                
                // Добавляем результат (add делает dup)
                lazy_result.add(result_label, temp);
                cuBool_Matrix_Free(temp);  // Освобождаем temp после add
                stats.concrete_additions++;
            }
        } else {
            cuBool_Matrix_Free(product);
        }
    }
    
    /**
     * Применить CNF правила для инкрементальных вычислений
     * 
     * КРИТИЧЕСКОЕ ИСПРАВЛЕНИЕ: убраны else if!
     * Все три случая должны выполняться независимо:
     * 1. delta[Y] · delta[Z] - оба новые
     * 2. M[Y] · delta[Z] - Y существует, Z новый
     * 3. delta[Y] · M[Z] - Y новый, Z существует
     * 
     * Пример почему else if неправильно:
     * Правило: S → S S
     * Если S ∈ delta, то нужны ВСЕ три произведения:
     * - delta[S] · delta[S]
     * - M[S] · delta[S]  
     * - delta[S] · M[S]
     * 
     * С else if получим только первое!
     */
    void apply_cnf_incremental_lazy(const CFMatrixRepresentation& M,
                                     const CFMatrixRepresentation& delta,
                                     LazyCFMatrixRepresentation& lazy_result) {
        for (const auto& rule : cnf_rules) {
            symbol X = std::get<0>(rule);
            symbol Y = std::get<1>(rule);
            symbol Z = std::get<2>(rule);
            
            // Случай 1: delta[Y] · delta[Z] (оба новые)
            if (delta.has(Y) && delta.has(Z)) {
                multiply_and_add_lazy(delta.matrices.at(Y), delta.matrices.at(Z), X, lazy_result);
            }
            
            // Случай 2: M[Y] · delta[Z] (Y существует, Z новый)
            // НЕ else if! Может быть Y==Z, и оба в delta и M
            if (M.has(Y) && delta.has(Z)) {
                multiply_and_add_lazy(M.matrices.at(Y), delta.matrices.at(Z), X, lazy_result);
            }
            
            // Случай 3: delta[Y] · M[Z] (Y новый, Z существует)
            if (delta.has(Y) && M.has(Z)) {
                multiply_and_add_lazy(delta.matrices.at(Y), M.matrices.at(Z), X, lazy_result);
            }
        }
    }
    
    /**
     * Extended left правила: A → Ba (B - нетерминал, a - терминал)
     */
    void apply_extended_left_incremental_lazy(const CFMatrixRepresentation& M,
                                              const CFMatrixRepresentation& delta,
                                              LazyCFMatrixRepresentation& lazy_result) {
        for (const auto& rule : extended_left_rules) {
            symbol X = std::get<0>(rule);
            symbol Y = std::get<1>(rule);  // нетерминал
            symbol Z = std::get<2>(rule);  // терминал
            
            if (graph.matrices.find(Z) == graph.matrices.end()) continue;
            
            // delta[Y] · graph[Z]
            if (delta.has(Y)) {
                multiply_and_add_lazy(delta.matrices.at(Y), graph.matrices.at(Z), X, lazy_result);
            }
            
            // M[Y] · graph[Z]
            if (M.has(Y)) {
                multiply_and_add_lazy(M.matrices.at(Y), graph.matrices.at(Z), X, lazy_result);
            }
        }
    }
    
    /**
     * Extended right правила: A → aB (a - терминал, B - нетерминал)
     */
    void apply_extended_right_incremental_lazy(const CFMatrixRepresentation& M,
                                               const CFMatrixRepresentation& delta,
                                               LazyCFMatrixRepresentation& lazy_result) {
        for (const auto& rule : extended_right_rules) {
            symbol X = std::get<0>(rule);
            symbol Y = std::get<1>(rule);  // терминал
            symbol Z = std::get<2>(rule);  // нетерминал
            
            if (graph.matrices.find(Y) == graph.matrices.end()) continue;
            
            // graph[Y] · delta[Z]
            if (delta.has(Z)) {
                multiply_and_add_lazy(graph.matrices.at(Y), delta.matrices.at(Z), X, lazy_result);
            }
            
            // graph[Y] · M[Z]
            // КРИТИЧЕСКОЕ ИСПРАВЛЕНИЕ: применяем для M тоже!
            // Без этого правила типа S → d_r V_d не применятся если V_d уже в M
            if (M.has(Z)) {
                multiply_and_add_lazy(graph.matrices.at(Y), M.matrices.at(Z), X, lazy_result);
            }
        }
    }
    
    /**
     * Простые правила A → B (A и B - нетерминалы)
     * 
     * КРИТИЧЕСКОЕ ИСПРАВЛЕНИЕ: добавлено в основной цикл!
     * В Python это есть в new_front.iadd_by_symbol(lhs, front[rhs])
     * 
     * Семантика: A = B (полное равенство)
     * В инкрементальном алгоритме: delta[A] += delta[B]
     */
    void apply_simple_rules_incremental_lazy(const CFMatrixRepresentation& delta,
                                             LazyCFMatrixRepresentation& lazy_result) {
        for (const auto& rule : grammar.simple_rules_) {
            symbol A = std::get<0>(rule);
            symbol B = std::get<1>(rule);
            
            if (delta.has(B)) {
                // Добавляем delta[B] к A
                cuBool_Matrix dup;
                cuBool_Matrix_Duplicate(delta.matrices.at(B), &dup);
                lazy_result.add(A, dup);
                cuBool_Matrix_Free(dup);
            }
        }
    }
    
    /**
     * Double-terminal правила: A → ab (оба терминалы)
     * Вычисляются один раз в начале
     */
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
        // По умолчанию: b = n^C1, где C1 = lazy_add_exponent
        b_factor = std::pow(static_cast<double>(matrix_size), config.lazy_add_exponent);
    }
    
    FullyOptimizedAlgo(const std::string& grammar_path, const std::string& graph_path,
                      const OptimizationConfig& cfg = OptimizationConfig()) {
        grammar = cnf_grammar(grammar_path);
        graph = label_decomposed_graph(graph_path);
        matrix_size = graph.matrix_size;
        config = cfg;
        classify_rules();
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
        
        // Простые правила (инициализация)
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
        
        // Double-terminal правила (вычисляются один раз)
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
            // Без lazy add
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
        
        cuBool_Index initial_nvals = 0;
        for (const auto& [label, matrix] : delta.matrices) {
            cuBool_Index nvals;
            cuBool_Matrix_Nvals(matrix, &nvals);
            initial_nvals += nvals;
        }
        std::cout << "Initial ΔM: " << initial_nvals << " edges" << std::endl;
        
        // M ← ∅
        CFMatrixRepresentation M(matrix_size);
        
        // Основной цикл (Алгоритм 4)
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
            apply_simple_rules_incremental_lazy(delta, lazy_delta_tmp);  // ИСПРАВЛЕНИЕ: добавлено!
            
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
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
            end_time - start_time
        );
        
        std::cout << "\n=== Results ===" << std::endl;
        std::cout << "Iterations: " << stats.iterations << std::endl;
        std::cout << "Total time: " << duration.count() << " ms" << std::endl;
        std::cout << "Multiplications: " << stats.total_multiplications << std::endl;
        std::cout << "Skipped (trivial): " << stats.skipped_multiplications << std::endl;
        std::cout << "Lazy additions: " << stats.lazy_additions << std::endl;
        std::cout << "Concrete additions: " << stats.concrete_additions << std::endl;
        
        // Получаем результат для стартового нетерминала
        if (M.has(grammar.start_nonterm_.label_)) {
            cuBool_Matrix result;
            cuBool_Matrix_Duplicate(M.matrices.at(grammar.start_nonterm_.label_), &result);
            
            cuBool_Index result_nvals;
            cuBool_Matrix_Nvals(result, &result_nvals);
            std::cout << "Result edges: " << result_nvals << std::endl;
            
            return result;
        } else {
            cuBool_Matrix empty;
            cuBool_Matrix_New(&empty, matrix_size, matrix_size);
            cuBool_Matrix_Build(empty, nullptr, nullptr, 0, CUBOOL_HINT_NO);
            std::cout << "Result edges: 0" << std::endl;
            return empty;
        }
    }
    
    AlgoStats get_stats() const {
        return stats;
    }
};