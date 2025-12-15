#pragma once

#include "../cnf_grammar/cnf_grammar.hpp"
#include "../label_decomposed_graph/label_decomposed_graph.hpp"
#include "matrix_representation.hpp"
#include "optimization_config.hpp"
#include <cubool.h>
#include <iostream>
#include <chrono>

/**
 * Инкрементальный матричный алгоритм КС-достижимости (Оптимизация 3.1)
 * 
 * Основная идея: вместо M ·Gr M вычисляем M ·Gr ΔM и ΔM ·Gr M,
 * где ΔM (front) содержит только новые пары, добавленные на прошлой итерации.
 * 
 * Улучшение сложности: O(n⁵) → O(n⁴)
 * 
 * Алгоритм 4 из отчёта Муравьева:
 * 1. ΔM ← инициализация из графа
 * 2. M ← ∅
 * 3. while M changes:
 *    4. ΔMₜₘₚ ← M ·Gr ΔM
 *    5. M ← M ∪ ΔM
 *    6. ΔM ← ΔMₜₘₚ ∪ (ΔM ·Gr M)
 *    7. ΔM ← ΔM \ M
 */
class IncrementalMatrixAlgo {
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
    
    // Проверка, пуста ли матрица (для оптимизации тривиальных операций)
    bool is_empty(cuBool_Matrix matrix) {
        cuBool_Index nvals;
        cuBool_Matrix_Nvals(matrix, &nvals);
        return nvals == 0;
    }
    
    // M ·Gr ΔM для CNF правил
    void apply_cnf_rules_incremental(const CFMatrixRepresentation& M,
                                     const CFMatrixRepresentation& delta,
                                     CFMatrixRepresentation& result) {
        for (const auto& rule : cnf_rules) {
            symbol X = std::get<0>(rule);
            symbol Y = std::get<1>(rule);
            symbol Z = std::get<2>(rule);
            
            // КРИТИЧЕСКОЕ ИСПРАВЛЕНИЕ: добавлен случай delta[Y] · delta[Z]!
            // Иначе правила типа V2_V3 ← V2 · V3 не применятся, если оба в delta
            
            // Случай 0: delta[Y] · delta[Z] (оба новые)
            if (delta.has(Y) && delta.has(Z)) {
                if (config.use_trivial_checks && 
                    (is_empty(delta.matrices.at(Y)) || is_empty(delta.matrices.at(Z)))) {
                    stats.skipped_multiplications++;
                    continue;
                }
                
                cuBool_Matrix product;
                cuBool_Matrix_New(&product, matrix_size, matrix_size);
                cuBool_MxM(product, delta.matrices.at(Y), delta.matrices.at(Z), CUBOOL_HINT_NO);
                stats.total_multiplications++;
                
                cuBool_Index nvals;
                cuBool_Matrix_Nvals(product, &nvals);
                
                if (nvals > 0) {
                    cuBool_Matrix& result_x = result.get_or_create(X);
                    cuBool_Matrix temp;
                    cuBool_Matrix_New(&temp, matrix_size, matrix_size);
                    cuBool_Matrix_EWiseAdd(temp, result_x, product, CUBOOL_HINT_NO);
                    cuBool_Matrix_Free(result_x);
                    result_x = temp;
                }
                
                cuBool_Matrix_Free(product);
            }
            // Случай 1: M[Y] · delta[Z] (Y существует, Z новый, но не оба в delta)
            else if (M.has(Y) && delta.has(Z)) {
                if (config.use_trivial_checks && 
                    (is_empty(M.matrices.at(Y)) || is_empty(delta.matrices.at(Z)))) {
                    stats.skipped_multiplications++;
                    continue;
                }
                
                cuBool_Matrix product;
                cuBool_Matrix_New(&product, matrix_size, matrix_size);
                cuBool_MxM(product, M.matrices.at(Y), delta.matrices.at(Z), CUBOOL_HINT_NO);
                stats.total_multiplications++;
                
                cuBool_Index nvals;
                cuBool_Matrix_Nvals(product, &nvals);
                
                if (nvals > 0) {
                    cuBool_Matrix& result_x = result.get_or_create(X);
                    cuBool_Matrix temp;
                    cuBool_Matrix_New(&temp, matrix_size, matrix_size);
                    cuBool_Matrix_EWiseAdd(temp, result_x, product, CUBOOL_HINT_NO);
                    cuBool_Matrix_Free(result_x);
                    result_x = temp;
                }
                
                cuBool_Matrix_Free(product);
            }
            // Случай 2: delta[Y] · M[Z] (Y новый, Z существует, но не оба в delta)
            else if (delta.has(Y) && M.has(Z)) {
                if (config.use_trivial_checks && 
                    (is_empty(delta.matrices.at(Y)) || is_empty(M.matrices.at(Z)))) {
                    stats.skipped_multiplications++;
                    continue;
                }
                
                cuBool_Matrix product;
                cuBool_Matrix_New(&product, matrix_size, matrix_size);
                cuBool_MxM(product, delta.matrices.at(Y), M.matrices.at(Z), CUBOOL_HINT_NO);
                stats.total_multiplications++;
                
                cuBool_Index nvals;
                cuBool_Matrix_Nvals(product, &nvals);
                
                if (nvals > 0) {
                    cuBool_Matrix& result_x = result.get_or_create(X);
                    cuBool_Matrix temp;
                    cuBool_Matrix_New(&temp, matrix_size, matrix_size);
                    cuBool_Matrix_EWiseAdd(temp, result_x, product, CUBOOL_HINT_NO);
                    cuBool_Matrix_Free(result_x);
                    result_x = temp;
                }
                
                cuBool_Matrix_Free(product);
            }
        }
    }
    
