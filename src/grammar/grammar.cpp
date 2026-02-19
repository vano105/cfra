#include "grammar.hpp"

#include <fstream>
#include <sstream>
#include <stdexcept>
#include <iostream>

static std::vector<std::string> split(const std::string &line) {
    std::vector<std::string> tokens;
    std::istringstream iss(line);
    std::string tok;
    while (iss >> tok) tokens.push_back(tok);
    return tokens;
}

static bool is_template(const std::string &s) {
    return s.size() >= 2 && s.substr(s.size() - 2) == "_i";
}

static std::string instantiate(const std::string& symbol, int index) {
    return symbol.substr(0, symbol.size() - 1) + std::to_string(index);
}

TemplateGrammar TemplateGrammar::load(const std::string &path) {
    TemplateGrammar g;
        std::ifstream fin(path);
    if (!fin.is_open())
        throw std::runtime_error("Не удалось открыть файл грамматики: " + path);

    std::string line;
    bool reading_footer = false;

    while (std::getline(fin, line)) {
        while (!line.empty() && (line.back() == '\r' || line.back() == '\n'))
            line.pop_back();
        if (line.empty()) continue;

        if (line == "Count:" || line == "Count:\r") {
            reading_footer = true;
            continue;
        }

        auto tokens = split(line);
        if (tokens.empty()) continue;

        if (reading_footer) {
            if (g.start_symbol_.empty())
                g.start_symbol_ = tokens[0];
            continue;
        }

        if (tokens.size() == 1) {
            g.epsilon_rules_.push_back(tokens[0]);
        } else if (tokens.size() == 2) {
            g.two_token_rules_.emplace_back(tokens[0], tokens[1]);
        } else if (tokens.size() == 3) {
            g.complex_rules_.emplace_back(tokens[0], tokens[1], tokens[2]);
        } else {
            std::cerr << "Предупреждение: пропускаем строку с "
                      << tokens.size() << " токенами: " << line << "\n";
        }
    }

    if (g.start_symbol_.empty() && !g.epsilon_rules_.empty())
        g.start_symbol_ = g.epsilon_rules_[0];

    return g;
}

CnfGrammar CnfGrammar::expand(const TemplateGrammar& tmpl,
                               const std::set<std::string>& graph_labels)
{
    CnfGrammar result;
    result.start_symbol_ = tmpl.start_symbol();

    // --- Шаг 1: собрать все шаблонные символы из грамматики ---
    std::set<std::string> template_syms;
    for (auto& sym : tmpl.epsilon_rules())
        if (is_template(sym)) template_syms.insert(sym);
    for (auto& [a, x] : tmpl.two_token_rules()) {
        if (is_template(a)) template_syms.insert(a);
        if (is_template(x)) template_syms.insert(x);
    }
    for (auto& [a, b, c] : tmpl.complex_rules()) {
        if (is_template(a)) template_syms.insert(a);
        if (is_template(b)) template_syms.insert(b);
        if (is_template(c)) template_syms.insert(c);
    }

    // --- Шаг 2: найти шаблонные терминалы ---
    // Шаблонный терминал — шаблонный символ, чья база встречается
    // как префикс метки графа. Например: "load_i" → "load_i_409".
    std::set<std::string> template_terms;
    for (auto& ts : template_syms) {
        std::string prefix = ts.substr(0, ts.size() - 1);  // "load_i" → "load_i"... нет, "load_i" - 1 char = "load_"
        // ts заканчивается на "_i", нам нужен префикс ts + "_" без последнего "i"
        // Проще: ищем метки вида ts + "_" + число
        // ts = "load_i", prefix для поиска = "load_i_"
        std::string search_prefix = ts + "_";
        for (auto& label : graph_labels) {
            if (label.size() > search_prefix.size() &&
                label.substr(0, search_prefix.size()) == search_prefix) {
                template_terms.insert(ts);
                break;
            }
        }
    }

    // --- Шаг 3: собрать все уникальные индексы ---
    std::set<int> indices;
    for (auto& label : graph_labels) {
        for (auto& tt : template_terms) {
            std::string prefix = tt + "_";
            if (label.size() > prefix.size() &&
                label.substr(0, prefix.size()) == prefix) {
                std::string idx_str = label.substr(prefix.size());
                try {
                    int idx = std::stoi(idx_str);
                    if (std::to_string(idx) == idx_str)
                        indices.insert(idx);
                } catch (...) {}
            }
        }
    }

    std::cout << "Раскрытие шаблонов: " << template_syms.size() << " шаблонных символов, "
              << template_terms.size() << " шаблонных терминалов, "
              << indices.size() << " уникальных индексов\n";

    // --- Шаг 4: раскрыть правила ---

    // Лямбда: подставить индекс в символ (если шаблонный)
    auto expand_sym = [](const std::string& sym, int idx) -> std::string {
        return is_template(sym) ? instantiate(sym, idx) : sym;
    };

    // Лямбда: есть ли в правиле хотя бы один шаблонный символ?
    auto has_template = [](const auto&... syms) -> bool {
        return (is_template(syms) || ...);
    };

    // Раскрытие ε-правил
    for (auto& sym : tmpl.epsilon_rules()) {
        if (is_template(sym)) {
            for (int idx : indices)
                result.epsilon_rules_.push_back(instantiate(sym, idx));
        } else {
            result.epsilon_rules_.push_back(sym);
        }
    }

    // Раскрытие двухтокенных правил (классификация: терминальное или цепное)
    for (auto& [a, x] : tmpl.two_token_rules()) {
        if (has_template(a, x)) {
            for (int idx : indices) {
                auto ea = expand_sym(a, idx);
                auto ex = expand_sym(x, idx);
                if (graph_labels.count(ex))
                    result.terminal_rules_.emplace_back(ea, ex);
                else
                    result.simple_rules_.emplace_back(ea, ex);
            }
        } else {
            if (graph_labels.count(x))
                result.terminal_rules_.emplace_back(a, x);
            else
                result.simple_rules_.emplace_back(a, x);
        }
    }

    // Раскрытие комплексных правил
    for (auto& [a, b, c] : tmpl.complex_rules()) {
        if (has_template(a, b, c)) {
            for (int idx : indices) {
                result.complex_rules_.emplace_back(
                    expand_sym(a, idx),
                    expand_sym(b, idx),
                    expand_sym(c, idx));
            }
        } else {
            result.complex_rules_.emplace_back(a, b, c);
        }
    }

    // --- Шаг 5: собрать все нетерминальные символы ---
    for (auto& s : result.epsilon_rules_) result.nonterminals_.insert(s);
    for (auto& [a, _] : result.terminal_rules_) result.nonterminals_.insert(a);
    for (auto& [a, _] : result.simple_rules_) result.nonterminals_.insert(a);
    for (auto& [a, b, c] : result.complex_rules_) {
        result.nonterminals_.insert(a);
        if (!graph_labels.count(b)) result.nonterminals_.insert(b);
        if (!graph_labels.count(c)) result.nonterminals_.insert(c);
    }

    std::cout << "Раскрытая грамматика: "
              << result.epsilon_rules_.size() << " ε, "
              << result.terminal_rules_.size() << " терминальных, "
              << result.simple_rules_.size() << " цепных, "
              << result.complex_rules_.size() << " комплексных правил, "
              << result.nonterminals_.size() << " нетерминалов\n";

    return result;
}