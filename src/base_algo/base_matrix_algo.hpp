#pragma once

#include "../cnf_grammar/cnf_grammar.hpp"
#include "../label_decomposed_graph/label_decomposed_graph.hpp"
#include "matrix_representation.hpp"
#include <cubool.h>
#include <iostream>
#include <chrono>
#include <set>

/**
 * ПОЛНОСТЬЮ ИСПРАВЛЕННЫЙ базовый матричный алгоритм КС-достижимости
 * 
 * Все исправления:
 * 1. Правильная классификация правил
 * 2. НЕ используем graph в основном цикле (только в инициализации)
 * 3. Убраны ВСЕ else if - применяем ВСЕ комбинации
 * 4. Simple rules: для нетерминалов в цикле
 * 5. Инициализация: epsilon + simple rules ОДИН РАЗ
 */
class matrix_base_algo_fixed {
private:
    cnf_grammar grammar;
    label_decomposed_graph graph;
    size_t matrix_size;
    
    using symbol = cnf_grammar::symbol;
    using complex_rule = std::tuple<symbol, symbol, symbol>;
    
    std::vector<complex_rule> cnf_rules;
    std::vector<complex_rule> extended_left_rules;
    std::vector<complex_rule> extended_right_rules;
    std::vector<complex_rule> double_terminal_rules;
    std::set<std::string> nonterminals;
    
