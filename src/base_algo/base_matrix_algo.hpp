#pragma once

#include "../cnf_grammar/cnf_grammar.hpp"
#include "../label_decomposed_graph/label_decomposed_graph.hpp"
#include "cubool.h"
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

  cuBool_Matrix solve() {
    label_decomposed_graph m(Graph.matrix_size);

    for (const std::tuple<symbol, symbol> &pair : Grammar.simple_rules_) {
      cuBool_Matrix result;
      cuBool_Matrix_New(&result, Graph.matrix_size, Graph.matrix_size);
      cuBool_Matrix_EWiseAdd(result, m[std::get<0>(pair)], Graph[std::get<1>(pair)],  CUBOOL_HINT_NO);
      m[std::get<0>(pair)] = result;
    }
    bool changed = true;
    while (changed) {
      changed = false;
      for (const std::tuple<symbol, symbol, symbol> &triple :
           Grammar.complex_rules_) {
        cuBool_Index old_nvals;
        cuBool_Matrix_Nvals(m[std::get<0>(triple)], &old_nvals);
        cuBool_Matrix result;
        cuBool_Matrix_New(&result, Graph.matrix_size, Graph.matrix_size);
        cuBool_MxM(result, m[std::get<1>(triple)], m[std::get<2>(triple)], CUBOOL_HINT_NO);
        cuBool_Matrix result2;
        cuBool_Matrix_New(&result2, Graph.matrix_size, Graph.matrix_size);
        cuBool_Matrix_EWiseAdd(result2, m[std::get<0>(triple)], result, CUBOOL_HINT_NO);
        m[std::get<0>(triple)] = result2;
        cuBool_Index new_nvals;
        cuBool_Matrix_Nvals(m[std::get<0>(triple)], &new_nvals);
        changed |= !(old_nvals == new_nvals);
      }
    }
    return m[Grammar.start_nonterm_];
  }
  ~matrix_base_algo() {}
};
