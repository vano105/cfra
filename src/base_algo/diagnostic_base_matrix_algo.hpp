#pragma once

#include "../cnf_grammar/cnf_grammar.hpp"
#include "../label_decomposed_graph/label_decomposed_graph.hpp"
#include "matrix_representation.hpp"
#include <cubool.h>
#include <iostream>

// Оптимизированный Extended CNF алгоритм
class OptimizedExtendedCNFAlgo {
private:
    cnf_grammar grammar;
    label_decomposed_graph graph;
    size_t matrix_size;
    
    using symbol = cnf_grammar::symbol;
    using complex_rule = std::tuple<symbol, symbol, symbol>;
    
    // Классификация правил
    std::vector<complex_rule> cnf_rules;           // A → B C (оба нетерминалы)
    std::vector<complex_rule> extended_left_rules; // A → B a (нетерминал + терминал)
    std::vector<complex_rule> extended_right_rules; // A → a B (терминал + нетерминал)
    
    // Предварительная классификация правил
    void classify_rules() {
        std::cout << "Classifying " << grammar.complex_rules_.size() << " rules..." << std::endl;
    
        // ДОБАВЬ ЭТО В НАЧАЛО:
        // Собираем нетерминалы из грамматики
        std::set<std::string> nonterminals;

        /*
        // В начале classify_rules(), ПЕРЕД сбором нетерминалов:
        std::set<std::string> forced_nonterminals = {"assign", "assign_r", "alloc", "alloc_r"};

        // При сборе нетерминалов:
        for (const auto& forced : forced_nonterminals) {
            nonterminals.insert(forced);
        }
    
        */
        // Стартовый нетерминал
        nonterminals.insert(grammar.start_nonterm_.label_);
    
        // Из сложных правил (только LHS)
        for (const auto& rule : grammar.complex_rules_) {
            nonterminals.insert(std::get<0>(rule));
        }
    
        // Из простых правил (только LHS)
        for (const auto& rule : grammar.simple_rules_) {
            nonterminals.insert(std::get<0>(rule));
        }
    
        // Из эпсилон-правил
        for (const auto& eps : grammar.epsilon_rules_) {
            nonterminals.insert(eps);
        }
    
        std::cout << "Collected " << nonterminals.size() << " nonterminals from grammar" << std::endl;
    
        // ТЕПЕРЬ классифицируем правила:
        for (const auto& rule : grammar.complex_rules_) {
            symbol X = std::get<0>(rule);
            symbol Y = std::get<1>(rule);
            symbol Z = std::get<2>(rule);
    
            // Используем LOCAL nonterminals, а не grammar.non_terminals!
            bool Y_is_nonterminal = (nonterminals.find(Y) != nonterminals.end());
            bool Z_is_nonterminal = (nonterminals.find(Z) != nonterminals.end());
    
            if (Y_is_nonterminal && Z_is_nonterminal) {
                cnf_rules.push_back(rule);
            } else if (Y_is_nonterminal && !Z_is_nonterminal) {
                extended_left_rules.push_back(rule);
            } else if (!Y_is_nonterminal && Z_is_nonterminal) {
                extended_right_rules.push_back(rule);
            }
        } 
    
        std::cout << "  CNF rules (A→BC): " << cnf_rules.size() << std::endl;
        std::cout << "  Extended left (A→Ba): " << extended_left_rules.size() << std::endl;
        std::cout << "  Extended right (A→aB): " << extended_right_rules.size() << std::endl;
    }
    
