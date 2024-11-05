#pragma once
// #include <cubool/cubool.h>
#include <map>
#include <string>
#include <vector>

struct cuBool_Matrix {
  std::vector<std::vector<std::string>> m;
  cuBool_Matrix &ewiseadd(const cuBool_Matrix &matr) { return *this; }
};

class label_decomposed_graph {
private:
  std::map<std::string, cuBool_Matrix> matrices{};

public:
  size_t matrix_size{};

  label_decomposed_graph() {}

  label_decomposed_graph(const size_t size) : matrix_size(size) {}

  label_decomposed_graph(const std::string &file) {}

  label_decomposed_graph(const label_decomposed_graph &other)
      : matrices(other.matrices) {}

  cuBool_Matrix &operator[](const std::string &key) {
    if (matrices.find(key) == matrices.end()) {
      matrices.emplace(key);
      // cuBool_Matrix_New(&(matrices[key]), matrix_size, matrix_size);
      // fill empty matrix
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
