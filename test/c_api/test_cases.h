// SPDX-FileCopyrightText: Copyright (c) 2026 Cisco Systems
// SPDX-License-Identifier: BSD-2-Clause

#ifndef QUICR_C_TEST_CASES_H
#define QUICR_C_TEST_CASES_H

#include <stddef.h>

#ifdef __cplusplus
extern "C"
{
#endif

    typedef struct quicr_c_test_result
    {
        int line;
        const char* expression;
    } quicr_c_test_result_t;

#define QUICR_C_TEST_CHECK(expression)                                                                                 \
    do {                                                                                                               \
        if (!(expression)) {                                                                                           \
            return (quicr_c_test_result_t){ __LINE__, #expression };                                                   \
        }                                                                                                              \
    } while (0)

#define QUICR_C_TEST_SUCCESS ((quicr_c_test_result_t){ 0, NULL })

    quicr_c_test_result_t quicr_c_test_names(void);
    quicr_c_test_result_t quicr_c_test_extensions(void);
    quicr_c_test_result_t quicr_c_test_initializers(void);
    quicr_c_test_result_t quicr_c_test_responses(void);
    quicr_c_test_result_t quicr_c_test_handlers(void);
    quicr_c_test_result_t quicr_c_test_generic_handlers(void);

#ifdef __cplusplus
}
#endif

#endif
