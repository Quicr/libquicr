// SPDX-FileCopyrightText: Copyright (c) 2026 Cisco Systems
// SPDX-License-Identifier: BSD-2-Clause

#ifndef QUICR_TEST_C_API_SCENARIOS_H
#define QUICR_TEST_C_API_SCENARIOS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

    typedef struct quicr_test_joining_server quicr_test_joining_server_t;
    typedef struct quicr_test_event quicr_test_event_t;

    typedef struct quicr_c_test_result
    {
        int line;
        const char* expression;
    } quicr_c_test_result_t;

#define QUICR_C_TEST_SUCCESS ((quicr_c_test_result_t){ 0, NULL })

    quicr_test_event_t* quicr_test_event_create(void);

    void quicr_test_event_destroy(quicr_test_event_t* event);

    void quicr_test_event_signal(quicr_test_event_t* event);

    bool quicr_test_event_wait(quicr_test_event_t* event, uint32_t timeout_ms);

    void quicr_test_sleep_ms(uint32_t duration_ms);

    quicr_c_test_result_t quicr_test_c_connection(const char* connect_uri);

    quicr_c_test_result_t quicr_test_c_publish_subscribe(const char* connect_uri,
                                                         int use_datagrams,
                                                         int use_forwarding);

    quicr_c_test_result_t quicr_test_c_fetch_namespace_status(const char* connect_uri);

    quicr_c_test_result_t quicr_test_c_rejections(const char* connect_uri);

    quicr_c_test_result_t quicr_test_c_server_fetch(const char* connect_uri, uint16_t server_port);

    quicr_c_test_result_t quicr_test_c_joining_server_start(uint16_t port, quicr_test_joining_server_t** server);

    int quicr_test_c_joining_server_wait(quicr_test_joining_server_t* server, uint32_t timeout_ms);

    int quicr_test_c_joining_server_send_namespace(quicr_test_joining_server_t* server);

    void quicr_test_c_joining_server_destroy(quicr_test_joining_server_t* server);

#ifdef __cplusplus
}
#endif

#endif // QUICR_TEST_C_API_SCENARIOS_H
