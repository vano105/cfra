cmake_minimum_required(VERSION 3.10)
set (CMAKE_CXX_STANDARD 20)
project(cfra)

add_executable(${CMAKE_PROJECT_NAME} "")

add_subdirectory(cuBool)

target_include_directories(${CMAKE_PROJECT_NAME} PUBLIC cuBool/cubool/include/cubool
        src/grammar src/graph src/matrix_store src)

target_link_directories(${CMAKE_PROJECT_NAME} PUBLIC build/cuBool/cubool)
target_link_libraries(${CMAKE_PROJECT_NAME} PUBLIC cubool)

target_sources(${CMAKE_PROJECT_NAME} PUBLIC src/main.cpp src/grammar/grammar.cpp src/graph/graph.cpp
        src/base_algo/base_algo.cpp src/matrix_store/matrix_store.cpp src/incremental_algo/incremental_algo.cpp
        src/lazy_algo/lazy_algo.cpp src/lazy_algo/chunked_store.cpp)