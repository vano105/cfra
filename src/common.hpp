#pragma once

#define CB_CHECK(call)                                                          \
    do {                                                                        \
        cuBool_Status _s = (call);                                              \
        if (_s != CUBOOL_STATUS_SUCCESS) {                                      \
            throw std::runtime_error(std::string("Ошибка cuBool ") +            \
                std::to_string(_s) + " в " + __FILE__ + ":" +                   \
                std::to_string(__LINE__));                                       \
        }                                                                       \
    } while (0)
