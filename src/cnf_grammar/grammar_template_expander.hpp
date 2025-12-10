#pragma once

#include <regex>
#include <set>
#include <string>
#include <vector>
#include <map>
#include <fstream>
#include <sstream>
#include <iostream>

// Расширитель шаблонов грамматики
class GrammarTemplateExpander {
private:
    // Регулярное выражение для поиска шаблонных переменных _i в конце
    static std::regex template_pattern;
    
    // Извлечь индекс из конкретного символа (например: store_i_698 -> 698)
    static bool extract_index(const std::string& symbol, int& index) {
        std::regex index_pattern("_i_(\\d+)$");
        std::smatch match;
        
        if (std::regex_search(symbol, match, index_pattern)) {
            index = std::stoi(match[1].str());
            return true;
        }
        return false;
    }
    
    // Проверить, является ли символ шаблонным (заканчивается на _i без числа)
    static bool is_template_symbol(const std::string& symbol) {
        // Заканчивается на _i, но НЕ на _i_<число>
        if (symbol.length() < 2) return false;
        if (symbol.substr(symbol.length() - 2) != "_i") return false;
        
        // Проверяем, что перед _i нет цифры (т.е. это не _i_123)
        std::regex concrete_pattern("_i_\\d+$");
        return !std::regex_search(symbol, concrete_pattern);
    }
    
    // Заменить _i на конкретный индекс
    static std::string instantiate_template(const std::string& template_symbol, int index) {
        if (is_template_symbol(template_symbol)) {
            // Заменяем _i на _i_<index>
            return template_symbol.substr(0, template_symbol.length() - 2) + "_i_" + std::to_string(index);
        }
        return template_symbol;
    }

public:
    // Собрать все индексы из графа
    static std::set<int> collect_indices_from_graph(const std::string& graph_path) {
        std::set<int> indices;
        std::ifstream file(graph_path);
        
        if (!file) {
            std::cerr << "Warning: Cannot open graph file: " << graph_path << std::endl;
            return indices;
        }
        
        std::string line;
        while (std::getline(file, line)) {
            if (line.empty() || line[0] == '#') continue;
            
            std::istringstream iss(line);
            int from, to;
            std::string label;
            
            if (iss >> from >> to >> label) {
                int index;
                if (extract_index(label, index)) {
                    indices.insert(index);
                }
            }
        }
        
        file.close();
        
        std::cout << "Found " << indices.size() << " unique indices in graph: ";
        int count = 0;
        for (int idx : indices) {
            if (count++ < 10) std::cout << idx << " ";
        }
        if (indices.size() > 10) std::cout << "...";
        std::cout << std::endl;
        
        return indices;
    }
    
