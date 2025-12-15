#pragma once
#include <cubool.h>
#include <fstream>
#include <iostream>
#include <map>
#include <ostream>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

class label_decomposed_graph {
public:
  std::map<std::string, cuBool_Matrix> matrices{};

  using PairOfValues = std::pair<std::vector<int>, std::vector<int>>;
  size_t matrix_size{};

  label_decomposed_graph() {}

  label_decomposed_graph(const size_t size) : matrix_size(size) {}

  // load from txt file
  label_decomposed_graph(const std::string &path) {
    std::ifstream file(path);
    if (!file.is_open()) {
      std::cerr << "Can't open file: " << path << std::endl;
    }

    std::map<std::string, PairOfValues> result_matrix;
    std::string line;
    size_t line_count = 0;
    while (std::getline(file, line)) {
      line_count++;
      std::istringstream iss(line);
      size_t v, to;
      std::string label;

      if (!(iss >> v >> to >> label)) {
        std::cerr << "Wrong file format: " << line << std::endl;
        continue;
      }
      matrix_size = std::max(std::max(matrix_size, v), to);

      auto &value = result_matrix[label];
      value.first.emplace_back(v);
      value.second.emplace_back(to);
    }
    ++matrix_size;

    for (auto &[label, value] : result_matrix) {
      cuBool_Matrix matrix;
      cuBool_Matrix_New(&matrix, matrix_size, matrix_size);
      size_t number_of_values = value.first.size();

      std::vector<cuBool_Index> rows(number_of_values, 0);
      std::vector<cuBool_Index> cols(number_of_values, 0);

      size_t i = 0;
      for (auto x : value.first) {
        rows[i++] = x;
      }
      i = 0;
      for (auto x : value.second) {
        cols[i++] = x;
      }
      cuBool_Matrix_Build(matrix, rows.data(), cols.data(), number_of_values,
                          CUBOOL_HINT_NO);
      matrices[label] = matrix;
    }
    file.close();
  }

  label_decomposed_graph(const label_decomposed_graph &other) {
    matrix_size = other.matrix_size;
    for (const auto& [key, matrix] : other.matrices) {
      cuBool_Matrix_Duplicate(matrix, &matrices[key]);
    }
  }

  cuBool_Matrix &operator[](const std::string &key) {
    if (matrices.find(key) == matrices.end()) {
      cuBool_Matrix matrix;
      cuBool_Matrix_New(&matrix, matrix_size, matrix_size);
      cuBool_Matrix_Build(matrix, nullptr, nullptr, 0, CUBOOL_HINT_NO);
      matrices[key] = matrix;
    }
    return matrices[key];
  }

  cuBool_Matrix &get_item(const std::string &key) {
    return matrices[key];
  }

  void set_item(const std::string &key, const cuBool_Matrix &matr) {
    // Free old matrix if exists
    auto it = matrices.find(key);
    if (it != matrices.end()) {
      cuBool_Matrix_Free(it->second);
    }
    matrices[key] = matr;
  }

  size_t size() { return matrices.size(); }

  ~label_decomposed_graph() {
    for (auto &matr : matrices) {
      cuBool_Matrix_Free(matr.second);
    }
  }
};