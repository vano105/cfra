#include "base_algo.hpp"
#include "../matrix_store/matrix_store.hpp"
#include "../common.hpp"

#include <cubool/cubool.h>
#include <unordered_map>
#include <chrono>
#include <stdexcept>

CflrResult run_cflr_non_incremental(const CnfGrammar& grammar, const LabeledGraph& graph)
{
    auto t0 = std::chrono::high_resolution_clock::now();
    cuBool_Index n = static_cast<cuBool_Index>(graph.num_vertices());
    MatrixStore M(n);

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
        M.invalidate(label);
    }

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
                M.invalidate(nt);
            }
        }
    }

    for (auto& nt : grammar.epsilon_rules()) {
        cuBool_Matrix m = M.ensure(nt);
        for (cuBool_Index i = 0; i < n; i++)
            CB_CHECK(cuBool_Matrix_SetElement(m, i, i));
        M.invalidate(nt);
    }

    for (auto& nt : grammar.nonterminals())
        M.ensure(nt);

    int iter = 0;
    uint64_t prev_total = 0;
    uint64_t cur_total = M.total_nvals();

    while (cur_total != prev_total) {
        prev_total = cur_total;
        iter++;

        for (auto& [a, b, c] : grammar.complex_rules()) {
            if (M.is_empty(b) || M.is_empty(c)) continue;

            cuBool_Matrix ma = M.get(a);
            cuBool_Matrix mb = M.get(b);
            cuBool_Matrix mc = M.get(c);
            if (!ma || !mb || !mc) continue;

            CB_CHECK(cuBool_MxM(ma, mb, mc, CUBOOL_HINT_ACCUMULATE));
            M.invalidate(a);
        }

        for (auto& [a, b] : grammar.simple_rules()) {
            if (M.is_empty(b)) continue;

            cuBool_Matrix ma = M.get(a);
            cuBool_Matrix mb = M.get(b);
            if (!ma || !mb) continue;

            cuBool_Matrix tmp;
            CB_CHECK(cuBool_Matrix_New(&tmp, n, n));
            CB_CHECK(cuBool_Matrix_EWiseAdd(tmp, ma, mb, CUBOOL_HINT_NO));
            M.replace(a, tmp);
        }

        cur_total = M.total_nvals();
    }

    auto t1 = std::chrono::high_resolution_clock::now();
    double elapsed = std::chrono::duration<double>(t1 - t0).count();

    CflrResult result;
    result.start_nvals  = M.nvals_of(grammar.start_symbol());
    result.total_nvals  = cur_total;
    result.iterations   = iter;
    result.elapsed_secs = elapsed;

    return result;
}