    // Раскрыть шаблонную грамматику для всех индексов
    static void expand_grammar_template(
        const std::string& template_grammar_path,
        const std::string& graph_path,
        const std::string& output_grammar_path) {
        
        // 1. Собираем индексы из графа
        std::set<int> indices = collect_indices_from_graph(graph_path);
        
        if (indices.empty()) {
            std::cout << "No indices found in graph. Grammar will not be expanded." << std::endl;
            // Просто копируем исходную грамматику
            std::ifstream src(template_grammar_path);
            std::ofstream dst(output_grammar_path);
            dst << src.rdbuf();
            return;
        }
        
        // 2. Читаем шаблонную грамматику
        std::ifstream input_file(template_grammar_path);
        if (!input_file) {
            std::cerr << "Error: Cannot open template grammar: " << template_grammar_path << std::endl;
            return;
        }
        
        std::vector<std::string> epsilon_rules;
        std::vector<std::tuple<std::string, std::string>> simple_rules;
        std::vector<std::tuple<std::string, std::string, std::string>> complex_rules;
        std::string start_symbol;
        
        std::string line;
        bool reading_start = false;
        
        while (std::getline(input_file, line)) {
            if (line.empty() || line[0] == '#') continue;
            
            if (line == "Count:") {
                reading_start = true;
                continue;
            }
            
            if (reading_start) {
                start_symbol = line;
                break;
            }
            
            std::istringstream iss(line);
            std::string first, second, third;
            iss >> first >> second >> third;
            
            if (third.empty()) {
                // Простое правило: A -> b или эпсилон
                if (second.empty()) {
                    epsilon_rules.push_back(first);
                } else {
                    simple_rules.push_back({first, second});
                }
            } else {
                // Сложное правило: A -> B C
                complex_rules.push_back({first, second, third});
            }
        }
        input_file.close();
        
        // 3. Раскрываем грамматику
        std::ofstream output_file(output_grammar_path);
        
        int expanded_count = 0;
        
        // Раскрываем сложные правила
        for (const auto& [lhs, rhs1, rhs2] : complex_rules) {
            bool is_template = is_template_symbol(lhs) || 
                             is_template_symbol(rhs1) || 
                             is_template_symbol(rhs2);
            
            if (is_template) {
                // Раскрываем для каждого индекса
                for (int idx : indices) {
                    std::string exp_lhs = instantiate_template(lhs, idx);
                    std::string exp_rhs1 = instantiate_template(rhs1, idx);
                    std::string exp_rhs2 = instantiate_template(rhs2, idx);
                    
                    output_file << exp_lhs << " " << exp_rhs1 << " " << exp_rhs2 << "\n";
                    expanded_count++;
                }
            } else {
                // Не шаблонное правило - копируем как есть
                output_file << lhs << " " << rhs1 << " " << rhs2 << "\n";
            }
        }
        
        // Раскрываем простые правила
        for (const auto& [lhs, rhs] : simple_rules) {
            bool is_template = is_template_symbol(lhs) || is_template_symbol(rhs);
            
            if (is_template) {
                for (int idx : indices) {
                    std::string exp_lhs = instantiate_template(lhs, idx);
                    std::string exp_rhs = instantiate_template(rhs, idx);
                    
                    output_file << exp_lhs << " " << exp_rhs << "\n";
                    expanded_count++;
                }
            } else {
                output_file << lhs << " " << rhs << "\n";
            }
        }
        
        // Эпсилон-правила (обычно не шаблонные)
        for (const auto& eps : epsilon_rules) {
            bool is_template = is_template_symbol(eps);
            
            if (is_template) {
                for (int idx : indices) {
                    std::string exp_eps = instantiate_template(eps, idx);
                    output_file << exp_eps << "\n";
                    expanded_count++;
                }
            } else {
                output_file << eps << "\n";
            }
        }
        
        // Стартовый символ
        output_file << "Count:\n";
        output_file << start_symbol << "\n";
        
        output_file.close();
        
        std::cout << "Grammar expanded successfully!" << std::endl;
        std::cout << "  Template rules expanded: " << expanded_count << std::endl;
        std::cout << "  Output written to: " << output_grammar_path << std::endl;
    }
    
    // Вспомогательная функция: проверить, нужно ли раскрытие
    static bool needs_expansion(const std::string& grammar_path) {
        std::ifstream file(grammar_path);
        if (!file) return false;
        
        std::string line;
        while (std::getline(file, line)) {
            if (line.empty() || line[0] == '#') continue;
            if (line == "Count:") break;
            
            std::istringstream iss(line);
            std::string first, second, third;
            iss >> first >> second >> third;
            
            if (is_template_symbol(first) || 
                is_template_symbol(second) || 
                is_template_symbol(third)) {
                return true;
            }
        }
        
        return false;
    }
    
    // Автоматическое раскрытие с временным файлом
    static std::string auto_expand_if_needed(
        const std::string& grammar_path,
        const std::string& graph_path) {
        
        if (!needs_expansion(grammar_path)) {
            std::cout << "Grammar does not contain templates. Using as-is." << std::endl;
            return grammar_path;
        }
        
        std::cout << "Grammar contains templates. Expanding..." << std::endl;
        
        // Создаем временный файл для раскрытой грамматики
        std::string expanded_path = grammar_path + ".expanded";
        expand_grammar_template(grammar_path, graph_path, expanded_path);
        
        return expanded_path;
    }
};

// Инициализация статического члена
std::regex GrammarTemplateExpander::template_pattern("_i$");