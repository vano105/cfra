#pragma once

#include <string>

/**
 * Конфигурация оптимизаций для матричного алгоритма КС-достижимости
 * 
 * Позволяет включать/отключать каждую оптимизацию независимо
 */
struct OptimizationConfig {
    // Инкрементальные вычисления (3.1): O(n⁵) → O(n⁴)
    bool use_incremental = false;
    
    // Проверка тривиальных операций (3.3): пропуск пустых матриц
    bool use_trivial_checks = false;
    
    // Динамический выбор формата (3.4): row/column optimization
    // В cuBool менее критично, но полезно для отслеживания метрик
    bool use_format_optimization = false;
    
    // Отложенное сложение (3.5): O(n⁴) → O(n³)
    bool use_lazy_add = false;
    
    // Шаблоны продукций (3.7): блочные матрицы для индексированных грамматик
    bool use_templates = false;
    
    // Переписывание грамматик (3.8): эквивалентные преобразования
    bool use_grammar_rewriting = false;
    
    // Параметр b для отложенного сложения
    // Рекомендуется: b = n^C₁ где C₁ ∈ (0, 1]
    // По умолчанию: b = n^0.5 = sqrt(n)
    double lazy_add_exponent = 0.5;
    
    // Логирование статистики
    bool enable_stats = false;
    
    // Автоматический выбор оптимизаций на основе размера входа
    static OptimizationConfig automatic(size_t n, size_t num_rules) {
        OptimizationConfig config;
        
        // Для маленьких графов (<1000 вершин) оптимизации не нужны
        if (n < 1000) {
            return config; // все false
        }
        
        // Для средних графов (1000-10000)
        if (n < 10000) {
            config.use_incremental = true;
            config.use_trivial_checks = true;
            return config;
        }
        
        // Для больших графов (>10000) включаем всё
        config.use_incremental = true;
        config.use_trivial_checks = true;
        config.use_lazy_add = true;
        config.use_format_optimization = true;
        
        // Шаблоны только если грамматика большая
        if (num_rules > 100) {
            config.use_templates = true;
        }
        
        return config;
    }
    
    // Предустановленные конфигурации
    static OptimizationConfig none() {
        return OptimizationConfig();
    }
    
    static OptimizationConfig all() {
        OptimizationConfig config;
        config.use_incremental = true;
        config.use_trivial_checks = true;
        config.use_format_optimization = true;
        config.use_lazy_add = true;
        config.use_templates = true;
        config.use_grammar_rewriting = true;
        return config;
    }
    
    std::string to_string() const {
        std::string result = "Optimizations: ";
        if (!use_incremental && !use_trivial_checks && !use_lazy_add && 
            !use_format_optimization && !use_templates && !use_grammar_rewriting) {
            return result + "NONE";
        }
        
        std::vector<std::string> enabled;
        if (use_incremental) enabled.push_back("Incremental");
        if (use_trivial_checks) enabled.push_back("TrivialChecks");
        if (use_format_optimization) enabled.push_back("FormatOpt");
        if (use_lazy_add) enabled.push_back("LazyAdd");
        if (use_templates) enabled.push_back("Templates");
        if (use_grammar_rewriting) enabled.push_back("GrammarRewrite");
        
        for (size_t i = 0; i < enabled.size(); ++i) {
            result += enabled[i];
            if (i < enabled.size() - 1) result += ", ";
        }
        
        return result;
    }
};

/**
 * Статистика выполнения алгоритма
 */
struct AlgoStats {
    int iterations = 0;
    size_t total_multiplications = 0;
    size_t skipped_multiplications = 0;
    size_t lazy_additions = 0;
    size_t concrete_additions = 0;
    double total_time_seconds = 0.0;
    
    void print() const {
        std::cout << "\n=== Algorithm Statistics ===" << std::endl;
        std::cout << "Iterations: " << iterations << std::endl;
        std::cout << "Total multiplications: " << total_multiplications << std::endl;
        if (skipped_multiplications > 0) {
            std::cout << "Skipped (trivial): " << skipped_multiplications 
                      << " (" << (100.0 * skipped_multiplications / (total_multiplications + skipped_multiplications)) 
                      << "%)" << std::endl;
        }
        if (lazy_additions > 0) {
            std::cout << "Lazy additions: " << lazy_additions << std::endl;
            std::cout << "Concrete additions: " << concrete_additions << std::endl;
        }
        std::cout << "Total time: " << total_time_seconds << " seconds" << std::endl;
    }
};