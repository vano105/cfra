#include "base_algo/base_matrix_algo.hpp"
#include <fstream>
#include <iostream>
#include <vector>

template <typename T, typename... Args> void error(T first, Args... args) {
  std::cout << first << std::endl;
  if constexpr (sizeof...(Args) != 0)
    error(args...);
}

struct Config {
  std::string test_name;
  std::string graph;
  std::string grammar;
  std::string expected;
};

bool run_algo(const Config &config, const std::string &path_to_testdir) {
  cuBool_Initialize(CUBOOL_HINT_NO);

  matrix_base_algo algo(path_to_testdir + config.grammar,
                        path_to_testdir + config.graph);
  cuBool_Matrix result = algo.solve();
  size_t n = algo.matrix_size * algo.matrix_size;
  cuBool_Index tc_rows[n], tc_cols[n];
  cuBool_Index nvals;
  cuBool_Matrix_Nvals(result, &nvals);
  cuBool_Matrix_ExtractPairs(result, tc_rows, tc_cols, &nvals);

  // check resutls
  std::ifstream file(path_to_testdir + config.expected);
  if (!file) {
    std::cout << "Can't open file : " << path_to_testdir + config.expected
              << std::endl;
    return false;
  }
  std::vector<std::pair<int, int>> expected;
  {
    int row, col;
    while (file) {
      file >> row >> col;
      expected.emplace_back(row, col);
    }
  }

  for (int i = 0; i < nvals; i++) {
    if (expected[i].first != tc_rows[i] || expected[i].second != tc_cols[i])
      return false;
  }
  cuBool_Matrix_Free(result);

  cuBool_Finalize();
  return true;
}

bool test(const std::string &path_to_testdir) {
  std::vector<Config> configs{
      {
          .test_name = "an_bn",
          .graph = "an_bn/graph.txt",
          .grammar = "an_bn/grammar.cnf",
          .expected = "an_bn/expected.txt",
      },
      {
          .test_name = "transitive_loop",
          .graph = "transitive_loop/graph.txt",
          .grammar = "transitive_loop/grammar.cnf",
          .expected = "transitive_loop/expected.txt",
      },
  };

  for (const auto &config : configs) {
    if (!run_algo(config, path_to_testdir)) {
      std::cout << "faild test : " << config.test_name << std::endl;
      return false;
    }
  }

  return true;
}

int main() { return test("../test_data/"); }
