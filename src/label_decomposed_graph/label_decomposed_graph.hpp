#pragma once
#include <cstddef>
#include <cubool.h>
#include <fstream>
#include <iostream>
#include <map>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

class label_decomposed_graph {
private:
  std::map<std::string, cuBool_Matrix> matrices{};

public:
  size_t matrix_size{};

  label_decomposed_graph() {}

  label_decomposed_graph(const size_t size) : matrix_size(size) {}

  label_decomposed_graph(const std::string &path) {
    std::ifstream file(path);
    std::map<std::string, std::pair<std::vector<int>, std::vector<int>>>
        result_matrix;

    if (!file.is_open()) {
      std::cerr << "Can't open file: " << path << std::endl;
    }
    std::string line;
    size_t line_count = 0;
    while (std::getline(file, line)) {
      line_count++;
      std::istringstream iss(line);
      size_t v, to;
      std::string label;

      if (!(iss >> v >> label >> to)) {
        std::cerr << "Wrong file format: " << line << std::endl;
        continue;
      }
      matrix_size = std::max(std::max(matrix_size, v), to);
      result_matrix[label].first.emplace_back(v);
      result_matrix[label].first.emplace_back(to);
    }

    for (auto &it : result_matrix) {
      matrices[it.first] = nullptr;
      cuBool_Matrix_New(&matrices[it.first], matrix_size, matrix_size);
      size_t number_of_cols = result_matrix[it.first].first.size();
      cuBool_Index *rows = new cuBool_Index(number_of_cols);
      cuBool_Index *cols = new cuBool_Index(number_of_cols);
      size_t i = 0;
      for (auto x : result_matrix[it.first].first)
        rows[++i];
      i = 0;
      for (auto x : result_matrix[it.first].second)
        cols[++i];
      cuBool_Matrix_Build(matrices[it.first], rows, cols, number_of_cols,
                          CUBOOL_HINT_NO);
    }
    file.close();
  }

  label_decomposed_graph(const label_decomposed_graph &other)
      : matrices(other.matrices) {}

  cuBool_Matrix &operator[](const std::string &key) {
    if (matrices.find(key) == matrices.end()) {
      matrices[key] = nullptr;
      cuBool_Matrix_New(&matrices[key], matrix_size, matrix_size);
    }
    return matrices[key];
  }

  cuBool_Matrix &get_item(const std::string &key) {
    if (matrices.find(key) == matrices.end())
      ;
    return matrices[key];
  }

  void set_item(const std::string &key, const cuBool_Matrix &matr) {
    matrices.emplace(key, matr);
  }

  size_t size() { return matrices.size(); }

  ~label_decomposed_graph() {}
};
