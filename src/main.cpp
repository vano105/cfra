#include "base_algo/base_matrix_algo.hpp"

int main() {
  matrix_base_algo algo("../test_data/binary_tree/grammar.cnf",
                        "../test_data/binary_tree/graph.txt");
  cuBool_Matrix result = algo.solve();
  cuBool_Index tc_rows[algo.matrix_size], tc_cols[algo.matrix_size];
  cuBool_Index nvals;
  cuBool_Matrix_Nvals(result, &nvals);
  cuBool_Matrix_ExtractPairs(result, tc_rows, tc_cols, &nvals);
  for (cuBool_Index i = 0; i < nvals; i++)
    printf("(%u,%u) ", tc_rows[i], tc_cols[i]);
  return 0;
}