    // Extended left: A → Ba
    void apply_extended_left_incremental(const CFMatrixRepresentation& M,
                                        const CFMatrixRepresentation& delta,
                                        CFMatrixRepresentation& result) {
        for (const auto& rule : extended_left_rules) {
            symbol X = std::get<0>(rule);
            symbol Y = std::get<1>(rule); // нетерминал
            symbol Z = std::get<2>(rule); // терминал
            
            if (graph.matrices.find(Z) == graph.matrices.end()) continue;
            if (config.use_trivial_checks && is_empty(graph.matrices.at(Z))) {
                stats.skipped_multiplications++;
                continue;
            }
            
            // delta[Y] · graph[Z] (новые элементы)
            if (delta.has(Y)) {
                if (config.use_trivial_checks && is_empty(delta.matrices.at(Y))) {
                    stats.skipped_multiplications++;
                    continue;
                }
                
                cuBool_Matrix product;
                cuBool_Matrix_New(&product, matrix_size, matrix_size);
                cuBool_MxM(product, delta.matrices.at(Y), graph.matrices.at(Z), CUBOOL_HINT_NO);
                stats.total_multiplications++;
                
                cuBool_Index nvals;
                cuBool_Matrix_Nvals(product, &nvals);
                
                if (nvals > 0) {
                    cuBool_Matrix& result_x = result.get_or_create(X);
                    cuBool_Matrix temp;
                    cuBool_Matrix_New(&temp, matrix_size, matrix_size);
                    cuBool_Matrix_EWiseAdd(temp, result_x, product, CUBOOL_HINT_NO);
                    cuBool_Matrix_Free(result_x);
                    result_x = temp;
                }
                
                cuBool_Matrix_Free(product);
            }
            
            // КРИТИЧЕСКОЕ ИСПРАВЛЕНИЕ: M[Y] · graph[Z] (существующие элементы)
            // Применяем только если Y НЕ в delta (чтобы не дублировать)
            if (!delta.has(Y) && M.has(Y)) {
                if (config.use_trivial_checks && is_empty(M.matrices.at(Y))) {
                    stats.skipped_multiplications++;
                    continue;
                }
                
                cuBool_Matrix product;
                cuBool_Matrix_New(&product, matrix_size, matrix_size);
                cuBool_MxM(product, M.matrices.at(Y), graph.matrices.at(Z), CUBOOL_HINT_NO);
                stats.total_multiplications++;
                
                cuBool_Index nvals;
                cuBool_Matrix_Nvals(product, &nvals);
                
                if (nvals > 0) {
                    cuBool_Matrix& result_x = result.get_or_create(X);
                    cuBool_Matrix temp;
                    cuBool_Matrix_New(&temp, matrix_size, matrix_size);
                    cuBool_Matrix_EWiseAdd(temp, result_x, product, CUBOOL_HINT_NO);
                    cuBool_Matrix_Free(result_x);
                    result_x = temp;
                }
                
                cuBool_Matrix_Free(product);
            }
        }
    }
    
