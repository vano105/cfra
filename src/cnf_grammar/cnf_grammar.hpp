#pragma once
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <ranges>
#include <set>
#include <stdexcept>
#include <string>
#include <tuple>
#include <vector>

class cnf_grammar {
public:
  struct symbol {
  public:
    std::string label_{};
    bool is_indexed_ = false;

    symbol(const std::string &label) : label_(label) {
      if (label_.size() >= 2)
        is_indexed_ = (label_[label_.size() - 1] == 'i') &&
                      (label_[label_.size() - 2] == '_');
    }

    symbol() {}

    ~symbol() {}

    bool operator==(const symbol &other) { return label_ == other.label_; }

    bool operator<(const symbol &other) const { return label_ < other.label_; }

    operator std::string() const { return label_; }

    int hash() { return std::hash<std::string>{}(label_); }
  };

  symbol start_nonterm_{};
  std::set<symbol> non_terminals;
  std::set<symbol> terminals;
  std::vector<symbol> epsilon_rules_;
  std::vector<std::tuple<symbol, symbol>> simple_rules_;
  std::vector<std::tuple<symbol, symbol, symbol>> complex_rules_;

  cnf_grammar() {}

  cnf_grammar(const cnf_grammar &other)
      : start_nonterm_(other.start_nonterm_),
        epsilon_rules_(other.epsilon_rules_),
        simple_rules_(other.simple_rules_),
        complex_rules_(other.complex_rules_) {}

  cnf_grammar(
      const symbol &start_nonterm, const std::vector<symbol> &epsilon_rules,
      const std::vector<std::tuple<symbol, symbol>> &simple_rules,
      const std::vector<std::tuple<symbol, symbol, symbol>> &complex_rules)
      : start_nonterm_(start_nonterm), epsilon_rules_(epsilon_rules),
        simple_rules_(simple_rules), complex_rules_(complex_rules) {}

  // load from pocr cnf file
  cnf_grammar(const std::string &path) {
    std::ifstream infile(path);
    std::string line;
    auto strip = [](const std::string &str) {
      std::string new_str;

      for (auto symb : str)
        if (symb != ' ')
          new_str += symb;
      return new_str;
    };
    auto split = [](std::string &str, const std::string &sep) {
      std::vector<std::string> result;
      size_t pos = 0;
      std::string token;

      while ((pos = str.find(sep)) != std::string::npos) {
        token = sep.substr(0, pos);
        result.push_back(token);
        str.erase(0, pos + sep.length());
      }
      result.push_back(str);

      return result;
    };

    if (infile.is_open()) {
      while (std::getline(infile, line)) {
        if (line == strip(std::string("Count:"))) {
          std::getline(infile, line);
          start_nonterm_ = symbol(line);
          break;
        }

        auto words = line | std::views::split(' ');
        std::vector<std::string> parts;
        for (const auto &word_range : words) {
          std::string word(word_range.begin(), word_range.end());
          parts.push_back(word);
        }
        if (parts.size() == 1) {
          non_terminals.insert(parts[0]);
          terminals.insert(parts[0]);
          epsilon_rules_.push_back(symbol(parts[0]));
        } else if (parts.size() == 2) {
          non_terminals.insert(parts[0]);
          terminals.insert(parts[0]);
          terminals.insert(parts[1]);
          // epsilon_rules_.push_back(symbol(parts[0]));
          simple_rules_.push_back(
              std::tuple(symbol(parts[0]), symbol(parts[1])));
        } else if (parts.size() == 3) {
          non_terminals.insert(parts[0]);
          terminals.insert(parts[0]);
          terminals.insert(parts[1]);
          terminals.insert(parts[2]);
          complex_rules_.push_back(
              std::tuple(symbol(parts[0]), symbol(parts[1]), symbol(parts[2])));
        } else {
          std::cout << parts.size() << std::endl;
          for (auto &x : parts) {
            std::cout << x << std::endl;
          }
        }
      }

      infile.close();
    } else {
      std::cerr << "Unable to open file: " << path << std::endl;
    }
  }

  cnf_grammar &operator=(const cnf_grammar &other) = default;

  /*
  std::set<symbol> non_terminals() {
    std::set<symbol> epsilon_rules(epsilon_rules_.cbegin(),
                                   epsilon_rules_.cend());
    std::set<std::tuple<symbol, symbol>> simple_rules(simple_rules_.cbegin(),
                                                      simple_rules_.cend());
    std::set<std::tuple<symbol, symbol, symbol>> complex_rules(
        complex_rules_.cbegin(), complex_rules_.cend());
    std::set<symbol> result_set(epsilon_rules_.cbegin(), epsilon_rules_.cend());

    for (const auto &rule : simple_rules_)
      result_set.insert(std::get<0>(rule));
    for (const auto &rule : complex_rules_)
      result_set.insert(std::get<0>(rule));

    return result_set;
  }
  */

  std::set<symbol> symbols() {
    std::set<symbol> epsilon_rules(epsilon_rules_.cbegin(),
                                   epsilon_rules_.cend());
    std::set<std::tuple<symbol, symbol>> simple_rules(simple_rules_.cbegin(),
                                                      simple_rules_.cend());
    std::set<std::tuple<symbol, symbol, symbol>> complex_rules(
        complex_rules_.cbegin(), complex_rules_.cend());
    std::set<symbol> result_set(epsilon_rules_.cbegin(), epsilon_rules_.cend());

    for (const auto &rule : simple_rules_) {
      result_set.insert(std::get<0>(rule));
      result_set.insert(std::get<1>(rule));
    }
    for (const auto &rule : complex_rules_) {
      result_set.insert(std::get<0>(rule));
      result_set.insert(std::get<1>(rule));
      result_set.insert(std::get<2>(rule));
    }

    return result_set;
  }

  ~cnf_grammar() {}
};