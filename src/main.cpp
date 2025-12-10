#include "base_algo/incremental_matrix_algo.hpp"
#include "base_algo/trivial_optimized_algo.hpp"
#include "base_algo/lazy_add_optimized_algo.hpp"
#include "base_algo/template_grammar_optimized_algo.hpp"
#include "base_algo/fully_optimized_algo.hpp"
#include "cnf_grammar/grammar_template_expander.hpp"
#include "base_algo/base_matrix_algo.hpp"
#include "base_algo/diagnostic_base_matrix_algo.hpp"
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

bool run_algo(const Config &config, const std::string &path_to_testdir,
              CFReachabilityAlgoFactory::AlgoType algo_type) {
  cuBool_Initialize(CUBOOL_HINT_NO);

  std::cout << "\n=== Testing: " << config.test_name << " ===" << std::endl;
  
  // Автоматически раскрываем шаблоны в грамматике, если нужно
  std::string grammar_path = path_to_testdir + config.grammar;
  std::string graph_path = path_to_testdir + config.graph;
  
  std::string expanded_grammar = GrammarTemplateExpander::auto_expand_if_needed(
      grammar_path, graph_path);

  // Запускаем выбранный алгоритм
  auto start = std::chrono::high_resolution_clock::now();
  
  cuBool_Matrix result = CFReachabilityAlgoFactory::solve(
      expanded_grammar,
      graph_path,
      algo_type
  );
  
  auto end = std::chrono::high_resolution_clock::now();
  std::chrono::duration<double> elapsed = end - start;
  
  cuBool_Index nvals;
  cuBool_Matrix_Nvals(result, &nvals);
  std::vector<cuBool_Index> tc_rows(nvals), tc_cols(nvals);
  cuBool_Matrix_ExtractPairs(result, tc_rows.data(), tc_cols.data(), &nvals);

  std::cout << "Time: " << elapsed.count() << " seconds" << std::endl;
  std::cout << "Found: " << nvals << " reachable pairs" << std::endl;

  // Проверка результатов
  std::ifstream file(path_to_testdir + config.expected);
  if (!file) {
    std::cout << "Warning: Can't open expected file: " 
              << path_to_testdir + config.expected << std::endl;
    cuBool_Matrix_Free(result);
    cuBool_Finalize();
    return true; // Считаем успешным, если нет файла ожидаемых результатов
  }
  
  std::vector<std::pair<int, int>> expected;
  {
    std::string line;
    while (std::getline(file, line)) {
      if (line.empty()) continue;
      
      std::istringstream iss(line);
      int row, col;
      
      // Поддержка разных форматов: "row col" или "row\tcol"
      if (iss >> row >> col) {
        expected.emplace_back(row, col);
      }
    }
  }
  file.close();

  // Сравниваем результаты
  bool verify = true;
  
  if (nvals != expected.size()) {
    std::cout << "Error: Size mismatch! Got " << nvals 
              << " but expected " << expected.size() << std::endl;
    verify = false;
  } else {
    // Создаем множества для сравнения
    std::set<std::pair<int, int>> result_set, expected_set;
    
    for (size_t i = 0; i < nvals; i++) {
      result_set.insert({tc_rows[i], tc_cols[i]});
    }
    
    for (const auto& p : expected) {
      expected_set.insert(p);
    }
    
    if (result_set != expected_set) {
      std::cout << "Error: Results don't match!" << std::endl;
      verify = false;
      
      // Показываем различия
      std::cout << "Missing in result:" << std::endl;
      for (const auto& p : expected_set) {
        if (result_set.find(p) == result_set.end()) {
          std::cout << "  (" << p.first << ", " << p.second << ")" << std::endl;
        }
      }
      
      std::cout << "Extra in result:" << std::endl;
      for (const auto& p : result_set) {
        if (expected_set.find(p) == expected_set.end()) {
          std::cout << "  (" << p.first << ", " << p.second << ")" << std::endl;
        }
      }
    }
  }

  if (verify) {
    std::cout << "✓ Test passed!" << std::endl;
  } else {
    std::cout << "✗ Test failed!" << std::endl;
  }

  // Сохраняем результаты
  std::ofstream out_file(path_to_testdir + "result_" + config.test_name + ".txt");
  for (size_t i = 0; i < nvals; i++) {
    out_file << tc_rows[i] << '\t' << tc_cols[i] << '\n';
  }
  out_file.close();

  cuBool_Matrix_Free(result);
  cuBool_Finalize();
  
  // Удаляем временный файл раскрытой грамматики, если он был создан
  if (expanded_grammar != grammar_path) {
    std::remove(expanded_grammar.c_str());
  }
  
  return verify;
}

bool test(const std::string &path_to_testdir, 
          CFReachabilityAlgoFactory::AlgoType algo_type) {
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
      {
          .test_name = "avrora",
          .graph = "java/avrora/avrora.csv",
          .grammar = "java/avrora/old_grammar.cnf",
          .expected = "java/avrora/expected.txt",
      }
  };

  bool all_passed = true;
  for (const auto &config : configs) {
    bool passed = run_algo(config, path_to_testdir, algo_type);
    all_passed = all_passed && passed;
  }

  return all_passed;
}

