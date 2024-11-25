cmake_minimum_required(VERSION 3.10)
set (CMAKE_CXX_STANDARD 20)
project(cfra)

add_executable(${CMAKE_PROJECT_NAME} "")

add_subdirectory(cuBool)

target_include_directories(${CMAKE_PROJECT_NAME} PUBLIC cuBool/cubool/include/cubool)

target_link_directories(${CMAKE_PROJECT_NAME} PUBLIC build/cuBool/cubool)
target_link_libraries(${CMAKE_PROJECT_NAME} PUBLIC cubool)

target_sources(${CMAKE_PROJECT_NAME} PUBLIC src/main.cpp src/cnf_grammar/cnf_grammar.hpp src/base_algo/base_matrix_algo.hpp src/label_decomposed_graph/label_decomposed_graph.hpp)
