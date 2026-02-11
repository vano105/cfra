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
 * 
 * ИСПРАВЛЕНИЯ:
 * - Убраны else if в apply_cnf (все случаи независимы)
 * - Простые правила применяются только к delta
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
    
    bool is_empty(cuBool_Matrix matrix) {
        if (!config.use_trivial_checks) return false;
        
        cuBool_Index nvals;
        cuBool_Matrix_Nvals(matrix, &nvals);
        return nvals == 0;
    }
    
    /**
     * M ·Gr ΔM для CNF правил
     * 
     * КРИТИЧЕСКОЕ ИСПРАВЛЕНИЕ: все три случая независимы (не else if)!
     */
    void apply_cnf_rules_incremental(const CFMatrixRepresentation& M,
                                     const CFMatrixRepresentation& delta,
                                     CFMatrixRepresentation& result) {
        for (const auto& rule : cnf_rules) {
            symbol X = std::get<0>(rule);
            symbol Y = std::get<1>(rule);
            symbol Z = std::get<2>(rule);
            
            // Случай 1: delta[Y] · delta[Z] (оба новые)
            if (delta.has(Y) && delta.has(Z)) {
                if (config.use_trivial_checks && 
                    (is_empty(delta.matrices.at(Y)) || is_empty(delta.matrices.at(Z)))) {
                    stats.skipped_multiplications++;
                } else {
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
            }
            
            // Случай 2: M[Y] · delta[Z] (Y существует, Z новый)
            // НЕ else if! Независимый случай
            if (M.has(Y) && delta.has(Z)) {
                if (config.use_trivial_checks && 
                    (is_empty(M.matrices.at(Y)) || is_empty(delta.matrices.at(Z)))) {
                    stats.skipped_multiplications++;
                } else {
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
            }
            
            // Случай 3: delta[Y] · M[Z] (Y новый, Z существует)
            if (delta.has(Y) && M.has(Z)) {
                if (config.use_trivial_checks && 
                    (is_empty(delta.matrices.at(Y)) || is_empty(M.matrices.at(Z)))) {
                    stats.skipped_multiplications++;
                } else {
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
    }
    
    // Extended left: A → Ba
    void apply_extended_left_incremental(const CFMatrixRepresentation& M,
                                        const CFMatrixRepresentation& delta,
                                        CFMatrixRepresentation& result) {
        for (const auto& rule : extended_left_rules) {
            symbol X = std::get<0>(rule);
            symbol Y = std::get<1>(rule);  // нетерминал
            symbol Z = std::get<2>(rule);  // терминал
            
            if (graph.matrices.find(Z) == graph.matrices.end()) continue;
            
            // delta[Y] · graph[Z]
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
            
            // M[Y] · graph[Z]
            if (M.has(Y)) {
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
            symbol Y = std::get<1>(rule);  // терминал
            symbol Z = std::get<2>(rule);  // нетерминал
            
            if (graph.matrices.find(Y) == graph.matrices.end()) continue;
            
            // graph[Y] · delta[Z]
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
            
            // КРИТИЧЕСКОЕ ИСПРАВЛЕНИЕ: graph[Y] · M[Z] тоже!
            if (M.has(Z)) {
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
    
    /**
     * Простые правила A → B
     * ИСПРАВЛЕНИЕ: применяются ТОЛЬКО к delta!
     * Семантика: delta[A] = delta[B] (новые в A = новые в B)
     */
    void apply_simple_rules_incremental(const CFMatrixRepresentation& delta,
                                        CFMatrixRepresentation& result) {
        for (const auto& rule : grammar.simple_rules_) {
            symbol A = std::get<0>(rule);
            symbol B = std::get<1>(rule);
            
            if (delta.has(B)) {
                cuBool_Matrix& result_a = result.get_or_create(A);
                cuBool_Matrix temp;
                cuBool_Matrix_New(&temp, matrix_size, matrix_size);
                cuBool_Matrix_EWiseAdd(temp, result_a, delta.matrices.at(B), CUBOOL_HINT_NO);
                cuBool_Matrix_Free(result_a);
                result_a = temp;
            }
        }
    }
    
    // Double-terminal правила (вычисляются один раз)
    void apply_double_terminal(CFMatrixRepresentation& delta) {
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
                cuBool_Matrix& d_x = delta.get_or_create(X);
                cuBool_Matrix temp;
                cuBool_Matrix_New(&temp, matrix_size, matrix_size);
                cuBool_Matrix_EWiseAdd(temp, d_x, product, CUBOOL_HINT_NO);
                cuBool_Matrix_Free(d_x);
                d_x = temp;
            }
            
            cuBool_Matrix_Free(product);
        }
    }

public:
    IncrementalMatrixAlgo(const cnf_grammar& gr, const label_decomposed_graph& g,
                         const OptimizationConfig& cfg = OptimizationConfig())
        : grammar(gr), graph(g), matrix_size(g.matrix_size), config(cfg) {
        classify_rules();
    }
    
    IncrementalMatrixAlgo(const std::string& grammar_path, const std::string& graph_path,
                         const OptimizationConfig& cfg = OptimizationConfig()) {
        grammar = cnf_grammar(grammar_path);
        graph = label_decomposed_graph(graph_path);
        matrix_size = graph.matrix_size;
        config = cfg;
        classify_rules();
    }
    
    cuBool_Matrix solve() {
        auto start_time = std::chrono::high_resolution_clock::now();
        
        std::cout << "\n=== Incremental Matrix Algorithm ===" << std::endl;
        std::cout << config.to_string() << std::endl;
        std::cout << "Matrix size: " << matrix_size << std::endl;
        
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
                    cuBool_Matrix_EWiseAdd(temp, d_lhs, graph.matrices.at(rhs), CUBOOL_HINT_NO);
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
            apply_simple_rules_incremental(delta, delta_tmp);
            
            // M ← M ∪ ΔM
            M.union_with(delta);
            
            // ΔM ← ΔMₜₘₚ \ M
            CFMatrixRepresentation* diff = delta_tmp.difference(M);
            
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
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
            end_time - start_time
        );
        
        std::cout << "\n=== Results ===" << std::endl;
        std::cout << "Iterations: " << stats.iterations << std::endl;
        std::cout << "Total time: " << duration.count() << " ms" << std::endl;
        std::cout << "Multiplications: " << stats.total_multiplications << std::endl;
        std::cout << "Skipped (trivial): " << stats.skipped_multiplications << std::endl;
        
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