    // Extended right: A → aB
    void apply_extended_right_incremental(const CFMatrixRepresentation& M,
                                          const CFMatrixRepresentation& delta,
                                          CFMatrixRepresentation& result) {
        for (const auto& rule : extended_right_rules) {
            symbol X = std::get<0>(rule);
            symbol Y = std::get<1>(rule); // терминал
            symbol Z = std::get<2>(rule); // нетерминал
            
            if (graph.matrices.find(Y) == graph.matrices.end()) continue;
            if (config.use_trivial_checks && is_empty(graph.matrices.at(Y))) {
                stats.skipped_multiplications++;
                continue;
            }
            
            // graph[Y] · delta[Z] (новые элементы)
            if (delta.has(Z)) {
                if (config.use_trivial_checks && is_empty(delta.matrices.at(Z))) {
                    stats.skipped_multiplications++;
                    continue;
                }
                
                cuBool_Matrix product;
                cuBool_Matrix_New(&product, matrix_size, matrix_size);
                cuBool_MxM(product, graph.matrices.at(Y), delta.matrices.at(Z), CUBOOL_HINT_NO);
                stats.total_multiplications++;
                
                cuBool_Index nvals;
                cuBool_Matrix_Nvals(product, &nvals);
                
                if (nvals > 0) {
                    cuBool_Matrix& result_x = result.get_or_create(X);
                    cuBool_Matrix temp;
                    cuBool_Matrix_New(&temp, matrix_size, matrix_size);
                    cuBool_Matrix_EWiseAdd(temp, result_x, product, CUBOOL_HINT_NO);
                    cuBool_Matrix_Free(result_x);
                    result_x = temp;
                }
                
                cuBool_Matrix_Free(product);
            }
            
            // КРИТИЧЕСКОЕ ИСПРАВЛЕНИЕ: graph[Y] · M[Z] (существующие элементы)
            // Применяем только если Z НЕ в delta (чтобы не дублировать)
            if (!delta.has(Z) && M.has(Z)) {
                if (config.use_trivial_checks && is_empty(M.matrices.at(Z))) {
                    stats.skipped_multiplications++;
                    continue;
                }
                
                cuBool_Matrix product;
                cuBool_Matrix_New(&product, matrix_size, matrix_size);
                cuBool_MxM(product, graph.matrices.at(Y), M.matrices.at(Z), CUBOOL_HINT_NO);
                stats.total_multiplications++;
                
                cuBool_Index nvals;
                cuBool_Matrix_Nvals(product, &nvals);
                
                if (nvals > 0) {
                    cuBool_Matrix& result_x = result.get_or_create(X);
                    cuBool_Matrix temp;
                    cuBool_Matrix_New(&temp, matrix_size, matrix_size);
                    cuBool_Matrix_EWiseAdd(temp, result_x, product, CUBOOL_HINT_NO);
                    cuBool_Matrix_Free(result_x);
                    result_x = temp;
                }
                
                cuBool_Matrix_Free(product);
            }
        }
    }
    
    // ИСПРАВЛЕНИЕ: Простые правила A → B применяются ТОЛЬКО к delta!
    // Семантика: A = B (полное равенство)
    // В инкрементальном алгоритме: delta[A] = delta[B] (новые в A = новые в B)
    void apply_simple_rules_incremental(const CFMatrixRepresentation& delta,
                                        CFMatrixRepresentation& result) {
        for (const auto& rule : grammar.simple_rules_) {
            symbol A = std::get<0>(rule);
            symbol B = std::get<1>(rule);
            
            // Копируем ТОЛЬКО delta[B] → result[A]
            // Старые элементы B уже в M[A] из предыдущих итераций!
            if (delta.has(B)) {
                cuBool_Index nvals;
                cuBool_Matrix_Nvals(delta.matrices.at(B), &nvals);
                
                if (nvals > 0) {
                    cuBool_Matrix& result_a = result.get_or_create(A);
                    cuBool_Matrix temp;
                    cuBool_Matrix_New(&temp, matrix_size, matrix_size);
                    cuBool_Matrix_EWiseAdd(temp, result_a, delta.matrices.at(B), CUBOOL_HINT_NO);
                    cuBool_Matrix_Free(result_a);
                    result_a = temp;
                }
            }
        }
    }
    
    // Double-terminal: A → ab
    void apply_double_terminal(CFMatrixRepresentation& result) {
        for (const auto& rule : double_terminal_rules) {
            symbol X = std::get<0>(rule);
            symbol Y = std::get<1>(rule);
            symbol Z = std::get<2>(rule);
            
            if (graph.matrices.find(Y) == graph.matrices.end()) continue;
            if (graph.matrices.find(Z) == graph.matrices.end()) continue;
            
            if (config.use_trivial_checks && 
                (is_empty(graph.matrices.at(Y)) || is_empty(graph.matrices.at(Z)))) {
                stats.skipped_multiplications++;
                continue;
            }
            
            cuBool_Matrix product;
            cuBool_Matrix_New(&product, matrix_size, matrix_size);
            cuBool_MxM(product, graph.matrices.at(Y), graph.matrices.at(Z), CUBOOL_HINT_NO);
            stats.total_multiplications++;
            
            cuBool_Index nvals;
            cuBool_Matrix_Nvals(product, &nvals);
            
            if (nvals > 0) {
                cuBool_Matrix& result_x = result.get_or_create(X);
                cuBool_Matrix temp;
                cuBool_Matrix_New(&temp, matrix_size, matrix_size);
                cuBool_Matrix_EWiseAdd(temp, result_x, product, CUBOOL_HINT_NO);
                cuBool_Matrix_Free(result_x);
                result_x = temp;
            }
            
            cuBool_Matrix_Free(product);
        }
    }

public:
    IncrementalMatrixAlgo(const cnf_grammar& gr, 
                         const label_decomposed_graph& g,
                         const OptimizationConfig& cfg = OptimizationConfig())
        : grammar(gr), graph(g), matrix_size(g.matrix_size), config(cfg) {
        classify_rules();
    }
    
