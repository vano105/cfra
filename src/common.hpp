#pragma once

#include <cstdint>
#include "cubool/cubool.h"

#define CB_CHECK(call)                                                          \
    do {                                                                        \
        cuBool_Status _s = (call);                                              \
        if (_s != CUBOOL_STATUS_SUCCESS) {                                      \
            throw std::runtime_error(std::string("Ошибка cuBool ") +            \
                std::to_string(_s) + " в " + __FILE__ + ":" +                   \
                std::to_string(__LINE__));                                       \
        }                                                                       \
    } while (0)

class CflrResult {
public:
    uint64_t start_nvals  = 0;  // кол-во достижимых пар для стартового символа
    uint64_t total_nvals  = 0;  // суммарное кол-во пар по всем нетерминалам
    int      iterations   = 0;  // кол-во итераций до сходимости
    double   elapsed_secs = 0;  // время работы в секундах
};
