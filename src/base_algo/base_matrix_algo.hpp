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
    label_decomposed_graph m(matrix_size);

    std::vector<cuBool_Matrix> results;
    std::set<cuBool_Matrix> uniq_results;
    results.emplace_back();
    cuBool_Matrix_New(&results.back(), matrix_size, matrix_size);
    uniq_results.insert(results.back());

    std::vector<cuBool_Index> rows;
    std::vector<cuBool_Index> cols;
    for (int i = 0; i < matrix_size; i++) {
      rows.push_back(i);
      cols.push_back(i);
    }
    for (const symbol &left : Grammar.epsilon_rules_) {
      cuBool_Matrix_Build(m[left], rows.data(), cols.data(), matrix_size, CUBOOL_HINT_NO);
    }

    results.emplace_back();
    cuBool_Matrix_New(&results.back(), matrix_size, matrix_size);
    uniq_results.insert(results.back());
    for (auto &[lhs, rhs] : Grammar.simple_rules_) {
      cuBool_Matrix_EWiseAdd(results.back(), m[lhs],
                             Graph[rhs], CUBOOL_HINT_NO);
      m[lhs] = results.back();
    }

    cuBool_Index tc_rows[matrix_size], tc_cols[matrix_size];
    cuBool_Index nvals;
    cuBool_Matrix_Nvals(m[Grammar.epsilon_rules_[0].label_], &nvals);
    cuBool_Matrix_ExtractPairs(m[Grammar.epsilon_rules_[0].label_], tc_rows, tc_cols, &nvals);
    for (cuBool_Index i = 0; i < nvals; i++)
      printf("(%u,%u)\n", tc_rows[i], tc_cols[i]);

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
      //for (auto &[label, value] : result_matrix) {
      for (auto &[lhs, rhs1, rhs2] :
           Grammar.complex_rules_) {
        cuBool_Matrix_Nvals(m[lhs], &old_nvals);
        cuBool_MxM(result, m[rhs1], m[rhs2],
                   CUBOOL_HINT_NO);
        cuBool_Matrix_EWiseAdd(result2, m[lhs], result,
                               CUBOOL_HINT_NO);
        m[lhs] = result2;
        cuBool_Index new_nvals;

        cuBool_Matrix_Nvals(m[lhs], &new_nvals);

        changed |= !(old_nvals == new_nvals);
      }
    }
//    for (auto result : uniq_results)
 //     if (m[Grammar.start_nonterm_] != result)
  //      cuBool_Matrix_Free(result);
    
    return m[Grammar.start_nonterm_];
  }
  ~matrix_base_algo() {}
};
