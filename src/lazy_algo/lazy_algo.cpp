#include "lazy_algo.hpp"
#include "../matrix_store/matrix_store.hpp"
#include "../common.hpp"
#include "chunked_store.hpp"

#include <algorithm>
#include <cmath>
#include <cubool/cubool.h>
#include <unordered_map>
#include <chrono>
#include <stdexcept>

CflrResult run_cflr_lazy(const CnfGrammar& grammar,
                         const LabeledGraph& graph)
{
    auto t0 = std::chrono::high_resolution_clock::now();
    cuBool_Index n = static_cast<cuBool_Index>(graph.num_vertices());

    double b = std::max(2.0, std::sqrt(static_cast<double>(n)));

    ChunkedStore M(n, b);
    MatrixStore DM(n);

    for (auto& [label, edges] : graph.edges_by_label()) {
        if (edges.empty()) continue;
        std::vector<cuBool_Index> rows(edges.size()), cols(edges.size());
        for (size_t i = 0; i < edges.size(); i++) {
            rows[i] = edges[i].src;
            cols[i] = edges[i].dst;
        }
        cuBool_Matrix m = M.ensure_single(label);
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
            cuBool_Matrix m = DM.ensure(nt);
            if (!rows.empty()) {
                CB_CHECK(cuBool_Matrix_Build(m, rows.data(), cols.data(),
                         static_cast<cuBool_Index>(rows.size()), CUBOOL_HINT_NO));
                DM.invalidate(nt);
            }
        }
    }

    for (auto& nt : grammar.epsilon_rules()) {
        cuBool_Matrix m = DM.ensure(nt);
        for (cuBool_Index i = 0; i < n; i++)
            CB_CHECK(cuBool_Matrix_SetElement(m, i, i));
        DM.invalidate(nt);
    }

    for (auto& nt : grammar.nonterminals()) {
        DM.ensure(nt);
        M.ensure_empty(nt);
    }

    int iter = 0;

    while (true) {
        iter++;

        MatrixStore tmp(n);

        for (auto& [a, b_sym, c] : grammar.complex_rules()) {
            if (M.is_empty(b_sym) || DM.is_empty(c)) continue;
            cuBool_Matrix dmc = DM.get(c);
            if (!dmc) continue;

            for (auto& chunk : M.chunks(b_sym)) {
                if (chunk.nvals == 0) continue;
                CB_CHECK(cuBool_MxM(tmp.ensure(a), chunk.matrix, dmc,
                         CUBOOL_HINT_ACCUMULATE));
                tmp.invalidate(a);
            }
        }

        bool m_changed = false;

        for (auto& nt : grammar.nonterminals()) {
            if (DM.is_empty(nt)) continue;

            cuBool_Matrix dm = DM.get(nt);
            cuBool_Index dm_nv = DM.nvals_of(nt);

            m_changed = true;

            cuBool_Matrix dm_copy;
            CB_CHECK(cuBool_Matrix_Duplicate(dm, &dm_copy));
            M.lazy_add(nt, dm_copy, dm_nv);
        }

        if (!m_changed) {
            break;
        }

        for (auto& [a, b_sym, c] : grammar.complex_rules()) {
            if (DM.is_empty(b_sym) || M.is_empty(c)) continue;
            cuBool_Matrix dmb = DM.get(b_sym);
            if (!dmb) continue;

            for (auto& chunk : M.chunks(c)) {
                if (chunk.nvals == 0) continue;
                CB_CHECK(cuBool_MxM(tmp.ensure(a), dmb, chunk.matrix,
                         CUBOOL_HINT_ACCUMULATE));
                tmp.invalidate(a);
            }
        }

        for (auto& [a, b_sym] : grammar.simple_rules()) {
            if (DM.is_empty(b_sym)) continue;
            cuBool_Matrix dmb = DM.get(b_sym);
            if (!dmb) continue;
            cuBool_Matrix ta = tmp.ensure(a);
            cuBool_Matrix merged;
            CB_CHECK(cuBool_Matrix_New(&merged, n, n));
            CB_CHECK(cuBool_Matrix_EWiseAdd(merged, ta, dmb, CUBOOL_HINT_NO));
            tmp.replace(a, merged);
        }

        DM.clear();
        uint64_t dm_total = 0;

        for (auto& nt : grammar.nonterminals()) {
            if (tmp.is_empty(nt)) continue;

            const auto& m_chunks = M.chunks(nt);

            cuBool_Matrix diff;
            CB_CHECK(cuBool_Matrix_Duplicate(tmp.get(nt), &diff));

            for (auto& chunk : m_chunks) {
                if (chunk.nvals == 0) continue;

                cuBool_Matrix new_diff;
                CB_CHECK(cuBool_Matrix_New(&new_diff, n, n));
                CB_CHECK(cuBool_Matrix_EWiseMulInverted(new_diff, diff,
                         chunk.matrix, CUBOOL_HINT_NO));
                cuBool_Matrix_Free(diff);
                diff = new_diff;
            }

            cuBool_Index dnv = 0;
            CB_CHECK(cuBool_Matrix_Nvals(diff, &dnv));

            if (dnv > 0) {
                DM.replace(nt, diff);
                dm_total += dnv;
            } else {
                cuBool_Matrix_Free(diff);
            }
        }

        if (dm_total == 0) {
            break;
        }
    }

    auto t1 = std::chrono::high_resolution_clock::now();
    double elapsed = std::chrono::duration<double>(t1 - t0).count();

    CflrResult result;
    result.start_nvals  = M.nvals_of(grammar.start_symbol());
    result.total_nvals  = M.total_nvals();
    result.iterations   = iter;
    result.elapsed_secs = elapsed;

    return result;
}