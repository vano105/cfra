#include "base_algo/base_matrix_algo.hpp"
#include <iostream>

template <typename T, typename... Args> void error(T first, Args... args) {
  std::cout << first << std::endl;
  if constexpr (sizeof...(Args) != 0)
    error(args...);
}

int main() {
  /*cuBool_Initialize(CUBOOL_HINT_NO);
  matrix_base_algo algo("../test_data/binary_tree/grammar.cnf",
                        "../test_data/binary_tree/graph.txt");
  cuBool_Matrix result = algo.solve();
  std::cout << __LINE__ << std::endl;
  cuBool_Index tc_rows[algo.matrix_size], tc_cols[algo.matrix_size];
  std::cout << __LINE__ << std::endl;
  cuBool_Index nvals;
  std::cout << __LINE__ << std::endl;
  //cuBool_Matrix_New(&result, 5, 5);
  //cuBool_Matrix_Build(result, nullptr, nullptr, 0, CUBOOL_HINT_NO);
  cuBool_Matrix_Nvals(result, &nvals);
  std::cout << __LINE__ << std::endl;
  cuBool_Matrix_ExtractPairs(result, tc_rows, tc_cols, &nvals);
  std::cout << __LINE__ << std::endl;
  std::cout << "Solve:" << std::endl;
  for (cuBool_Index i = 0; i < nvals; i++)
    printf("(%u,%u)\n", tc_rows[i], tc_cols[i]);
  std::cout << __LINE__ << std::endl;
  cuBool_Matrix_Free(result);
  return cuBool_Finalize() != CUBOOL_STATUS_SUCCESS;*/
  cuBool_Initialize(CUBOOL_HINT_NO);
  cuBool_Matrix result1, result2, result3;
  cuBool_Matrix_New(&result1, 6, 6);
  cuBool_Matrix_New(&result2, 6, 6);
  cuBool_Matrix_New(&result3, 6, 6);

  std::vector<cuBool_Index> rows1 {};
  std::vector<cuBool_Index> cols1 {};
  for (int i = 0; i < 6; i++) {
    rows1.push_back(i);
    cols1.push_back(i);
  }
  cuBool_Matrix_Build(result1, rows1.data(), cols1.data(), 6, CUBOOL_HINT_NO);

  std::vector<cuBool_Index> rows2 {0, 1, 2, 3, 4};
  std::vector<cuBool_Index> cols2 {1, 2, 3, 4, 5};
  cuBool_Matrix_Build(result2, rows2.data(), cols2.data(), 5, CUBOOL_HINT_NO);

  cuBool_Matrix_EWiseAdd(result3, result1, result2, CUBOOL_HINT_NO);

  cuBool_Index tc_rows[6], tc_cols[6];
  cuBool_Index nvals;
  cuBool_Matrix_Nvals(result3, &nvals);
  cuBool_Matrix_ExtractPairs(result3, tc_rows, tc_cols, &nvals);
  for (cuBool_Index i = 0; i < nvals; i++)
    printf("(%u,%u)\n", tc_rows[i], tc_cols[i]);

  cuBool_Matrix_Free(result1);
  cuBool_Matrix_Free(result2);
  cuBool_Matrix_Free(result3);
  return cuBool_Finalize() != CUBOOL_STATUS_SUCCESS;
}