    // Умножение для CNF правил (A → B C)
    void apply_cnf_rules(const CFMatrixRepresentation& M, 
                        CFMatrixRepresentation& result) {
        for (const auto& rule : cnf_rules) {
            symbol X = std::get<0>(rule);
            symbol Y = std::get<1>(rule);
            symbol Z = std::get<2>(rule);
            
            if (!M.has(Y) || !M.has(Z)) continue;
            
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
    
    // Умножение для Extended правил (A → B a)
    void apply_extended_left_rules(const CFMatrixRepresentation& M,
                                   CFMatrixRepresentation& result) {
        for (const auto& rule : extended_left_rules) {
            symbol X = std::get<0>(rule);
            symbol Y = std::get<1>(rule);
            symbol Z = std::get<2>(rule); // терминал
            
            if (!M.has(Y)) continue;
            
            // Z - терминал, берём из графа
            if (graph.matrices.find(Z) == graph.matrices.end()) continue;
            
            cuBool_Index graph_nvals;
            cuBool_Matrix_Nvals(graph.matrices.at(Z), &graph_nvals);
            if (graph_nvals == 0) continue;
            
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
    
    // Умножение для Extended правил (A → a B)
    void apply_extended_right_rules(const CFMatrixRepresentation& M,
                                    CFMatrixRepresentation& result) {
        for (const auto& rule : extended_right_rules) {
            symbol X = std::get<0>(rule);
            symbol Y = std::get<1>(rule); // терминал
            symbol Z = std::get<2>(rule);
            
            if (!M.has(Z)) continue;
            
            // Y - терминал, берём из графа
            if (graph.matrices.find(Y) == graph.matrices.end()) continue;
            
            cuBool_Index graph_nvals;
            cuBool_Matrix_Nvals(graph.matrices.at(Y), &graph_nvals);
            if (graph_nvals == 0) continue;
            
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

public:
    OptimizedExtendedCNFAlgo(const cnf_grammar& gr, const label_decomposed_graph& g)
        : grammar(gr), graph(g), matrix_size(g.matrix_size) {
        classify_rules();
    }
    
    OptimizedExtendedCNFAlgo(const std::string& grammar_path, const std::string& graph_path) {
        grammar = cnf_grammar(grammar_path);
        graph = label_decomposed_graph(graph_path);
        matrix_size = graph.matrix_size;
        classify_rules();
    }
    
    cuBool_Matrix solve() {
        std::cout << "\n=== Optimized Extended CNF Algorithm ===" << std::endl;
        std::cout << "Matrix size: " << matrix_size << std::endl;
        
        CFMatrixRepresentation M(matrix_size);
        
        // Инициализация с простыми правилами
        std::cout << "\nInitializing from simple rules..." << std::endl;
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
        }
        
        // Эпсилон-правила
        std::cout << "Initializing from epsilon rules..." << std::endl;
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
        
        // Начальная статистика
        cuBool_Index total_initial = 0;
        int nonterms_initial = 0;
        for (const auto& [label, matrix] : M.matrices) {
            cuBool_Index nvals;
            cuBool_Matrix_Nvals(matrix, &nvals);
            if (nvals > 0) {
                total_initial += nvals;
                nonterms_initial++;
            }
        }
        std::cout << "Initial: " << nonterms_initial << " nonterminals, " 
                  << total_initial << " edges" << std::endl;
        
        // Основной цикл
        std::cout << "\n=== Main loop ===" << std::endl;
        bool changed = true;
        int iteration = 0;
        
        while (changed) {
            if (M.has("PT")) {
                cuBool_Index pt_nvals;
                cuBool_Matrix_Nvals(M["PT"], &pt_nvals);
                std::cout << "  PT edges: " << pt_nvals << std::endl;
            }
            iteration++;
            std::cout << "\nIteration " << iteration << std::endl;
            
            auto M_snapshot = M.clone();
            
            CFMatrixRepresentation product(matrix_size);
            
            // Применяем все типы правил
            apply_cnf_rules(M, product);
            apply_extended_left_rules(M, product);
            // apply_extended_right_rules(M, product);
            
            // Объединяем
            M.union_with(product);
            
            // Статистика
            cuBool_Index total = 0;
            int nonterms = 0;
            for (const auto& [label, matrix] : M.matrices) {
                cuBool_Index nvals;
                cuBool_Matrix_Nvals(matrix, &nvals);
                if (nvals > 0) {
                    total += nvals;
                    nonterms++;
                }
            }
            std::cout << "  After: " << nonterms << " nonterminals, " << total << " edges" << std::endl;
            
            // Проверка сходимости
            changed = !M.equals(*M_snapshot);
            delete M_snapshot;
            
            std::cout << "  Status: " << (changed ? "CHANGED" : "CONVERGED") << std::endl;
            
            if (iteration > 20) {
                std::cerr << "WARNING: Too many iterations!" << std::endl;
                break;
            }
        }
        
        std::cout << "\nTotal iterations: " << iteration << std::endl;
        
        // Финальная статистика
        cuBool_Index total_final = 0;
        int nonterms_final = 0;
        for (const auto& [label, matrix] : M.matrices) {
            cuBool_Index nvals;
            cuBool_Matrix_Nvals(matrix, &nvals);
            if (nvals > 0) {
                total_final += nvals;
                nonterms_final++;
            }
        }
        std::cout << "Final: " << nonterms_final << " nonterminals, " 
                  << total_final << " edges" << std::endl;
        
        // Возвращаем результат
        if (M.has(grammar.start_nonterm_)) {
            cuBool_Matrix result;
            cuBool_Matrix_Duplicate(M[grammar.start_nonterm_], &result);
            
            cuBool_Index result_nvals;
            cuBool_Matrix_Nvals(result, &result_nvals);
            std::cout << "Start nonterminal '" << grammar.start_nonterm_.label_ 
                      << "': " << result_nvals << " pairs" << std::endl;
            
            return result;
        } else {
            std::cout << "WARNING: Start nonterminal not found!" << std::endl;
            cuBool_Matrix empty;
            cuBool_Matrix_New(&empty, matrix_size, matrix_size);
            cuBool_Matrix_Build(empty, nullptr, nullptr, 0, CUBOOL_HINT_NO);
            return empty;
        }
    }
};