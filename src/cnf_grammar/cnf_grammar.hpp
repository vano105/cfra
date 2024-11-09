#pragma once
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
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

  cnf_grammar(const std::string &path) {
    std::ifstream infile(path);
    std::string line;
    auto strip = [](auto str) {
      std::string new_str;

      for (auto symb : str)
        if (symb == ' ')
          new_str.push_back(symb);
      return new_str;
    };
    auto split = [](auto str, std::string sep) {
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
      if (std::getline(infile, line)) {
        this->start_nonterm_ = line.substr(0, line.find(' '));
      }

      std::getline(infile, line);
      while (std::getline(infile, line)) {
        std::string lhs = strip(line.substr(0, line.find("->")));
        std::string rhs = strip(line.substr(line.find("->") + 2));
        std::vector<std::string> rhs_parts = split(rhs, " ");
        if (rhs_parts.size() == 1) {
          epsilon_rules_.push_back(symbol(rhs_parts[0]));
        } else if (rhs_parts.size() == 2) {
          simple_rules_.push_back(
              std::tuple(symbol(rhs_parts[0]), symbol(rhs_parts[1])));
        } else if (rhs_parts.size() == 2) {
          complex_rules_.push_back(std::tuple(symbol(rhs_parts[0]),
                                              symbol(rhs_parts[1]),
                                              symbol(rhs_parts[2])));
        } else {
          std::runtime_error("error in cnf file format");
        }
      }

      infile.close();
    } else {
      std::cerr << "Unable to open file: " << path << std::endl;
    }
  }

  cnf_grammar &operator=(const cnf_grammar &other) = default;

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
