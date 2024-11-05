#pragma once

#include "../cnf_grammar/cnf_grammar.hpp"
#include "../label_decomposed_graph/label_decomposed_graph.hpp"
#include <string>
#include <tuple>

class matrix_base_algo {
private:
  cnf_grammar Grammar;
  label_decomposed_graph Graph;
  using symbol = cnf_grammar::symbol;

public:
  matrix_base_algo() {}
  matrix_base_algo(const cnf_grammar &grammar,
                   const label_decomposed_graph &graph)
      : Grammar(grammar), Graph(graph) {}
  matrix_base_algo(const std::string &path_to_gramar,
                   const std::string &path_to_graph) {
    Grammar = *(new cnf_grammar(path_to_gramar));
    Graph = *(new label_decomposed_graph(path_to_graph));
  }

  void solve() {
    label_decomposed_graph m(Graph.matrix_size);

    for (const std::tuple<symbol, symbol> &pair : Grammar.simple_rules_) {
      m[std::get<0>(pair)].ewiseadd(Graph[std::get<1>(pair)]);
    }
    bool changed = true;
    while (changed) {
      changed = false;
      for (const std::tuple<symbol, symbol, symbol> &triple :
           Grammar.complex_rules_) {
        //cuBool_Index current;
        //CHECK(cuBool_Matrix_Nvals(TC, &current));
        
      }
    }
  }
  ~matrix_base_algo() {}
};
