#include "grammar/grammar.hpp"
#include "graph/graph.hpp"
#include "base_algo/base_algo.hpp"
#include "incremental_algo/incremental_algo.hpp"

#include <cubool/cubool.h>
#include <iostream>
#include <string>

static void usage(const char* prog) {
    std::cerr << "Использование:\n"
              << "  " << prog << " --grammar <путь> --graph <путь> [опции]\n"
              << "\n"
              << "Обязательные:\n"
              << "  --grammar <путь>   POCR CNF грамматика (с шаблонами _i)\n"
              << "  --graph <путь>     список рёбер: 'src dst label' на строку\n"
              << "\n"
              << "Опции:\n"
              << "  --algo <имя>       алгоритм: base | incremental (по умолчанию: base)\n"
              << "  --cpu              принудительно использовать CPU бэкенд\n"
              << "  --help             показать эту справку\n";
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
            std::cerr << "Неизвестный аргумент: " << arg << "\n\n";
            usage(argv[0]);
            return 1;
        }
    }

    if (grammar_path.empty() || graph_path.empty()) {
        std::cerr << "Ошибка: необходимо указать --grammar и --graph\n\n";
        usage(argv[0]);
        return 1;
    }

    if (algo != "base" && algo != "incremental") {
        std::cerr << "Ошибка: неизвестный алгоритм '" << algo << "'\n"
                  << "Допустимые значения: base, incremental\n";
        return 1;
    }

    try {
        LabeledGraph graph = LabeledGraph::load(graph_path);
        TemplateGrammar tmpl = TemplateGrammar::load(grammar_path);
        CnfGrammar grammar = CnfGrammar::expand(tmpl, graph.labels());
        cuBool_Status status = cuBool_Initialize(init_hints);
        if (status != CUBOOL_STATUS_SUCCESS) {
            std::cerr << "Ошибка инициализации cuBool (код=" << status << ")\n";
            return 1;
        }

        std::cout << "\n=== Запуск CFL-достижимости (" << algo << ") ===\n";
        CflrResult result = (algo == "incremental")
            ? run_cflr_incremental(grammar, graph)
            : run_cflr_non_incremental(grammar, graph);

        std::cout << "\n=== Результат ===\n";
        std::cout << "AnalysisTime: " << result.elapsed_secs << "\n";
        std::cout << "#SEdges: " << result.start_nvals << "\n";

        cuBool_Finalize();

    } catch (const std::exception& e) {
        std::cerr << "Ошибка: " << e.what() << "\n";
        cuBool_Finalize();
        return 1;
    }

    return 0;
}