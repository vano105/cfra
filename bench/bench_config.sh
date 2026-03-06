#!/usr/bin/env bash

# Number of repetitions per measurement
RUNS=3

# Timeout in seconds
TIMEOUT=600

# Output file
OUTPUT_CSV="results.csv"

# Solvers
# Format: "name|command"
# {graph} and {grammar} are substituted automatically.
SOLVERS=(
    "cfra-base|./build/cfra --graph {graph} --grammar {grammar} --algo base"
    "cfra-incremental|./build/cfra --graph {graph} --grammar {grammar} --algo incremental"
    "cfra-lazy|./build/cfra --graph {graph} --grammar {grammar} --algo lazy"
)

# Datasets
# Format: "name|graph_path|grammar_path"
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
    "commons_io|data/test_data/java/commons_io/commons_io.csv|data/test_data/java/commons_io/grammar.cnf"
    "luindex|data/test_data/java/luindex/luindex.csv|data/test_data/java/luindex/grammar.cnf"
    "commons_lang3|data/test_data/java/commons_lang3/commons_lang3.csv|data/test_data/java/commons_lang3/grammar.cnf"
    "mockito|data/test_data/java/mockito/mockito.csv|data/test_data/java/mockito/grammar.cnf"
    "fop|data/test_data/java/fop/fop.csv|data/test_data/java/fop/grammar.cnf"
    "xalan|data/test_data/java/xalan/xalan.csv|data/test_data/java/xalan/grammar.cnf"
    "junit5|data/test_data/java/junit5/junit5.csv|data/test_data/java/junit5/grammar.cnf"
    "jython|data/test_data/java/jython/jython.csv|data/test_data/java/jython/grammar.cnf"
    "tradesoap|data/test_data/java/tradesoap/tradesoap.csv|data/test_data/java/tradesoap/grammar.cnf"
    "tradebeans|data/test_data/java/tradebeans/tradebeans.csv|data/test_data/java/tradebeans/grammar.cnf"
    "jackson|data/test_data/java/jackson/jackson.csv|data/test_data/java/jackson/grammar.cnf"
    "guava|data/test_data/java/guava/guava.csv|data/test_data/java/guava/grammar.cnf"

    # === C alias analysis ===
    "arch|data/test_data/c_alias/arch/arch.csv|data/test_data/c_alias/arch/grammar.cnf"
    "fs|data/test_data/c_alias/fs/fs.csv|data/test_data/c_alias/fs/grammar.cnf"
    "init|data/test_data/c_alias/init/init.csv|data/test_data/c_alias/init/grammar.cnf"
    "kernel|data/test_data/c_alias/kernel/kernel.csv|data/test_data/c_alias/kernel/grammar.cnf"
    "lib|data/test_data/c_alias/lib/lib.csv|data/test_data/c_alias/lib/grammar.cnf"
    "mm|data/test_data/c_alias/mm/mm.csv|data/test_data/c_alias/mm/grammar.cnf"
    "net|data/test_data/c_alias/net/net.csv|data/test_data/c_alias/net/grammar.cnf"
    "sound|data/test_data/c_alias/sound/sound.csv|data/test_data/c_alias/sound/grammar.cnf"
    "ipc|data/test_data/c_alias/ipc/ipc.csv|data/test_data/c_alias/ipc/grammar.cnf"
    "block|data/test_data/c_alias/block/block.csv|data/test_data/c_alias/block/grammar.cnf"
    "crypto|data/test_data/c_alias/crypto/crypto.csv|data/test_data/c_alias/crypto/grammar.cnf"
    "security|data/test_data/c_alias/security/security.csv|data/test_data/c_alias/security/grammar.cnf"
    "drivers|data/test_data/c_alias/drivers/drivers.csv|data/test_data/c_alias/drivers/grammar.cnf"
    "apache|data/test_data/c_alias/apache/apache.csv|data/test_data/c_alias/apache/grammar.cnf"
    "postgre|data/test_data/c_alias/postgre/postgre.csv|data/test_data/c_alias/postgre/grammar.cnf"

    # === RDF ===
    "eclass|data/test_data/rdf/eclass/eclass.csv|data/test_data/rdf/eclass/grammar.cnf"
    "go|data/test_data/rdf/go/go.csv|data/test_data/rdf/go/grammar.cnf"
    "go_hierarchy|data/test_data/rdf/go_hierarchy/go_hierarchy.csv|data/test_data/rdf/go_hierarchy/grammar.cnf"
    "taxonomy|data/test_data/rdf/taxonomy/taxonomy.csv|data/test_data/rdf/taxonomy/grammar.cnf"
    "taxonomy_hierarchy|data/test_data/rdf/taxonomy_hierarchy/taxonomy_hierarchy.csv|data/test_data/rdf/taxonomy_hierarchy/grammar.cnf"
)