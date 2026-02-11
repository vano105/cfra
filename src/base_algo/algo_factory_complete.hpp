#pragma once

#include "incremental_matrix_algo.hpp"
#include "fully_optimized_algo.hpp"
#include "optimization_config.hpp"
#include <string>
#include <iostream>
#include <iomanip>
#include <vector>
#include <chrono>
#include <climits>
#include <stdexcept>
#include <algorithm>

/**
 * Фабрика для создания различных версий алгоритма КС-достижимости
 * 
 * Поддерживает все оптимизации из работы Муравьева:
 * - INCREMENTAL: С инкрементальными вычислениями (3.1)
 * - TRIVIAL_OPT: + проверка тривиальных операций (3.3)
 * - LAZY_ADD: + отложенное сложение (3.5)
 * - FULLY_OPTIMIZED: Все оптимизации
 * - AUTO: Автоматический выбор на основе размера входа
 */
class CFReachabilityAlgoFactory {
public:
    enum class AlgoType {
        INCREMENTAL,       // Инкрементальные вычисления (O(n⁴))
        TRIVIAL_OPT,       // + проверка тривиальности (O(n⁴) с ускорением)
        LAZY_ADD,          // + отложенное сложение (O(n³))
        FULLY_OPTIMIZED,   // Все оптимизации (O(n³))
        AUTO               // Автоматический выбор
    };
    
    /**
     * Конвертация AlgoType в строку
     */
    static std::string algo_type_to_string(AlgoType type) {
        switch (type) {
            case AlgoType::INCREMENTAL: return "INCREMENTAL";
            case AlgoType::TRIVIAL_OPT: return "TRIVIAL_OPT";
            case AlgoType::LAZY_ADD: return "LAZY_ADD";
            case AlgoType::FULLY_OPTIMIZED: return "FULLY_OPTIMIZED";
            case AlgoType::AUTO: return "AUTO";
            default: return "UNKNOWN";
        }
    }
    
    /**
     * Конвертация строки в AlgoType
     */
    static AlgoType string_to_algo_type(const std::string& str) {
        std::string upper = str;
        std::transform(upper.begin(), upper.end(), upper.begin(), ::toupper);
        
        if (upper == "INCREMENTAL") return AlgoType::INCREMENTAL;
        if (upper == "TRIVIAL_OPT" || upper == "TRIVIAL") return AlgoType::TRIVIAL_OPT;
        if (upper == "LAZY_ADD" || upper == "LAZY") return AlgoType::LAZY_ADD;
        if (upper == "FULLY_OPTIMIZED" || upper == "FULL" || upper == "OPTIMIZED") 
            return AlgoType::FULLY_OPTIMIZED;
        if (upper == "AUTO") return AlgoType::AUTO;
        
        throw std::invalid_argument("Unknown algorithm type: " + str);
    }
    
    static cuBool_Matrix solve(const std::string& grammar_path,
                              const std::string& graph_path,
                              AlgoType type = AlgoType::AUTO) {
        cnf_grammar grammar(grammar_path);
        label_decomposed_graph graph(graph_path);
        
        return solve(grammar, graph, type);
    }
    
    static cuBool_Matrix solve(const cnf_grammar& grammar,
                              const label_decomposed_graph& graph,
                              AlgoType type = AlgoType::AUTO) {
        
        // Автоматический выбор алгоритма на основе размера
        if (type == AlgoType::AUTO) {
            type = choose_algo_type(graph.matrix_size, grammar);
        }
        
        switch (type) {
            case AlgoType::INCREMENTAL: {
                std::cout << "=== Using INCREMENTAL algorithm ===" << std::endl;
                OptimizationConfig config;
                config.use_trivial_checks = false;
                config.use_lazy_add = false;
                IncrementalMatrixAlgo algo(grammar, graph, config);
                return algo.solve();
            }
            
            case AlgoType::TRIVIAL_OPT: {
                std::cout << "=== Using INCREMENTAL + TRIVIAL_OPT ===" << std::endl;
                OptimizationConfig config;
                config.use_trivial_checks = true;
                config.use_lazy_add = false;
                IncrementalMatrixAlgo algo(grammar, graph, config);
                return algo.solve();
            }
            
            case AlgoType::LAZY_ADD: {
                std::cout << "=== Using INCREMENTAL + LAZY_ADD ===" << std::endl;
                OptimizationConfig config;
                config.use_trivial_checks = false;
                config.use_lazy_add = true;
                config.lazy_add_exponent = 0.5;  // b = n^0.5
                FullyOptimizedAlgo algo(grammar, graph, config);
                return algo.solve();
            }
            
            case AlgoType::FULLY_OPTIMIZED: {
                std::cout << "=== Using FULLY OPTIMIZED algorithm ===" << std::endl;
                OptimizationConfig config;
                config.use_trivial_checks = true;
                config.use_lazy_add = true;
                config.lazy_add_exponent = 0.5;  // b = n^0.5
                FullyOptimizedAlgo algo(grammar, graph, config);
                return algo.solve();
            }
            
            case AlgoType::AUTO:
                // Недостижимо, обработано выше
                break;
        }
        
        // Не должно достигаться
        std::cerr << "ERROR: Unknown algorithm type!" << std::endl;
        cuBool_Matrix empty;
        cuBool_Matrix_New(&empty, graph.matrix_size, graph.matrix_size);
        return empty;
    }
    
