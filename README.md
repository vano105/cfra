# CFRA

**CFRA** (Context-Free Reachability Analyzer) is a GPU-accelerated solver for the Context-Free Language Reachability (CFL-R) problem, based on sparse Boolean linear algebra operations. It uses the [cuBool](https://github.com/SparseLinearAlgebra/cuBool) library to perform matrix operations on NVIDIA CUDA.

The implementation is based on the matrix CFL-reachability algorithm by R. Azimov and optimizations proposed by I. Muravev.

## Features

* Three variants of the matrix CFL-reachability algorithm:
  * **Base** - `M = M + M * M` until fixed point
  * **Incremental** - operates only on the delta of new pairs, O(n^4) instead of O(n^5)
  * **Incremental with lazy addition** - M is stored as a set of chunks, theoretical complexity O(n^3)
* Trivial operation optimization: skipping multiplications and additions with empty matrices via nvals caching
* Template CNF grammar support (POCR format): automatic expansion templates
* Benchmark system for comparison with other solvers

## Requirements

* Linux (tested on Ubuntu 22.04)
* CMake >= 3.15
* GCC with C++20 support
* NVIDIA CUDA Toolkit (for GPU backend)

## Build

```bash
git clone https://github.com/vano105/cfra.git
cd cfra
git submodule update --init --recursive

cmake -B build -S .
cmake --build build
```

To build without CUDA (CPU only):

```bash
cmake -B build -S . -DCUBOOL_WITH_CUDA=OFF
cmake --build build
```

## Usage

```bash
./build/cfra --grammar <grammar.cnf> --graph <graph.csv> [options]
```

**Options:**

| Argument | Description |
| --- | --- |
| `--algo base` | Base algorithm (Azimov) |
| `--algo incremental` | Incremental algorithm (default) |
| `--algo lazy` | Incremental with lazy addition |
| `--cpu` | Force CPU backend |

**Examples:**

```bash
./build/cfra --grammar data/test_data/java/avrora/grammar.cnf \
             --graph data/test_data/java/avrora/avrora.csv \
             --algo lazy

./build/cfra --grammar data/test_data/c_alias/kernel/grammar.cnf \
             --graph data/test_data/c_alias/kernel/kernel.csv \
             --algo incremental --cpu
```

**Output format:**

```
Graph: 24690 vertices, 3136 labels, 50392 edges
Expanded grammar: 4 epsilon rules, 2 terminal rules, 0 chain rules, 6009 complex rules, 6869 nonterminals
AnalysisTime: 0.748
#SEdges: 183043
```

## Input Format

**Graph** (`*.csv`):

```
0 1 assign
1 2 load_i_3
2 3 store_i_3
3 0 alloc
```

**Grammar** (`*.cnf`) - CFG rules in CNF format:

```
PT alloc
PT PT alloc
PT assign
PT assign PT
PT load_i AS_i
...
Count:
PT                      # start nonterminal
```

Symbols ending with `_i` are automatically expanded using indices found in the graph.

## Benchmarking

The benchmark system allows automatic comparison of CFRA with other solvers:

```bash
./bench/bench.sh --config bench/bench_config.sh --runs 3
```

## Performance

Benchmarked on: PC with Ubuntu 22.04, Intel Core i5-11400f 2.6GHz CPU, DDR4 16Gb RAM, Nvidia RTX 3050 GPU with 8Gb VRAM. 

![java](docs/pictures/java.png)
![c_alias](docs/pictures/c_alias.png)
![rdf](docs/pictures/rdf.png)
![vf](docs/pictures/vf.png)

## Project Structure

```
cfra
├── bench                              - benchmark system
│   ├── bench.sh                       - benchmark runner script
│   ├── bench_config.sh                - configuration: solvers, datasets
├── cuBool                             - dependency: cuBool library (submodule)
├── src
│   ├── base_algo/                     - base algorithm
│   ├── incremental_algo/              - incremental algorithm
│   ├── lazy_algo/                     - incremental with lazy addition
│   ├── matrix_store/                  - matrix storage with nvals caching
│   ├── grammar/                       - grammar parsing and template expansion
│   ├── graph/                         - graph loading
├── test_data                          - small test grammars and graphs
└── CMakeLists.txt
```