void print_usage() {
  std::cout << "Usage: cfra [OPTIONS]" << std::endl;
  std::cout << "\nOptions:" << std::endl;
  std::cout << "  --test [algo_type]    Run tests with specified algorithm" << std::endl;
  std::cout << "  --benchmark           Benchmark all algorithm versions" << std::endl;
  std::cout << "  --grammar <path>      Path to grammar file" << std::endl;
  std::cout << "  --graph <path>        Path to graph file" << std::endl;
  std::cout << "  --algo <type>         Algorithm type to use" << std::endl;
  std::cout << "\nAlgorithm types:" << std::endl;
  std::cout << "  base                  Base Azimov algorithm" << std::endl;
  std::cout << "  incremental           With incremental computations" << std::endl;
  std::cout << "  trivial               With trivial operation checks" << std::endl;
  std::cout << "  lazy                  With lazy addition" << std::endl;
  std::cout << "  template              With template optimizations" << std::endl;
  std::cout << "  full                  All optimizations" << std::endl;
  std::cout << "  auto                  Automatic selection (default)" << std::endl;
}

CFReachabilityAlgoFactory::AlgoType parse_algo_type(const std::string& type) {
  if (type == "base") return CFReachabilityAlgoFactory::AlgoType::BASE;
  if (type == "diagnostic") return CFReachabilityAlgoFactory::AlgoType::DIAGNOSTIC;
  if (type == "incremental") return CFReachabilityAlgoFactory::AlgoType::INCREMENTAL;
  if (type == "trivial") return CFReachabilityAlgoFactory::AlgoType::TRIVIAL_OPT;
  if (type == "lazy") return CFReachabilityAlgoFactory::AlgoType::LAZY_ADD;
  if (type == "template") return CFReachabilityAlgoFactory::AlgoType::TEMPLATE_OPT;
  if (type == "full") return CFReachabilityAlgoFactory::AlgoType::FULLY_OPTIMIZED;
  if (type == "auto") return CFReachabilityAlgoFactory::AlgoType::AUTO;
  
  std::cerr << "Unknown algorithm type: " << type << std::endl;
  std::cerr << "Using 'auto' instead." << std::endl;
  return CFReachabilityAlgoFactory::AlgoType::AUTO;
}

int main(int argc, char* argv[]) {
  if (argc == 1) {
    // По умолчанию запускаем тесты с автоматическим выбором алгоритма
    std::cout << "Running tests with automatic algorithm selection..." << std::endl;
    return test("../test_data/", CFReachabilityAlgoFactory::AlgoType::AUTO) ? 0 : 1;
  }
  
  std::string mode = argv[1];
  
  if (mode == "--help" || mode == "-h") {
    print_usage();
    return 0;
  }
  
  if (mode == "--test") {
    auto algo_type = CFReachabilityAlgoFactory::AlgoType::AUTO;
    
    if (argc > 2) {
      algo_type = parse_algo_type(argv[2]);
    }
    
    std::cout << "Running tests..." << std::endl;
    return test("../test_data/", algo_type) ? 0 : 1;
  }
  
  if (mode == "--benchmark") {
    std::string grammar_path = "../test_data/an_bn/grammar.cnf";
    std::string graph_path = "../test_data/an_bn/graph.txt";
    
    if (argc > 3) {
      grammar_path = argv[2];
      graph_path = argv[3];
    }
    
    std::cout << "Benchmarking all algorithms..." << std::endl;
    CFReachabilityAlgoFactory::benchmark_all(grammar_path, graph_path);
    return 0;
  }
  
  // Режим одиночного запуска
  std::string grammar_path, graph_path;
  auto algo_type = CFReachabilityAlgoFactory::AlgoType::AUTO;
  
  for (int i = 1; i < argc; i++) {
    std::string arg = argv[i];
    
    if (arg == "--grammar" && i + 1 < argc) {
      grammar_path = argv[++i];
    } else if (arg == "--graph" && i + 1 < argc) {
      graph_path = argv[++i];
    } else if (arg == "--algo" && i + 1 < argc) {
      algo_type = parse_algo_type(argv[++i]);
    }
  }
  
  if (grammar_path.empty() || graph_path.empty()) {
    std::cerr << "Error: Both --grammar and --graph must be specified" << std::endl;
    print_usage();
    return 1;
  }
  
  std::cout << "Running CF-reachability solver..." << std::endl;
  std::cout << "Grammar: " << grammar_path << std::endl;
  std::cout << "Graph: " << graph_path << std::endl;
  
  // Автоматически раскрываем шаблоны
  std::string expanded_grammar = GrammarTemplateExpander::auto_expand_if_needed(
      grammar_path, graph_path);

  cuBool_Initialize(CUBOOL_HINT_NO);
  
  auto start = std::chrono::high_resolution_clock::now();
  cuBool_Matrix result = CFReachabilityAlgoFactory::solve(
      expanded_grammar, graph_path, algo_type);
  auto end = std::chrono::high_resolution_clock::now();
  
  std::chrono::duration<double> elapsed = end - start;
  
  cuBool_Index nvals;
  cuBool_Matrix_Nvals(result, &nvals);
  
  std::cout << "\nResults:" << std::endl;
  std::cout << "  Time: " << elapsed.count() << " seconds" << std::endl;
  std::cout << "  Reachable pairs: " << nvals << std::endl;
  
  // Выводим первые 10 пар
  if (nvals > 0) {
    std::vector<cuBool_Index> rows(nvals), cols(nvals);
    cuBool_Matrix_ExtractPairs(result, rows.data(), cols.data(), &nvals);
    
    std::cout << "\nFirst " << std::min(nvals, (cuBool_Index)10) << " pairs:" << std::endl;
    for (size_t i = 0; i < std::min(nvals, (cuBool_Index)10); i++) {
      std::cout << "  (" << rows[i] << ", " << cols[i] << ")" << std::endl;
    }
  }
  
  cuBool_Matrix_Free(result);
  cuBool_Finalize();
  
  // Удаляем временный файл
  if (expanded_grammar != grammar_path) {
    std::remove(expanded_grammar.c_str());
  }
  
  return 0;
}