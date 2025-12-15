#pragma once

#include "base_matrix_algo.hpp"
#include "incremental_matrix_algo.hpp"
#include "fully_optimized_algo.hpp"
#include "optimization_config.hpp"
#include <string>
#include <iostream>
#include <iomanip>
#include <vector>
#include <chrono>

/**
 * Фабрика для создания различных версий алгоритма КС-достижимости
 * 
 * Поддерживает все оптимизации из работы Муравьева:
 * - BASE: Базовая версия (исправленная)
 * - INCREMENTAL: С инкрементальными вычислениями (3.1)
 * - TRIVIAL_OPT: + проверка тривиальных операций (3.3)
 * - LAZY_ADD: + отложенное сложение (3.5)
 * - FULLY_OPTIMIZED: Все оптимизации
 * - AUTO: Автоматический выбор на основе размера входа
 */
class CFReachabilityAlgoFactory {
public:
    enum class AlgoType {
        BASE,              // Базовый алгоритм (O(n⁵))
        INCREMENTAL,       // + инкрементальные вычисления (O(n⁴))
        TRIVIAL_OPT,       // + проверка тривиальности (O(n⁴) с ускорением)
        LAZY_ADD,          // + отложенное сложение (O(n³))
        FULLY_OPTIMIZED,   // Все оптимизации (O(n³))
        AUTO               // Автоматический выбор
    };
    
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
        switch (type) {
            case AlgoType::BASE: {
                std::cout << "=== Using BASE algorithm ===" << std::endl;
                matrix_base_algo_fixed algo(grammar, graph);
                return algo.solve();
            }
            
            case AlgoType::INCREMENTAL: {
                std::cout << "=== Using INCREMENTAL algorithm ===" << std::endl;
                OptimizationConfig config;
                config.use_incremental = true;
                config.use_trivial_checks = false;  // Явно отключаем
                IncrementalMatrixAlgo algo(grammar, graph, config);
                return algo.solve();
            }
            
            case AlgoType::TRIVIAL_OPT: {
                std::cout << "=== Using INCREMENTAL + TRIVIAL algorithm ===" << std::endl;
                OptimizationConfig config;
                config.use_incremental = true;
                config.use_trivial_checks = true;
                config.enable_stats = true;
                IncrementalMatrixAlgo algo(grammar, graph, config);
                return algo.solve();
            }
            
            case AlgoType::LAZY_ADD: {
                std::cout << "=== Using INCREMENTAL + LAZY ADD algorithm ===" << std::endl;
                OptimizationConfig config;
                config.use_incremental = true;
                config.use_trivial_checks = true;
                config.use_lazy_add = true;
                config.enable_stats = true;
                FullyOptimizedAlgo algo(grammar, graph, config);
                return algo.solve();
            }
            
            case AlgoType::FULLY_OPTIMIZED: {
                std::cout << "=== Using FULLY OPTIMIZED algorithm ===" << std::endl;
                OptimizationConfig config = OptimizationConfig::all();
                config.enable_stats = true;
                FullyOptimizedAlgo algo(grammar, graph, config);
                return algo.solve();
            }
            
            case AlgoType::AUTO:
            default: {
                std::cout << "=== Using AUTO algorithm selection ===" << std::endl;
                FullyOptimizedAlgo algo(grammar, graph);
                return algo.solve_auto();
            }
        }
    }
    
    // Бенчмарк всех версий
    static void benchmark_all(const std::string& grammar_path,
                             const std::string& graph_path) {
        std::cout << "\n========================================" << std::endl;
        std::cout << "BENCHMARKING ALL ALGORITHM VERSIONS" << std::endl;
        std::cout << "========================================\n" << std::endl;
        
        cnf_grammar grammar(grammar_path);
        label_decomposed_graph graph(graph_path);
        
        std::vector<std::pair<std::string, AlgoType>> versions = {
            {"BASE (Fixed, O(n⁵))", AlgoType::BASE},
            {"INCREMENTAL (O(n⁴))", AlgoType::INCREMENTAL},
            {"INCREMENTAL + TRIVIAL", AlgoType::TRIVIAL_OPT},
            {"INCREMENTAL + LAZY ADD (O(n³))", AlgoType::LAZY_ADD},
            {"FULLY OPTIMIZED", AlgoType::FULLY_OPTIMIZED},
        };
        
        std::cout << "Input: n = " << graph.matrix_size 
                  << " vertices, " << grammar.complex_rules_.size() 
                  << " rules\n" << std::endl;
        
        struct BenchResult {
            std::string name;
            double time_seconds;
            cuBool_Index result_size;
            bool success;
        };
        
        std::vector<BenchResult> results;
        
        for (const auto& [name, type] : versions) {
            std::cout << "\n========================================" << std::endl;
            std::cout << "Testing: " << name << std::endl;
            std::cout << "========================================" << std::endl;
            
            BenchResult result;
            result.name = name;
            
            try {
                auto start = std::chrono::high_resolution_clock::now();
                cuBool_Matrix matrix = solve(grammar, graph, type);
                auto end = std::chrono::high_resolution_clock::now();
                
                std::chrono::duration<double> elapsed = end - start;
                result.time_seconds = elapsed.count();
                
                cuBool_Matrix_Nvals(matrix, &result.result_size);
                result.success = true;
                
                cuBool_Matrix_Free(matrix);
                
                std::cout << "\n✓ Completed successfully" << std::endl;
            } catch (const std::exception& e) {
                result.success = false;
                result.time_seconds = -1.0;
                result.result_size = 0;
                std::cout << "\n✗ Failed: " << e.what() << std::endl;
            }
            
            results.push_back(result);
        }
        
        // Итоговая таблица
        std::cout << "\n\n========================================" << std::endl;
        std::cout << "BENCHMARK RESULTS SUMMARY" << std::endl;
        std::cout << "========================================\n" << std::endl;
        
        std::cout << std::left << std::setw(35) << "Algorithm" 
                  << std::right << std::setw(12) << "Time (s)" 
                  << std::setw(15) << "Result Size" 
                  << std::setw(10) << "Status" << std::endl;
        std::cout << std::string(72, '-') << std::endl;
        
        for (const auto& r : results) {
            std::cout << std::left << std::setw(35) << r.name;
            
            if (r.success) {
                std::cout << std::right << std::setw(12) << std::fixed 
                          << std::setprecision(6) << r.time_seconds
                          << std::setw(15) << r.result_size
                          << std::setw(10) << "OK";
            } else {
                std::cout << std::right << std::setw(12) << "N/A"
                          << std::setw(15) << "N/A"
                          << std::setw(10) << "FAILED";
            }
            std::cout << std::endl;
        }
        
        // Вычисляем ускорение
        if (results.size() >= 2 && results[0].success && results.back().success) {
            double speedup = results[0].time_seconds / results.back().time_seconds;
            std::cout << "\nSpeedup (BASE → FULLY OPTIMIZED): " 
                      << std::fixed << std::setprecision(2) 
                      << speedup << "x" << std::endl;
        }
        
        std::cout << "\n========================================\n" << std::endl;
    }
    
    // Вспомогательная функция для выбора оптимального алгоритма
    static AlgoType recommend_algo(size_t n, size_t num_rules) {
        if (n < 1000) {
            return AlgoType::BASE;
        } else if (n < 5000) {
            return AlgoType::TRIVIAL_OPT;
        } else if (n < 20000) {
            return AlgoType::LAZY_ADD;
        } else {
            return AlgoType::FULLY_OPTIMIZED;
        }
    }
    
    static std::string algo_type_to_string(AlgoType type) {
        switch (type) {
            case AlgoType::BASE: return "BASE";
            case AlgoType::INCREMENTAL: return "INCREMENTAL";
            case AlgoType::TRIVIAL_OPT: return "TRIVIAL_OPT";
            case AlgoType::LAZY_ADD: return "LAZY_ADD";
            case AlgoType::FULLY_OPTIMIZED: return "FULLY_OPTIMIZED";
            case AlgoType::AUTO: return "AUTO";
            default: return "UNKNOWN";
        }
    }
    
    static AlgoType string_to_algo_type(const std::string& str) {
        if (str == "base") return AlgoType::BASE;
        if (str == "incremental") return AlgoType::INCREMENTAL;
        if (str == "trivial") return AlgoType::TRIVIAL_OPT;
        if (str == "lazy") return AlgoType::LAZY_ADD;
        if (str == "full" || str == "fully_optimized") return AlgoType::FULLY_OPTIMIZED;
        if (str == "auto") return AlgoType::AUTO;
        
        // Legacy aliases для обратной совместимости
        if (str == "diagnostic") {
            std::cout << "Note: 'diagnostic' is deprecated, using 'base' instead" << std::endl;
            return AlgoType::BASE;
        }
        if (str == "template") {
            std::cout << "Note: 'template' is deprecated, using 'full' instead" << std::endl;
            return AlgoType::FULLY_OPTIMIZED;
        }
        
        std::cerr << "Unknown algorithm type: " << str << std::endl;
        std::cerr << "Using AUTO instead." << std::endl;
        return AlgoType::AUTO;
    }
};