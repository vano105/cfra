#!/usr/bin/env bash

# Число повторов каждого замера
RUNS=3

# Таймаут в секундах
TIMEOUT=600

# Файл с результатами
OUTPUT_CSV="results.csv"

# Решатели
SOLVERS=(
    "cfra-base|./build/cfra --graph {graph} --grammar {grammar} --algo base"
    "cfra-incremental|./build/cfra --graph {graph} --grammar {grammar} --algo incremental"
    "cfra-lazy|./build/cfra --graph {graph} --grammar {grammar} --algo lazy"
)

# Датасеты
# Формат: "имя|путь_к_графу|путь_к_грамматике"

DATASETS=(
    # === Java (points-to analysis) ===
    "avrora|data/test_data/java/avrora/avrora.csv|data/test_data/java/avrora/grammar.cnf"
    "batik|data/test_data/java/batik/batik.csv|data/test_data/java/batik/grammar.cnf"
    "eclipse|data/test_data/java/eclipse/eclipse.csv|data/test_data/java/eclipse/grammar.cnf"
    "gson|data/test_data/java/gson/gson.csv|data/test_data/java/gson/grammar.cnf"
    "h2|data/test_data/java/h2/h2.csv|data/test_data/java/h2/grammar.cnf"
    "lusearch|data/test_data/java/lusearch/lusearch.csv|data/test_data/java/lusearch/grammar.cnf"
    "pmd|data/test_data/java/pmd/pmd.csv|data/test_data/java/pmd/grammar.cnf"
    "sunflow|data/test_data/java/sunflow/sunflow.csv|data/test_data/java/sunflow/grammar.cnf"
    "tomcat|data/test_data/java/tomcat/tomcat.csv|data/test_data/java/tomcat/grammar.cnf"

    # === C alias analysis ===
    "arch|data/test_data/c_alias/arch/arch.csv|data/test_data/c_alias/arch/grammar.cnf"
    "fs|data/test_data/c_alias/fs/fs.csv|data/test_data/c_alias/fs/grammar.cnf"
    "init|data/test_data/c_alias/init/init.csv|data/test_data/c_alias/init/grammar.cnf"
    "kernel|data/test_data/c_alias/kernel/kernel.csv|data/test_data/c_alias/kernel/grammar.cnf"
    "lib|data/test_data/c_alias/lib/lib.csv|data/test_data/c_alias/lib/grammar.cnf"
    "mm|data/test_data/c_alias/mm/mm.csv|data/test_data/c_alias/mm/grammar.cnf"
    "net|data/test_data/c_alias/net/net.csv|data/test_data/c_alias/net/grammar.cnf"
    "sound|data/test_data/c_alias/sound/sound.csv|data/test_data/c_alias/sound/grammar.cnf"

    # === RDF ===
    "eclass|data/test_data/rdf/eclass/eclass.csv|data/test_data/rdf/eclass/grammar.cnf"
    "go|data/test_data/rdf/go/go.csv|data/test_data/rdf/go/grammar.cnf"
    "go_hierarchy|data/test_data/rdf/go_hierarchy/go_hierarchy.csv|data/test_data/rdf/go_hierarchy/grammar.cnf"
    "taxonomy|data/test_data/rdf/taxonomy/taxonomy.csv|data/test_data/rdf/taxonomy/grammar.cnf"
    "taxonomy_hierarchy|data/test_data/rdf/taxonomy_hierarchy/taxonomy_hierarchy.csv|data/test_data/rdf/taxonomy_hierarchy/grammar.cnf"
)