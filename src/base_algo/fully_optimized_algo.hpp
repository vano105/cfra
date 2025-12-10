#pragma once

#include "../cnf_grammar/cnf_grammar.hpp"
#include "../label_decomposed_graph/label_decomposed_graph.hpp"
#include "base_matrix_algo.hpp"
#include "incremental_matrix_algo.hpp"
#include "trivial_optimized_algo.hpp"
#include "lazy_add_optimized_algo.hpp"
#include "template_grammar_optimized_algo.hpp"
#include "diagnostic_base_matrix_algo.hpp"
#include <cubool.h>

// Конфигурация для выбора оптимизаций
struct OptimizationConfig {
    bool use_incremental = true;       // 3.1: Инкрементальные вычисления
    bool use_trivial_checks = true;    // 3.3: Проверка тривиальных операций
    bool use_format_selection = true;  // 3.4: Динамический выбор форматов
    bool use_lazy_add = true;          // 3.5: Отложенное сложение
    bool use_templates = true;         // 3.7: Шаблоны продукций
    
    // Параметры
    double lazy_add_C1 = 0.5;          // Параметр для отложенного сложения
};

// Полностью оптимизированный алгоритм, комбинирующий все оптимизации
class FullyOptimizedAlgo {
private:
    cnf_grammar grammar;
    label_decomposed_graph graph;
    size_t matrix_size;
    OptimizationConfig config;
    
    using symbol = cnf_grammar::symbol;

public:
    FullyOptimizedAlgo(const cnf_grammar& gr, 
                      const label_decomposed_graph& g,
                      const OptimizationConfig& cfg = OptimizationConfig())
        : grammar(gr), graph(g), matrix_size(g.matrix_size), config(cfg) {}
    
    FullyOptimizedAlgo(const std::string& grammar_path, 
                      const std::string& graph_path,
                      const OptimizationConfig& cfg = OptimizationConfig())
        : config(cfg) {
        grammar = cnf_grammar(grammar_path);
        graph = label_decomposed_graph(graph_path);
        matrix_size = graph.matrix_size;
    }
    
    cuBool_Matrix solve() {
        // Выбираем алгоритм в зависимости от конфигурации
        
        if (!config.use_incremental) {
            // Базовый алгоритм без инкрементальных вычислений
            matrix_base_algo algo(grammar, graph);
            return algo.solve();
        }
        
        if (config.use_templates) {
            // С шаблонами (включает инкрементальные вычисления)
            TemplateOptimizedAlgo algo(grammar, graph);
            return algo.solve();
        }
        
        if (config.use_lazy_add) {
            // С отложенным сложением (включает инкрементальные вычисления)
            LazyAddOptimizedAlgo algo(grammar, graph);
            return algo.solve();
        }
        
        if (config.use_trivial_checks) {
            // С проверкой тривиальных операций (включает инкрементальные вычисления)
            TrivialOptimizedAlgo algo(grammar, graph);
            return algo.solve();
        }
        
        // Только инкрементальные вычисления
        IncrementalMatrixAlgo algo(grammar, graph);
        return algo.solve();
    }
    
    // Метод для выбора оптимального алгоритма автоматически
    cuBool_Matrix solve_auto() {
        // Анализируем грамматику и граф
        bool has_templates = !TemplateGrammarAnalyzer::analyze_templates(grammar).empty();
        bool is_large = matrix_size > 10000;
        
        OptimizationConfig auto_config;
        auto_config.use_incremental = true;  // Всегда включаем
        auto_config.use_trivial_checks = true; // Всегда включаем
        auto_config.use_templates = has_templates;
        auto_config.use_lazy_add = is_large;
        
        // Адаптируем параметр C1 в зависимости от размера
        if (is_large) {
            auto_config.lazy_add_C1 = 0.3; // Меньший параметр для больших графов
        }
        
        config = auto_config;
        return solve();
    }
};

// Фабрика для создания оптимизированных алгоритмов
class CFReachabilityAlgoFactory {
public:
    enum class AlgoType {
        BASE,              // Базовый алгоритм Азимова
        DIAGNOSTIC,        // Версия базового алгоритма для диагностики
        INCREMENTAL,       // С инкрементальными вычислениями
        TRIVIAL_OPT,       // + проверка тривиальных операций
        LAZY_ADD,          // + отложенное сложение
        TEMPLATE_OPT,      // + шаблоны продукций
        FULLY_OPTIMIZED,   // Все оптимизации
        AUTO               // Автоматический выбор
    };
    
    static cuBool_Matrix solve(const std::string& grammar_path,
                              const std::string& graph_path,
                              AlgoType type = AlgoType::AUTO) {

        cnf_grammar grammar(grammar_path);
        label_decomposed_graph graph(graph_path);
        
        switch (type) {
            case AlgoType::BASE: {
                matrix_base_algo algo(grammar, graph);
                return algo.solve();
            }
            case AlgoType::DIAGNOSTIC: {
                OptimizedExtendedCNFAlgo algo(grammar, graph);
                return algo.solve();
            }
            case AlgoType::INCREMENTAL: {
                IncrementalMatrixAlgo algo(grammar, graph);
                return algo.solve();
            }
            case AlgoType::TRIVIAL_OPT: {
                TrivialOptimizedAlgo algo(grammar, graph);
                return algo.solve();
            }
            case AlgoType::LAZY_ADD: {
                LazyAddOptimizedAlgo algo(grammar, graph);
                return algo.solve();
            }
            case AlgoType::TEMPLATE_OPT: {
                TemplateOptimizedAlgo algo(grammar, graph);
                return algo.solve();
            }
            case AlgoType::FULLY_OPTIMIZED: {
                OptimizationConfig config;
                config.use_incremental = true;
                config.use_trivial_checks = true;
                config.use_lazy_add = true;
                config.use_templates = true;
                
                FullyOptimizedAlgo algo(grammar, graph, config);
                return algo.solve();
            }
            case AlgoType::AUTO:
            default: {
                FullyOptimizedAlgo algo(grammar, graph);
                return algo.solve_auto();
            }
        }
    }
    
    // Вспомогательный метод для тестирования всех версий
    static void benchmark_all(const std::string& grammar_path,
                             const std::string& graph_path) {
        std::cout << "Testing all algorithm versions:\n" << std::endl;
        
        std::vector<std::pair<std::string, AlgoType>> versions = {
            {"Base (Azimov)", AlgoType::BASE},
            {"Diagnostic algorithm", AlgoType::DIAGNOSTIC},
            {"Incremental", AlgoType::INCREMENTAL},
            {"Trivial Optimized", AlgoType::TRIVIAL_OPT},
            {"Lazy Add", AlgoType::LAZY_ADD},
            {"Template Optimized", AlgoType::TEMPLATE_OPT},
            {"Fully Optimized", AlgoType::FULLY_OPTIMIZED},
            {"Auto", AlgoType::AUTO}
        };
        
        for (const auto& [name, type] : versions) {
            std::cout << "Running: " << name << "..." << std::endl;
            
            auto start = std::chrono::high_resolution_clock::now();
            cuBool_Matrix result = solve(grammar_path, graph_path, type);
            auto end = std::chrono::high_resolution_clock::now();
            
            std::chrono::duration<double> elapsed = end - start;
            
            cuBool_Index nvals;
            cuBool_Matrix_Nvals(result, &nvals);
            
            std::cout << "  Time: " << elapsed.count() << " seconds" << std::endl;
            std::cout << "  Result size: " << nvals << " reachable pairs" << std::endl;
            std::cout << std::endl;
            
            cuBool_Matrix_Free(result);
        }
    }
};