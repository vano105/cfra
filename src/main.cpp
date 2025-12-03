#include "base_algo/base_matrix_algo.hpp"
#include <chrono>
#include <fstream>
#include <iostream>
#include <vector>

#include "cubool.h"

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
  // start timer
  auto start = std::chrono::high_resolution_clock::now();
  cuBool_Matrix result = algo.solve();
  cuBool_Index nvals;
  cuBool_Matrix_Nvals(result, &nvals);
  std::vector<cuBool_Index> tc_rows(nvals), tc_cols(nvals);
  cuBool_Matrix_ExtractPairs(result, tc_rows.data(), tc_cols.data(), &nvals);

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

  auto end = std::chrono::high_resolution_clock::now();
  std::chrono::duration<double> elapsed = end - start;
  std::cout << config.test_name << " time : " << elapsed << std::endl;

  std::ofstream out_file(path_to_testdir + "result.txt");
  std::cout << nvals << std::endl;
  if (nvals == 0 && expected.size() == 0) {
    out_file.close();
    file.close();
    return false;
  }
  bool verify = true;
  for (int i = 0; i < nvals; i++) {
    out_file << tc_rows[i] << ' ' << tc_cols[i] << '\n';
    if (expected[i].first != tc_rows[i] || expected[i].second != tc_cols[i])
      verify = false;
  }
  out_file.close();
  file.close();
  cuBool_Matrix_Free(result);

  cuBool_Finalize();
  return verify;
}

bool test(const std::string &path_to_testdir) {
  std::vector<Config> configs{
    /*
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
      // java graphs
      {
          .test_name = "lusearch",
          .graph = "java/lusearch/lusearch.csv",
          .grammar = "java/lusearch/grammar.cnf",
          .expected = "java/lusearch/expected.txt",
      },
      */
      {
          .test_name = "avrora",
          .graph = "java/avrora/avrora.csv",
          .grammar = "java/avrora/grammar.cnf",
          .expected = "java/avrora/expected.txt",
      },
      {
          .test_name = "tomcat",
          .graph = "java/tomcat/tomcat.csv",
          .grammar = "java/tomcat/grammar.cnf",
          .expected = "java/tomcat/expected.txt",
      },
      {
          .test_name = "eclipse",
          .graph = "java/eclipse/eclipse.csv",
          .grammar = "java/eclipse/grammar.cnf",
          .expected = "java/eclipse/expected.txt",
      },
      {
          .test_name = "h2",
          .graph = "java/h2/h2.csv",
          .grammar = "java/h2/grammar.cnf",
          .expected = "java/h2/expected.txt",
      },
      {
          .test_name = "gson",
          .graph = "java/gson/gson.csv",
          .grammar = "java/gson/grammar.cnf",
          .expected = "java/gson/expected.txt",
      },
      {
          .test_name = "pmd",
          .graph = "java/pmd/pmd.csv",
          .grammar = "java/pmd/grammar.cnf",
          .expected = "java/pmd/expected.txt",
      },
      {
          .test_name = "sunflow",
          .graph = "java/sunflow/sunflow.csv",
          .grammar = "java/sunflow/grammar.cnf",
          .expected = "java/sunflow/expected.txt",
      },
      {
          .test_name = "batik",
          .graph = "java/batik/batik.csv",
          .grammar = "java/batik/grammar.cnf",
          .expected = "java/batik/expected.txt",
      },
      /*
      // c_alias graphs
      {
          .test_name = "init",
          .graph = "c_alias/init/init.csv",
          .grammar = "c_alias/init/grammar.cnf",
          .expected = "c_alias/init/expected.txt",
      },
      {
          .test_name = "kernel",
          .graph = "c_alias/kernel/kernel.csv",
          .grammar = "c_alias/kernel/grammar.cnf",
          .expected = "c_alias/kernel/expected.txt",
      },
      {
          .test_name = "fs",
          .graph = "c_alias/fs/fs.csv",
          .grammar = "c_alias/fs/grammar.cnf",
          .expected = "c_alias/fs/expected.txt",
      },
      {
          .test_name = "net",
          .graph = "c_alias/net/net.csv",
          .grammar = "c_alias/net/grammar.cnf",
          .expected = "c_alias/net/expected.txt",
      },
      {
          .test_name = "mm",
          .graph = "c_alias/mm/mm.csv",
          .grammar = "c_alias/mm/grammar.cnf",
          .expected = "c_alias/mm/expected.txt",
      },
      {
          .test_name = "arch",
          .graph = "c_alias/arch/arch.csv",
          .grammar = "c_alias/arch/grammar.cnf",
          .expected = "c_alias/arch/expected.txt",
      },
      {
          .test_name = "lib",
          .graph = "c_alias/lib/lib.csv",
          .grammar = "c_alias/lib/grammar.cnf",
          .expected = "c_alias/lib/expected.txt",
      },
      {
          .test_name = "sound",
          .graph = "c_alias/sound/sound.csv",
          .grammar = "c_alias/sound/grammar.cnf",
          .expected = "c_alias/sound/expected.txt",
      },
      // rdf graphs
      {
          .test_name = "go",
          .graph = "rdf/go/go.csv",
          .grammar = "rdf/go/grammar.cnf",
          .expected = "rdf/go/expected.txt",
      },
      {
          .test_name = "go_hierarchy",
          .graph = "rdf/go_hierarchy/go_hierarchy.csv",
          .grammar = "rdf/go/grammar.cnf",
          .expected = "rdf/go/expected.txt",
      },
      {
          .test_name = "taxonomy",
          .graph = "rdf/taxonomy/taxonomy.csv",
          .grammar = "rdf/taxonomy/grammar.cnf",
          .expected = "rdf/taxonomy/expected.txt",
      },
      {
          .test_name = "taxonomy_hierarchy",
          .graph = "rdf/taxonomy_hierarchy/taxonomy_hierarchy.csv",
          .grammar = "rdf/taxonomy_hierarchy/grammar.cnf",
          .expected = "rdf/taxonomy_hierarchy/expected.txt",
      },
      {
          .test_name = "eclass",
          .graph = "rdf/eclass/eclass.csv",
          .grammar = "rdf/eclass/grammar.cnf",
          .expected = "rdf/eclass/expected.txt",
      },
      */
  };

  for (const auto &config : configs) {
    if (!run_algo(config, path_to_testdir)) {
      std::cout << "faild test : " << config.test_name << std::endl;
    }
  }

  return true;
}

int main() { return test("../data/test_data/"); }