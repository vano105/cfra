#pragma once

#include "../cnf_grammar/cnf_grammar.hpp"
#include "../label_decomposed_graph/label_decomposed_graph.hpp"
#include "matrix_representation.hpp"
#include <cubool.h>
#include <iostream>

class matrix_base_algo {
private:
    cnf_grammar grammar;
    label_decomposed_graph graph;
    size_t matrix_size;
    
    using symbol = cnf_grammar::symbol;
    
    // Операция конкатенации путей для GB-полукольца КС-достижимости
    // (A ·Gr B)_X = ∑∨ {A_Y ·Bool B_Z | (X → Y Z) ∈ P}
    void multiply_cf_matrices(const CFMatrixRepresentation& A, 
                              const CFMatrixRepresentation& B,
                              CFMatrixRepresentation& result) {
        // Для каждого сложного правила X → Y Z
        for (const auto& rule : grammar.complex_rules_) {
            symbol X = std::get<0>(rule);
            symbol Y = std::get<1>(rule);
            symbol Z = std::get<2>(rule);
            
            // Проверяем наличие матриц для Y и Z
            if (!A.has(Y) || !B.has(Z)) continue;
            
            // Вычисляем A_Y · B_Z
            cuBool_Matrix product;
            cuBool_Matrix_New(&product, matrix_size, matrix_size);
            cuBool_MxM(product, A.matrices.at(Y), B.matrices.at(Z), CUBOOL_HINT_NO);
            
            // Проверяем, есть ли ненулевые элементы
            cuBool_Index nvals;
            cuBool_Matrix_Nvals(product, &nvals);
            
            if (nvals > 0) {
                // Добавляем к результату для нетерминала X
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
    matrix_base_algo(const cnf_grammar& gr, const label_decomposed_graph& g)
        : grammar(gr), graph(g), matrix_size(g.matrix_size) {}
    
    matrix_base_algo(const std::string& grammar_path, const std::string& graph_path) {
        grammar = cnf_grammar(grammar_path);
        graph = label_decomposed_graph(graph_path);
        matrix_size = graph.matrix_size;
    }
    
    cuBool_Matrix solve() {
        std::cout << "Matrix size: " << matrix_size << std::endl;
        std::cout << "Number of nonterminals: " << grammar.non_terminals.size() << std::endl;
        std::cout << "Number of complex rules: " << grammar.complex_rules_.size() << std::endl;
        std::cout << "Number of simple rules: " << grammar.simple_rules_.size() << std::endl;
        std::cout << "Number of epsilon rules: " << grammar.epsilon_rules_.size() << std::endl;
        
        CFMatrixRepresentation M(matrix_size);
        
        // 1. Обработка простых правил: A → a
        std::cout << "\nInitializing with simple rules..." << std::endl;
        int simple_initialized = 0;
        for (const auto& rule : grammar.simple_rules_) {
            symbol lhs = std::get<0>(rule);
            symbol rhs = std::get<1>(rule);
            
            // Если rhs - терминал и есть соответствующие рёбра в графе
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
                    simple_initialized++;
                }
            }
        }
        std::cout << "Initialized " << simple_initialized << " nonterminals from simple rules" << std::endl;
        
        // 2. Обработка эпсилон-правил: A → ε
        std::cout << "\nInitializing with epsilon rules..." << std::endl;
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
        std::cout << "Initialized " << grammar.epsilon_rules_.size() << " nonterminals from epsilon rules" << std::endl;
        
        // Выводим начальное состояние
        std::cout << "\nInitial state:" << std::endl;
        cuBool_Index total_initial = 0;
        for (const auto& [label, matrix] : M.matrices) {
            cuBool_Index nvals;
            cuBool_Matrix_Nvals(matrix, &nvals);
            total_initial += nvals;
        }
        std::cout << "Total edges in initial M: " << total_initial << std::endl;
        
        // 3. Основной цикл: пока M меняется
        std::cout << "\n=== Starting main loop ===" << std::endl;
        bool changed = true;
        int iteration = 0;
        
        while (changed) {
            iteration++;
            std::cout << "\n--- Iteration " << iteration << " ---" << std::endl;
            
            // Подсчитываем текущее состояние (только непустые!)
            cuBool_Index total_before = 0;
            int nonterminals_before = 0;
            for (const auto& [label, matrix] : M.matrices) {
                cuBool_Index nvals;
                cuBool_Matrix_Nvals(matrix, &nvals);
                if (nvals > 0) {
                    total_before += nvals;
                    nonterminals_before++;
                }
            }
            std::cout << "M before: " << nonterminals_before << " nonterminals, " 
                      << total_before << " total edges" << std::endl;
            
            // Сохраняем снимок для сравнения
            auto M_snapshot = M.clone();
            
            // Вычисляем M ·Gr M
            CFMatrixRepresentation product(matrix_size);
            multiply_cf_matrices(M, M, product);
            
            // Подсчитываем размер произведения (только непустые!)
            cuBool_Index product_total = 0;
            int product_nonterminals = 0;
            for (const auto& [label, matrix] : product.matrices) {
                cuBool_Index nvals;
                cuBool_Matrix_Nvals(matrix, &nvals);
                if (nvals > 0) {
                    product_total += nvals;
                    product_nonterminals++;
                }
            }
            std::cout << "Product (M ·Gr M): " << product_nonterminals 
                      << " nonterminals, " << product_total << " total edges" << std::endl;
            
            // M ← M ∪ (M ·Gr M)
            M.union_with(product);
            
            // Подсчитываем после объединения (только непустые!)
            cuBool_Index total_after = 0;
            int nonterminals_after = 0;
            for (const auto& [label, matrix] : M.matrices) {
                cuBool_Index nvals;
                cuBool_Matrix_Nvals(matrix, &nvals);
                if (nvals > 0) {
                    total_after += nvals;
                    nonterminals_after++;
                }
            }
            std::cout << "M after union: " << nonterminals_after << " nonterminals, " 
                      << total_after << " total edges" << std::endl;
            std::cout << "Added: " << (total_after - total_before) << " new edges" << std::endl;
            
            // Проверяем изменения: сравниваем M с M_snapshot
            changed = !M.equals(*M_snapshot);
            
            // Освобождаем память
            delete M_snapshot;
            
            if (changed) {
                std::cout << "Status: CHANGED - continuing" << std::endl;
            } else {
                std::cout << "Status: CONVERGED - stopping" << std::endl;
            }
            
            // Защита от бесконечного цикла
            if (iteration > 100) {
                std::cerr << "WARNING: Too many iterations (>100), stopping!" << std::endl;
                break;
            }
        }
        
        std::cout << "\n=== Convergence achieved ===" << std::endl;
        std::cout << "Total iterations: " << iteration << std::endl;
        
        // Финальная статистика
        std::cout << "\nFinal state:" << std::endl;
        cuBool_Index total_final = 0;
        for (const auto& [label, matrix] : M.matrices) {
            cuBool_Index nvals;
            cuBool_Matrix_Nvals(matrix, &nvals);
            total_final += nvals;
        }
        std::cout << "Total edges in final M: " << total_final << std::endl;
        
        // Возвращаем матрицу для стартового нетерминала
        if (M.has(grammar.start_nonterm_)) {
            cuBool_Matrix result;
            cuBool_Matrix_Duplicate(M[grammar.start_nonterm_], &result);
            
            cuBool_Index result_nvals;
            cuBool_Matrix_Nvals(result, &result_nvals);
            
            return result;
        } else {
            cuBool_Matrix empty;
            cuBool_Matrix_New(&empty, matrix_size, matrix_size);
            cuBool_Matrix_Build(empty, nullptr, nullptr, 0, CUBOOL_HINT_NO);
            return empty;
        }
    }
};