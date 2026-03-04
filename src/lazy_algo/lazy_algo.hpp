#pragma once

#include "../grammar/grammar.hpp"
#include "../graph/graph.hpp"
#include "../common.hpp"
#include <cstdint>

CflrResult run_cflr_lazy(const CnfGrammar& grammar,
                         const LabeledGraph& graph);