    void classify_rules() {
        nonterminals.clear();
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
    
    void apply_rules(const CFMatrixRepresentation& M, CFMatrixRepresentation& result) {
        // CNF rules: A → B C
        for (const auto& rule : cnf_rules) {
            symbol X = std::get<0>(rule);
            symbol Y = std::get<1>(rule);
            symbol Z = std::get<2>(rule);
            
            // Применяем ВСЕ комбинации (без else if!)
            if (M.has(Y) && M.has(Z)) {
                cuBool_Matrix product;
                cuBool_Matrix_New(&product, matrix_size, matrix_size);
                cuBool_MxM(product, M.matrices.at(Y), M.matrices.at(Z), CUBOOL_HINT_NO);
                
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
        
        // Extended LEFT: A → B c (используем graph для терминала!)
        for (const auto& rule : extended_left_rules) {
            symbol X = std::get<0>(rule);
            symbol Y = std::get<1>(rule);
            symbol Z = std::get<2>(rule);
            
            if (M.has(Y) && graph.matrices.find(Z) != graph.matrices.end()) {
                cuBool_Matrix product;
                cuBool_Matrix_New(&product, matrix_size, matrix_size);
                cuBool_MxM(product, M.matrices.at(Y), graph.matrices.at(Z), CUBOOL_HINT_NO);
                
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
        
        // Extended RIGHT: A → a C (используем graph для терминала!)
        for (const auto& rule : extended_right_rules) {
            symbol X = std::get<0>(rule);
            symbol Y = std::get<1>(rule);
            symbol Z = std::get<2>(rule);
            
            if (graph.matrices.find(Y) != graph.matrices.end() && M.has(Z)) {
                cuBool_Matrix product;
                cuBool_Matrix_New(&product, matrix_size, matrix_size);
                cuBool_MxM(product, graph.matrices.at(Y), M.matrices.at(Z), CUBOOL_HINT_NO);
                
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
    
    void apply_simple_rules(const CFMatrixRepresentation& M, CFMatrixRepresentation& result) {
        for (const auto& rule : grammar.simple_rules_) {
            symbol A = std::get<0>(rule);
            symbol B = std::get<1>(rule);
            
            // Применяем ТОЛЬКО для нетерминалов (как в Python)
            if (nonterminals.find(B) == nonterminals.end()) continue;
            
            if (M.has(B)) {
                cuBool_Index nvals;
                cuBool_Matrix_Nvals(M.matrices.at(B), &nvals);
                if (nvals > 0) {
                    cuBool_Matrix& result_a = result.get_or_create(A);
                    cuBool_Matrix temp;
                    cuBool_Matrix_New(&temp, matrix_size, matrix_size);
                    cuBool_Matrix_EWiseAdd(temp, result_a, M.matrices.at(B), CUBOOL_HINT_NO);
                    cuBool_Matrix_Free(result_a);
                    result_a = temp;
                }
            }
        }
    }

public:
    matrix_base_algo_fixed(const cnf_grammar& gr, const label_decomposed_graph& g)
        : grammar(gr), graph(g), matrix_size(g.matrix_size) {
        classify_rules();
    }
    
    cuBool_Matrix solve() {
        auto start_time = std::chrono::high_resolution_clock::now();
        
        std::cout << "\n=== Corrected Base Matrix Algorithm ===" << std::endl;
        std::cout << "Matrix size: " << matrix_size << std::endl;
        
        CFMatrixRepresentation M(matrix_size);
        
        // Epsilon rules
        std::vector<cuBool_Index> diag_rows, diag_cols;
        for (size_t i = 0; i < matrix_size; i++) {
            diag_rows.push_back(i);
            diag_cols.push_back(i);
        }
        
        for (const auto& eps_rule : grammar.epsilon_rules_) {
            cuBool_Matrix& m_eps = M.get_or_create(eps_rule);
            cuBool_Matrix identity;
            cuBool_Matrix_New(&identity, matrix_size, matrix_size);
            cuBool_Matrix_Build(identity, diag_rows.data(), diag_cols.data(), 
                              matrix_size, CUBOOL_HINT_NO);
            
            cuBool_Matrix temp;
            cuBool_Matrix_New(&temp, matrix_size, matrix_size);
            cuBool_Matrix_EWiseAdd(temp, m_eps, identity, CUBOOL_HINT_NO);
            cuBool_Matrix_Free(m_eps);
            cuBool_Matrix_Free(identity);
            m_eps = temp;
        }
        
        // Simple rules ОДИН РАЗ
        for (const auto& rule : grammar.simple_rules_) {
            symbol lhs = std::get<0>(rule);
            symbol rhs = std::get<1>(rule);
            
            if (graph.matrices.find(rhs) != graph.matrices.end()) {
                cuBool_Index graph_nvals;
                cuBool_Matrix_Nvals(graph.matrices.at(rhs), &graph_nvals);
                if (graph_nvals > 0) {
                    cuBool_Matrix& m_lhs = M.get_or_create(lhs);
                    cuBool_Matrix temp;
                    cuBool_Matrix_New(&temp, matrix_size, matrix_size);
                    cuBool_Matrix_EWiseAdd(temp, m_lhs, graph.matrices.at(rhs), CUBOOL_HINT_NO);
                    cuBool_Matrix_Free(m_lhs);
                    m_lhs = temp;
                }
            }
            
            if (M.has(rhs)) {
                cuBool_Index nvals;
                cuBool_Matrix_Nvals(M.matrices.at(rhs), &nvals);
                if (nvals > 0) {
                    cuBool_Matrix& m_lhs = M.get_or_create(lhs);
                    cuBool_Matrix temp;
                    cuBool_Matrix_New(&temp, matrix_size, matrix_size);
                    cuBool_Matrix_EWiseAdd(temp, m_lhs, M.matrices.at(rhs), CUBOOL_HINT_NO);
                    cuBool_Matrix_Free(m_lhs);
                    m_lhs = temp;
                }
            }
        }
        
        // Double-terminal rules
        for (const auto& rule : double_terminal_rules) {
            symbol X = std::get<0>(rule);
            symbol Y = std::get<1>(rule);
            symbol Z = std::get<2>(rule);
            
            if (graph.matrices.find(Y) == graph.matrices.end()) continue;
            if (graph.matrices.find(Z) == graph.matrices.end()) continue;
            
            cuBool_Matrix product;
            cuBool_Matrix_New(&product, matrix_size, matrix_size);
            cuBool_MxM(product, graph.matrices.at(Y), graph.matrices.at(Z), CUBOOL_HINT_NO);
            
            cuBool_Index nvals;
            cuBool_Matrix_Nvals(product, &nvals);
            if (nvals > 0) {
                cuBool_Matrix& m_x = M.get_or_create(X);
                cuBool_Matrix temp;
                cuBool_Matrix_New(&temp, matrix_size, matrix_size);
                cuBool_Matrix_EWiseAdd(temp, m_x, product, CUBOOL_HINT_NO);
                cuBool_Matrix_Free(m_x);
                m_x = temp;
            }
            cuBool_Matrix_Free(product);
        }
        
        // Основной цикл
        bool changed = true;
        int iterations = 0;
        
        while (changed) {
            iterations++;
            changed = false;
            
            cuBool_Index old_nvals = 0;
            for (const auto& [label, matrix] : M.matrices) {
                cuBool_Index nvals;
                cuBool_Matrix_Nvals(matrix, &nvals);
                old_nvals += nvals;
            }
            
            CFMatrixRepresentation M_new(matrix_size);
            
            // Применяем все правила
            apply_rules(M, M_new);
            apply_simple_rules(M, M_new);  // Simple rules для нетерминалов
            
            // M = M ∪ M_new
            for (const auto& [label, matrix] : M_new.matrices) {
                cuBool_Matrix& m_label = M.get_or_create(label);
                cuBool_Matrix temp;
                cuBool_Matrix_New(&temp, matrix_size, matrix_size);
                cuBool_Matrix_EWiseAdd(temp, m_label, matrix, CUBOOL_HINT_NO);
                cuBool_Matrix_Free(m_label);
                m_label = temp;
            }
            
            cuBool_Index new_nvals = 0;
            for (const auto& [label, matrix] : M.matrices) {
                cuBool_Index nvals;
                cuBool_Matrix_Nvals(matrix, &nvals);
                new_nvals += nvals;
            }
            
            std::cout << "Iteration " << iterations << ": |M| = " << new_nvals << std::endl;
            
            if (new_nvals > old_nvals) {
                changed = true;
            }
            
            if (iterations > 100) break;
        }
        
        std::cout << "Converged after " << iterations << " iterations" << std::endl;
        
        auto end_time = std::chrono::high_resolution_clock::now();
        std::chrono::duration<double> elapsed = end_time - start_time;
        std::cout << "Time: " << elapsed.count() << " seconds" << std::endl;
        
        if (M.has(grammar.start_nonterm_)) {
            cuBool_Matrix result;
            cuBool_Matrix_Duplicate(M.matrices.at(grammar.start_nonterm_), &result);
            return result;
        } else {
            cuBool_Matrix empty;
            cuBool_Matrix_New(&empty, matrix_size, matrix_size);
            cuBool_Matrix_Build(empty, nullptr, nullptr, 0, CUBOOL_HINT_NO);
            return empty;
        }
    }
};