    /**
     * Автоматический выбор алгоритма на основе характеристик входа
     */
    static AlgoType choose_algo_type(size_t n, const cnf_grammar& grammar) {
        std::cout << "\n=== AUTO mode: choosing algorithm ===" << std::endl;
        std::cout << "Graph size: " << n << " vertices" << std::endl;
        std::cout << "Grammar: " << grammar.complex_rules_.size() << " complex rules, "
                  << grammar.simple_rules_.size() << " simple rules, "
                  << grammar.epsilon_rules_.size() << " epsilon rules" << std::endl;
        
        // Эвристика выбора:
        // - n < 500: INCREMENTAL + TRIVIAL (быстрее для маленьких)
        // - n >= 500: FULLY_OPTIMIZED (оптимально для больших)
        
        AlgoType chosen;
        
        if (n < 500) {
            chosen = AlgoType::TRIVIAL_OPT;
            std::cout << "Chosen: TRIVIAL_OPT (small/medium graph)" << std::endl;
        } else {
            chosen = AlgoType::FULLY_OPTIMIZED;
            std::cout << "Chosen: FULLY_OPTIMIZED (large graph)" << std::endl;
        }
        
        return chosen;
    }
    
    /**
     * Получить название алгоритма (синоним для algo_type_to_string)
     */
    static std::string get_algo_name(AlgoType type) {
        return algo_type_to_string(type);
    }
    
    /**
     * Вывести список доступных алгоритмов
     */
    static void print_available_algorithms() {
        std::cout << "\nAvailable algorithms:" << std::endl;
        std::cout << "  INCREMENTAL       - Incremental computations O(n⁴)" << std::endl;
        std::cout << "  TRIVIAL_OPT       - + Trivial operations check" << std::endl;
        std::cout << "  LAZY_ADD          - + Lazy addition O(n³)" << std::endl;
        std::cout << "  FULLY_OPTIMIZED   - All optimizations O(n³)" << std::endl;
        std::cout << "  AUTO              - Automatic selection" << std::endl;
    }
    
    /**
     * Запустить сравнение всех алгоритмов
     */
    static void benchmark_all(const cnf_grammar& grammar, 
                              const label_decomposed_graph& graph) {
        std::cout << "\n" << std::string(70, '=') << std::endl;
        std::cout << "BENCHMARKING ALL ALGORITHMS" << std::endl;
        std::cout << std::string(70, '=') << std::endl;
        
        std::vector<AlgoType> algos = {
            AlgoType::INCREMENTAL,
            AlgoType::TRIVIAL_OPT,
            AlgoType::LAZY_ADD,
            AlgoType::FULLY_OPTIMIZED
        };
        
        struct Result {
            std::string name;
            cuBool_Index edges;
            long long time_ms;
            bool success;
        };
        
        std::vector<Result> results;
        
        for (auto algo_type : algos) {
            Result result;
            result.name = get_algo_name(algo_type);
            result.success = false;
            
            try {
                std::cout << "\n" << std::string(70, '-') << std::endl;
                auto start = std::chrono::high_resolution_clock::now();
                
                cuBool_Matrix res = solve(grammar, graph, algo_type);
                
                auto end = std::chrono::high_resolution_clock::now();
                auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
                    end - start
                );
                
                cuBool_Matrix_Nvals(res, &result.edges);
                result.time_ms = duration.count();
                result.success = true;
                
                cuBool_Matrix_Free(res);
                
            } catch (const std::exception& e) {
                std::cerr << "ERROR: " << e.what() << std::endl;
                result.edges = 0;
                result.time_ms = -1;
            }
            
            results.push_back(result);
        }
        
        // Вывод таблицы результатов
        std::cout << "\n" << std::string(70, '=') << std::endl;
        std::cout << "BENCHMARK RESULTS" << std::endl;
        std::cout << std::string(70, '=') << std::endl;
        
        std::cout << std::left << std::setw(20) << "Algorithm" 
                  << std::right << std::setw(15) << "Edges" 
                  << std::setw(15) << "Time (ms)"
                  << std::setw(15) << "Status" << std::endl;
        std::cout << std::string(70, '-') << std::endl;
        
        for (const auto& r : results) {
            std::cout << std::left << std::setw(20) << r.name;
            
            if (r.success) {
                std::cout << std::right << std::setw(15) << r.edges
                          << std::setw(15) << r.time_ms
                          << std::setw(15) << "OK";
            } else {
                std::cout << std::right << std::setw(15) << "-"
                          << std::setw(15) << "-"
                          << std::setw(15) << "FAILED";
            }
            std::cout << std::endl;
        }
        
        // Проверка корректности (все алгоритмы должны давать одинаковый результат)
        std::cout << std::string(70, '-') << std::endl;
        
        bool all_match = true;
        cuBool_Index reference_edges = 0;
        
        for (const auto& r : results) {
            if (r.success) {
                if (reference_edges == 0) {
                    reference_edges = r.edges;
                } else if (r.edges != reference_edges) {
                    all_match = false;
                    std::cerr << "WARNING: " << r.name << " produced different result!" << std::endl;
                }
            }
        }
        
        if (all_match) {
            std::cout << "✓ All algorithms produced the same result: " 
                      << reference_edges << " edges" << std::endl;
        } else {
            std::cerr << "✗ MISMATCH: Different algorithms produced different results!" << std::endl;
        }
        
        // Вывод лучшего времени
        long long best_time = LLONG_MAX;
        std::string best_algo;
        
        for (const auto& r : results) {
            if (r.success && r.time_ms < best_time) {
                best_time = r.time_ms;
                best_algo = r.name;
            }
        }
        
        if (best_time < LLONG_MAX) {
            std::cout << "✓ Fastest: " << best_algo << " (" << best_time << " ms)" << std::endl;
        }
        
        std::cout << std::string(70, '=') << std::endl;
    }
};