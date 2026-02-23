#pragma once

#include <string>
#include <vector>
#include <tuple>
#include <set>
#include <unordered_map>

class TemplateGrammar {
public:
    static TemplateGrammar load(const std::string& path);

    const std::string& start_symbol() const { return start_symbol_; }

    const std::vector<std::string>& epsilon_rules() const { return epsilon_rules_; }

    const std::vector<std::pair<std::string, std::string>>& two_token_rules() const {
        return two_token_rules_;
    }

    const std::vector<std::tuple<std::string, std::string, std::string>>& complex_rules() const {
        return complex_rules_;
    }

private:
    std::string start_symbol_;
    std::vector<std::string> epsilon_rules_;
    std::vector<std::pair<std::string, std::string>> two_token_rules_;
    std::vector<std::tuple<std::string, std::string, std::string>> complex_rules_;
};

class CnfGrammar {
public:
    static CnfGrammar expand(const TemplateGrammar& tmpl,
                             const std::set<std::string>& graph_labels);

    const std::string& start_symbol() const { return start_symbol_; }

    const std::vector<std::string>& epsilon_rules() const { return epsilon_rules_; }

    const std::vector<std::pair<std::string, std::string>>& terminal_rules() const {
        return terminal_rules_;
    }

    const std::vector<std::pair<std::string, std::string>>& simple_rules() const {
        return simple_rules_;
    }

    const std::vector<std::tuple<std::string, std::string, std::string>>& complex_rules() const {
        return complex_rules_;
    }

    const std::set<std::string>& nonterminals() const { return nonterminals_; }

private:
    std::string start_symbol_;
    std::vector<std::string> epsilon_rules_;
    std::vector<std::pair<std::string, std::string>> terminal_rules_;
    std::vector<std::pair<std::string, std::string>> simple_rules_;
    std::vector<std::tuple<std::string, std::string, std::string>> complex_rules_;
    std::set<std::string> nonterminals_; 
};