#pragma once

#include <cstdint>
#include "cubool/cubool.h"
#include <stdexcept>

#define CB_CHECK(call)                                                          \
    do {                                                                        \
        cuBool_Status _s = (call);                                              \
        if (_s != CUBOOL_STATUS_SUCCESS) {                                      \
            throw std::runtime_error(std::string("cuBool error ") +            \
                std::to_string(_s) + " at " + __FILE__ + ":" +                   \
                std::to_string(__LINE__));                                       \
        }                                                                       \
    } while (0)

class CflrResult {
public:
    uint64_t start_nvals  = 0;  // reachable pairs for start symbol
    uint64_t total_nvals  = 0;  // total pairs across all nonterminals
    int      iterations   = 0;  // iterations until convergence
    double   elapsed_secs = 0;  // wall-clock time in seconds
};