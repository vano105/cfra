#include "grammar/grammar.hpp"
#include "graph/graph.hpp"
#include "base_algo/base_algo.hpp"

#include <cubool/cubool.h>
#include <iostream>
#include <string>

static void usage(const char* prog) {
    std::cerr << "Использование: " << prog << " <граф.csv> <грамматика.cnf> [--cpu]\n"
              << "\n"
              << "  граф.csv       — список рёбер: 'src dst label' на строку\n"
              << "  грамматика.cnf — POCR CNF грамматика (с шаблонами _i)\n"
              << "  --cpu          — принудительно использовать CPU бэкенд\n";
}

int main(int argc, char** argv) {
    if (argc < 3) {
        usage(argv[0]);
        return 1;
    }

    std::string graph_path   = argv[1];
    std::string grammar_path = argv[2];

    // Разбор флагов командной строки
    cuBool_Hints init_hints = CUBOOL_HINT_NO;
    for (int i = 3; i < argc; i++) {
        std::string arg = argv[i];
        if (arg == "--cpu")
            init_hints = CUBOOL_HINT_CPU_BACKEND;
    }

    try {
        // 1. Загрузка графа
        std::cout << "=== Загрузка графа: " << graph_path << " ===\n";
        LabeledGraph graph = LabeledGraph::load(graph_path);

        // 2. Чтение шаблонной грамматики
        std::cout << "\n=== Чтение грамматики: " << grammar_path << " ===\n";
        TemplateGrammar tmpl = TemplateGrammar::load(grammar_path);
        std::cout << "Шаблонная грамматика: "
                  << tmpl.epsilon_rules().size() << " ε, "
                  << tmpl.two_token_rules().size() << " двухтокенных, "
                  << tmpl.complex_rules().size() << " комплексных, "
                  << "старт='" << tmpl.start_symbol() << "'\n";

        // 3. Раскрытие шаблонов по индексам из графа
        std::cout << "\n=== Раскрытие шаблонов ===\n";
        CnfGrammar grammar = CnfGrammar::expand(tmpl, graph.labels());

        // 4. Инициализация cuBool
        std::cout << "\n=== Инициализация cuBool ===\n";
        cuBool_Status status = cuBool_Initialize(init_hints);
        if (status != CUBOOL_STATUS_SUCCESS) {
            std::cerr << "Ошибка инициализации cuBool (код=" << status << ")\n";
            return 1;
        }

        // 5. Запуск алгоритма CFL-достижимости
        std::cout << "\n=== Запуск CFL-достижимости ===\n";
        CflrResult result = run_cflr_non_incremental(grammar, graph);

        // 6. Итоговый вывод
        std::cout << "\n=== Результат ===\n";
        std::cout << "AnalysisTime: " << result.elapsed_secs << "\n";
        std::cout << "#SEdges: " << result.start_nvals << "\n";

        // 7. Освобождение ресурсов cuBool
        cuBool_Finalize();

    } catch (const std::exception& e) {
        std::cerr << "Ошибка: " << e.what() << "\n";
        cuBool_Finalize();
        return 1;
    }

    return 0;
}