#pragma once

#include "grammar.hpp"
#include "graph.hpp"
#include "common.hpp"

#include <cstdint>

CflrResult run_cflr_non_incremental(const CnfGrammar& grammar,
                                    const LabeledGraph& graph);