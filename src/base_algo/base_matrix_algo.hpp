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
  size_t matrix_size{};

  matrix_base_algo() {}
  matrix_base_algo(const cnf_grammar &grammar,
                   const label_decomposed_graph &graph)
      : Grammar(grammar), Graph(graph) {}

  matrix_base_algo(const std::string &path_to_gramar,
                   const std::string &path_to_graph) {
    Grammar = *(new cnf_grammar(path_to_gramar));
    Graph = *(new label_decomposed_graph(path_to_graph));
    matrix_size = Graph.matrix_size;
  }

  cuBool_Matrix solve() {
    // check resutls
    std::vector<cuBool_Matrix> results;
    std::set<cuBool_Matrix> uniq_results;

    // for epsilone rules
    std::vector<cuBool_Index> rows;
    std::vector<cuBool_Index> cols;
    for (int i = 0; i < matrix_size; i++) {
      rows.push_back(i);
      cols.push_back(i);
    }

    results.emplace_back();
    cuBool_Matrix_New(&results.back(), matrix_size, matrix_size);
    uniq_results.insert(results.back());
    cuBool_Matrix_Build(results.back(), rows.data(), cols.data(), matrix_size,
                        CUBOOL_HINT_NO);
    for (const symbol &left : Grammar.epsilon_rules_) {
      if (Graph.matrices.find(left) == Graph.matrices.end()) {
        cuBool_Matrix_New(&Graph[left], matrix_size, matrix_size);
      }

      cuBool_Matrix_EWiseAdd(Graph[left], results.back(), Graph[left],
                             CUBOOL_HINT_NO);
    }

    // for simple rules
    for (auto &[lhs, rhs] : Grammar.simple_rules_) {
      if (Graph.matrices.find(lhs) == Graph.matrices.end()) {
        cuBool_Matrix_New(&Graph[lhs], matrix_size, matrix_size);
      }
      if (Graph.matrices.find(rhs) == Graph.matrices.end()) {
        cuBool_Matrix_New(&Graph[rhs], matrix_size, matrix_size);
      }
      results.emplace_back();
      cuBool_Matrix_New(&results.back(), matrix_size, matrix_size);
      uniq_results.insert(results.back());

      cuBool_Matrix_EWiseAdd(results.back(), Graph[lhs], Graph[rhs],
                             CUBOOL_HINT_NO);

      Graph[lhs] = results.back();
    }

    // core cycle
    bool changed = true;
    cuBool_Index old_nvals;
    results.emplace_back();
    auto result = results.back();
    cuBool_Matrix_New(&result, matrix_size, matrix_size);
    uniq_results.insert(result);
    results.emplace_back();
    auto result2 = results.back();
    cuBool_Matrix_New(&result2, matrix_size, matrix_size);
    uniq_results.insert(result2);
    while (changed) {
      changed = false;
      for (auto &[lhs, rhs1, rhs2] : Grammar.complex_rules_) {
        if (Graph.matrices.find(lhs) == Graph.matrices.end()) {
          cuBool_Matrix_New(&Graph[lhs], matrix_size, matrix_size);
        }
        if (Graph.matrices.find(rhs1) == Graph.matrices.end()) {
          cuBool_Matrix_New(&Graph[rhs1], matrix_size, matrix_size);
        }
        if (Graph.matrices.find(rhs2) == Graph.matrices.end()) {
          cuBool_Matrix_New(&Graph[rhs2], matrix_size, matrix_size);
        }
        cuBool_Matrix_New(&result, matrix_size, matrix_size);
        cuBool_Matrix_New(&result2, matrix_size, matrix_size);
        cuBool_Matrix_Nvals(Graph[lhs], &old_nvals);
        cuBool_MxM(result, Graph[rhs1], Graph[rhs2], CUBOOL_HINT_NO);

        cuBool_Matrix_EWiseAdd(result2, Graph[lhs], result, CUBOOL_HINT_NO);
        Graph[lhs] = result2;

        cuBool_Index new_nvals;
        cuBool_Matrix_Nvals(Graph[lhs], &new_nvals);
        changed |= !(old_nvals == new_nvals);
      }
    }
    for (auto result : uniq_results)
      if (Graph[Grammar.start_nonterm_] != result)
        cuBool_Matrix_Free(result);

    return Graph[Grammar.start_nonterm_];
  }
  ~matrix_base_algo() {}
};
