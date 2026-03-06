#include "grammar/grammar.hpp"
#include "graph/graph.hpp"
#include "base_algo/base_algo.hpp"
#include "incremental_algo/incremental_algo.hpp"
#include "lazy_algo/lazy_algo.hpp"

#include <cubool/cubool.h>
#include <iostream>
#include <string>

static void usage(const char* prog) {
    std::cerr << "Usage:\n"
              << "  " << prog << " --grammar <path> --graph <path> [options]\n"
              << "\n"
              << "Required:\n"
              << "  --grammar <path>   POCR CNF grammar (with _i templates)\n"
              << "  --graph <path>     edge list: 'src dst label' per line\n"
              << "\n"
              << "Options:\n"
              << "  --algo <name>      algorithm: base | incremental | lazy (default: base)\n"
              << "  --cpu              force CPU backend\n"
              << "  --help             show this help\n";
}

int main(int argc, char** argv) {
    std::string graph_path;
    std::string grammar_path;
    std::string algo = "base";
    cuBool_Hints init_hints = CUBOOL_HINT_NO;

    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];

        if ((arg == "--grammar") && i + 1 < argc) {
            grammar_path = argv[++i];
        } else if ((arg == "--graph") && i + 1 < argc) {
            graph_path = argv[++i];
        } else if ((arg == "--algo") && i + 1 < argc) {
            algo = argv[++i];
        } else if (arg == "--cpu") {
            init_hints = CUBOOL_HINT_CPU_BACKEND;
        } else if (arg == "--help" || arg == "-h") {
            usage(argv[0]);
            return 0;
        } else {
            std::cerr << "Unknown argument: " << arg << "\n\n";
            usage(argv[0]);
            return 1;
        }
    }

    if (grammar_path.empty() || graph_path.empty()) {
        std::cerr << "Error: --grammar and --graph are required\n\n";
        usage(argv[0]);
        return 1;
    }

    if (algo != "base" && algo != "incremental" && algo != "lazy") {
        std::cerr << "Error: unknown algorithm '" << algo << "'\n"
                  << "Valid values: base, incremental, lazy\n";
        return 1;
    }

    try {
        LabeledGraph graph = LabeledGraph::load(graph_path);
        TemplateGrammar tmpl = TemplateGrammar::load(grammar_path);
        CnfGrammar grammar = CnfGrammar::expand(tmpl, graph.labels());

        cuBool_Status status = cuBool_Initialize(init_hints);
        if (status != CUBOOL_STATUS_SUCCESS) {
            std::cerr << "cuBool initialization failed (code=" << status << ")\n";
            return 1;
        }

        CflrResult result;
        if (algo == "incremental")
            result = run_cflr_incremental(grammar, graph);
        else if (algo == "lazy")
            result = run_cflr_lazy(grammar, graph);
        else
            result = run_cflr_non_incremental(grammar, graph);

        std::cout << "AnalysisTime: " << result.elapsed_secs << "\n";
        std::cout << "#SEdges: " << result.start_nvals << "\n";

        cuBool_Finalize();

    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n";
        cuBool_Finalize();
        return 1;
    }

    return 0;
}