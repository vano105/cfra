#include "base_algo.hpp"
#include "../matrix_store/matrix_store.hpp"
#include "../common.hpp"

#include <cubool/cubool.h>
#include <unordered_map>
#include <iostream>
#include <chrono>
#include <stdexcept>

// ============================================================
// Неинкрементальный алгоритм CFL-достижимости
// ============================================================
//
// Классический fixed-point на булевых матрицах:
//
//   1. Инициализация: M[A] := рёбра из терминальных правил + ε-правила
//   2. Итерация до сходимости:
//      - Для каждого A -> B C: M[A] += M[B] × M[C]
//      - Для каждого A -> B:   M[A] |= M[B]
//      - Проверка: если total nvals не изменился — стоп
//

CflrResult run_cflr_non_incremental(const CnfGrammar& grammar, const LabeledGraph& graph)
{
    auto t0 = std::chrono::high_resolution_clock::now();

    cuBool_Index n = static_cast<cuBool_Index>(graph.num_vertices());
    MatrixStore M(n);

    std::cout << "Размерность матриц: " << n << " x " << n << "\n";

    // ================================================================
    // Фаза 1: построить матрицы для терминальных символов (меток графа)
    // ================================================================
    for (auto& [label, edges] : graph.edges_by_label()) {
        if (edges.empty()) continue;

        std::vector<cuBool_Index> rows(edges.size()), cols(edges.size());
        for (size_t i = 0; i < edges.size(); i++) {
            rows[i] = edges[i].src;
            cols[i] = edges[i].dst;
        }
        cuBool_Matrix m = M.ensure(label);
        CB_CHECK(cuBool_Matrix_Build(m, rows.data(), cols.data(),
                 static_cast<cuBool_Index>(edges.size()), CUBOOL_HINT_NO));
    }
    std::cout << "Построено " << graph.labels().size() << " терминальных матриц\n";

    // ================================================================
    // Фаза 2: инициализация нетерминальных матриц
    // ================================================================

    // 2а. Терминальные правила (A -> t): собираем все рёбра метки t в M[A].
    //     Группируем по нетерминалу, чтобы построить каждую матрицу одним вызовом Build.
    {
        std::unordered_map<std::string, std::vector<std::string>> nt_terms;
        for (auto& [nt, term] : grammar.terminal_rules())
            nt_terms[nt].push_back(term);

        for (auto& [nt, terms] : nt_terms) {
            std::vector<cuBool_Index> rows, cols;
            for (auto& term : terms) {
                auto it = graph.edges_by_label().find(term);
                if (it == graph.edges_by_label().end()) continue;
                for (auto& e : it->second) {
                    rows.push_back(e.src);
                    cols.push_back(e.dst);
                }
            }
            cuBool_Matrix m = M.ensure(nt);
            if (!rows.empty()) {
                CB_CHECK(cuBool_Matrix_Build(m, rows.data(), cols.data(),
                         static_cast<cuBool_Index>(rows.size()), CUBOOL_HINT_NO));
            }
        }
    }

    // 2б. ε-правила: добавляем единичную диагональ (петли на всех вершинах)
    for (auto& nt : grammar.epsilon_rules()) {
        cuBool_Matrix m = M.ensure(nt);
        for (cuBool_Index i = 0; i < n; i++)
            CB_CHECK(cuBool_Matrix_SetElement(m, i, i));
    }

    // 2в. Создаём пустые матрицы для оставшихся нетерминалов
    for (auto& nt : grammar.nonterminals())
        M.ensure(nt);

    std::cout << "Нетерминальные матрицы инициализированы\n";

    // ================================================================
    // Фаза 3: итерация до неподвижной точки
    // ================================================================
    std::cout << "Запуск итераций: " << grammar.complex_rules().size() << " комплексных, "
              << grammar.simple_rules().size() << " цепных правил\n";

    int iter = 0;
    uint64_t prev_total = 0;
    uint64_t cur_total = M.total_nvals();

    while (cur_total != prev_total) {
        prev_total = cur_total;
        iter++;

        // --- Комплексные правила: A -> B C  =>  M[A] += M[B] × M[C] ---
        for (auto& [a, b, c] : grammar.complex_rules()) {
            cuBool_Matrix ma = M.get(a);
            cuBool_Matrix mb = M.get(b);
            cuBool_Matrix mc = M.get(c);
            if (!ma || !mb || !mc) continue;

            // Пропускаем умножение, если один из операндов пуст
            cuBool_Index nb = 0, nc = 0;
            cuBool_Matrix_Nvals(mb, &nb);
            cuBool_Matrix_Nvals(mc, &nc);
            if (nb == 0 || nc == 0) continue;

            // Аккумулируем: M[A] = M[A] | (M[B] × M[C])
            CB_CHECK(cuBool_MxM(ma, mb, mc, CUBOOL_HINT_ACCUMULATE));
        }

        // --- Цепные правила: A -> B  =>  M[A] |= M[B] ---
        for (auto& [a, b] : grammar.simple_rules()) {
            cuBool_Matrix ma = M.get(a);
            cuBool_Matrix mb = M.get(b);
            if (!ma || !mb) continue;

            cuBool_Index nb = 0;
            cuBool_Matrix_Nvals(mb, &nb);
            if (nb == 0) continue;

            // EWiseAdd не поддерживает result == left, используем временную матрицу
            cuBool_Matrix tmp;
            CB_CHECK(cuBool_Matrix_New(&tmp, n, n));
            CB_CHECK(cuBool_Matrix_EWiseAdd(tmp, ma, mb, CUBOOL_HINT_NO));
            M.replace(a, tmp);
        }

        cur_total = M.total_nvals();
        std::cout << "  итерация " << iter << ": nvals=" << cur_total
                  << " (+" << (int64_t)(cur_total - prev_total) << ")\n";
    }

    auto t1 = std::chrono::high_resolution_clock::now();
    double elapsed = std::chrono::duration<double>(t1 - t0).count();

    CflrResult result;
    result.start_nvals  = M.nvals_of(grammar.start_symbol());
    result.total_nvals  = cur_total;
    result.iterations   = iter;
    result.elapsed_secs = elapsed;

    std::cout << "\nСтартовый символ '" << grammar.start_symbol()
              << "': " << result.start_nvals << " достижимых пар\n"
              << "Время: " << elapsed << " сек, итераций: " << iter << "\n";

    return result;
}