    cuBool_Matrix solve() {
        auto start_time = std::chrono::high_resolution_clock::now();
        
        std::cout << "\n=== Incremental Matrix Algorithm ===" << std::endl;
        std::cout << config.to_string() << std::endl;
        std::cout << "Matrix size: " << matrix_size << std::endl;
        
        // Инициализация ΔM из графа
        CFMatrixRepresentation delta(matrix_size);
        
        // Эпсилон-правила: A → ε (создаём СНАЧАЛА, до простых правил!)
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
        
        // Простые правила: A → B (применяем в инициализации)
        // ВАЖНО: применяем только к тому что уже есть в delta (epsilon правила и граф)
        for (const auto& rule : grammar.simple_rules_) {
            symbol lhs = std::get<0>(rule);
            symbol rhs = std::get<1>(rule);
            
            // Случай 1: B - терминал из графа
            if (graph.matrices.find(rhs) != graph.matrices.end()) {
                cuBool_Index graph_nvals;
                cuBool_Matrix_Nvals(graph.matrices.at(rhs), &graph_nvals);
                
                if (graph_nvals > 0) {
                    cuBool_Matrix& d_lhs = delta.get_or_create(lhs);
                    cuBool_Matrix temp;
                    cuBool_Matrix_New(&temp, matrix_size, matrix_size);
                    cuBool_Matrix_EWiseAdd(temp, d_lhs, graph.matrices.at(rhs), CUBOOL_HINT_NO);
                    cuBool_Matrix_Free(d_lhs);
                    d_lhs = temp;
                }
            }
            
            // Случай 2: B - нетерминал (уже в delta из epsilon)
            if (delta.has(rhs)) {
                cuBool_Index nvals;
                cuBool_Matrix_Nvals(delta.matrices.at(rhs), &nvals);
                
                if (nvals > 0) {
                    cuBool_Matrix& d_lhs = delta.get_or_create(lhs);
                    cuBool_Matrix temp;
                    cuBool_Matrix_New(&temp, matrix_size, matrix_size);
                    cuBool_Matrix_EWiseAdd(temp, d_lhs, delta.matrices.at(rhs), CUBOOL_HINT_NO);
                    cuBool_Matrix_Free(d_lhs);
                    d_lhs = temp;
                }
            }
        }
        
        // Double-terminal правила (вычисляются один раз)
        apply_double_terminal(delta);
        
        cuBool_Index initial_nvals = 0;
        for (const auto& [label, matrix] : delta.matrices) {
            cuBool_Index nvals;
            cuBool_Matrix_Nvals(matrix, &nvals);
            initial_nvals += nvals;
        }
        std::cout << "Initial ΔM: " << initial_nvals << " edges" << std::endl;
        
        // M ← ∅
        CFMatrixRepresentation M(matrix_size);
        
        // Основной цикл
        std::cout << "\n=== Main incremental loop ===" << std::endl;
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
                std::cout << "Iteration " << stats.iterations << ": ΔM empty, converged" << std::endl;
                break;
            }
            
            std::cout << "Iteration " << stats.iterations << ": |ΔM| = " << delta_nvals;
            
            // ΔMₜₘₚ ← M ·Gr ΔM + ΔM ·Gr M
            CFMatrixRepresentation delta_tmp(matrix_size);
            
            apply_cnf_rules_incremental(M, delta, delta_tmp);
            apply_extended_left_incremental(M, delta, delta_tmp);
            apply_extended_right_incremental(M, delta, delta_tmp);
            apply_simple_rules_incremental(delta, delta_tmp);  // ← Только delta!
            
            // M ← M ∪ ΔM
            M.union_with(delta);
            
            // ΔM ← ΔMₜₘₚ \ M
            CFMatrixRepresentation* diff = delta_tmp.difference(M);
            
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
        
        // Выведем все нетерминалы в M
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
};