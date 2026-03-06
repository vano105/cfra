#include "incremental_algo.hpp"
#include "../matrix_store/matrix_store.hpp"
#include "../common.hpp"

#include <cubool/cubool.h>
#include <unordered_map>
#include <chrono>
#include <stdexcept>

CflrResult run_cflr_incremental(const CnfGrammar& grammar,
                                const LabeledGraph& graph)
{
    auto t0 = std::chrono::high_resolution_clock::now();
    cuBool_Index n = static_cast<cuBool_Index>(graph.num_vertices());

    MatrixStore M(n);
    MatrixStore DM(n);

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
        M.ensure(nt);
    }

    int iter = 0;

    while (true) {
        iter++;

        MatrixStore tmp(n);

        for (auto& [a, b, c] : grammar.complex_rules()) {
            if (M.is_empty(b) || DM.is_empty(c)) continue;

            cuBool_Matrix mb = M.get(b);
            cuBool_Matrix dmc = DM.get(c);
            if (!mb || !dmc) continue;

            CB_CHECK(cuBool_MxM(tmp.ensure(a), mb, dmc, CUBOOL_HINT_ACCUMULATE));
            tmp.invalidate(a);
        }

        bool m_changed = false;

        for (auto& nt : grammar.nonterminals()) {
            if (DM.is_empty(nt)) continue;

            cuBool_Matrix dm = DM.get(nt);
            cuBool_Matrix ma = M.get(nt);
            if (!dm || !ma) continue;

            cuBool_Index old_nv = M.nvals_of(nt);

            cuBool_Matrix merged;
            CB_CHECK(cuBool_Matrix_New(&merged, n, n));
            CB_CHECK(cuBool_Matrix_EWiseAdd(merged, ma, dm, CUBOOL_HINT_NO));
            M.replace(nt, merged);

            cuBool_Index new_nv = M.nvals_of(nt);
            if (new_nv > old_nv) m_changed = true;
        }

        if (!m_changed) {
            break;
        }

        for (auto& [a, b, c] : grammar.complex_rules()) {
            if (DM.is_empty(b) || M.is_empty(c)) continue;

            cuBool_Matrix dmb = DM.get(b);
            cuBool_Matrix mc = M.get(c);
            if (!dmb || !mc) continue;

            CB_CHECK(cuBool_MxM(tmp.ensure(a), dmb, mc, CUBOOL_HINT_ACCUMULATE));
            tmp.invalidate(a);
        }

        for (auto& [a, b] : grammar.simple_rules()) {
            if (DM.is_empty(b)) continue;

            cuBool_Matrix dmb = DM.get(b);
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

            cuBool_Matrix t = tmp.get(nt);
            cuBool_Matrix ma = M.get(nt);
            if (!t || !ma) continue;

            cuBool_Matrix diff;
            CB_CHECK(cuBool_Matrix_New(&diff, n, n));
            CB_CHECK(cuBool_Matrix_EWiseMulInverted(diff, t, ma, CUBOOL_HINT_NO));

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