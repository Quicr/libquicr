// SPDX-FileCopyrightText: Copyright (c) 2026 Cisco Systems
// SPDX-License-Identifier: BSD-2-Clause

#include "c_api_scenarios.h"

#include <quicr/quicr.h>

#include <stdatomic.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define QUICR_TEST_TIMEOUT_MS 10000U

#define SCENARIO_REQUIRE(condition)                                                                                    \
    do {                                                                                                               \
        if (!(condition)) {                                                                                            \
            result = (quicr_c_test_result_t){ __LINE__, #condition };                                                  \
            goto cleanup;                                                                                              \
        }                                                                                                              \
    } while (0)

typedef struct connection_context
{
    quicr_test_event_t* ready;
    quicr_test_event_t* removed;
    quicr_test_event_t* metrics;
    bool setup_received;
    quicr_status_t wrong_reply_kind_status;
    quicr_status_t reply_accept_status;
    quicr_status_t duplicate_reply_status;
} connection_context_t;

typedef struct track_context
{
    quicr_test_event_t* session_ready;
    quicr_test_event_t* ready;
    quicr_test_event_t* object;
    quicr_test_event_t* object_status;
    quicr_test_event_t* subgroup_ended;
    quicr_test_event_t* metrics;
    quicr_test_event_t* forwarding_started;
    quicr_test_event_t* forwarding_data;
    quicr_test_event_t* datagram;
    uint8_t received[64];
    size_t received_size;
    uint64_t received_group_id;
    uint64_t received_object_id;
    size_t received_extension_count;
    quicr_object_status_t received_status;
    size_t datagram_size;
    quicr_test_event_t* callback_entered;
    quicr_test_event_t* callback_release;
    quicr_test_event_t* callback_finished;
    atomic_bool block_callback;
    atomic_bool callback_release_timed_out;
} track_context_t;

typedef struct request_context
{
    quicr_test_event_t* session_ready;
    quicr_test_event_t* fetch_ready;
    quicr_test_event_t* fetch_object;
    quicr_test_event_t* namespace_ready;
    quicr_test_event_t* namespace_received;
    quicr_test_event_t* publish_namespace_ready;
    quicr_test_event_t* track_status_ready;
    size_t expected_fetch_size;
    size_t fetch_size;
    size_t namespace_count;
} request_context_t;

typedef struct server_fetch_context
{
    quicr_test_event_t* client_ready;
    quicr_test_event_t* fetch_rejected;
    quicr_test_event_t* fetch_ready;
    quicr_test_event_t* fetch_object;
    quicr_test_event_t* fetch_object_status;
    quicr_test_event_t* namespace_request;
    quicr_test_event_t* namespace_rejected;
    quicr_test_event_t* namespace_accepted;
    quicr_test_event_t* server_session_ready;
    quicr_session_t* server_session;
    quicr_publish_fetch_handler_t* publish_fetch_handler;
    size_t fetch_request_count;
    size_t fetch_size;
    quicr_fetch_track_handler_status_t fetch_status;
    size_t expected_fetch_size;
    quicr_object_status_t object_status;
    size_t namespace_request_count;
} server_fetch_context_t;

typedef struct rejection_context
{
    quicr_test_event_t* publish_session_ready;
    quicr_test_event_t* subscribe_session_ready;
    quicr_test_event_t* publish_rejected;
    quicr_test_event_t* subscribe_rejected;
    quicr_test_event_t* track_status_rejected;
    quicr_publish_track_handler_status_t publish_status;
    quicr_subscribe_track_handler_status_t subscribe_status;
    quicr_track_status_handler_status_t track_status;
} rejection_context_t;

struct quicr_test_joining_server
{
    quicr_session_manager_t* manager;
    quicr_test_event_t* subscribe_received;
    quicr_test_event_t* subscribe_tracks_received;
    quicr_test_event_t* joining_received;
    quicr_test_event_t* publish_done_received;
    quicr_test_event_t* unsubscribe_received;
    quicr_test_event_t* publish_namespace_done_received;
    quicr_test_event_t* unsubscribe_namespace_received;
    quicr_session_t* relay_session;
    quicr_publish_track_handler_t* relay_publish_handler;
    quicr_subscribe_track_handler_t* received_publish_handler;
    quicr_publish_response_t* publish_response;
    uint64_t relay_request_id;
};

static void
accept_setup(quicr_session_t* session,
             const quicr_setup_attributes_t* attributes,
             quicr_reply_t* reply,
             void* user_data)
{
    connection_context_t* context = user_data;
    (void)session;
    (void)attributes;

    if (context != NULL) {
        context->setup_received = true;
        const quicr_fetch_response_t response = { 0 };
        context->wrong_reply_kind_status = quicr_reply_accept_fetch(reply, &response);
        context->reply_accept_status = quicr_reply_accept(reply);
        context->duplicate_reply_status = quicr_reply_reject(reply, QUICR_ERROR_INTERNAL_ERROR, "duplicate", NULL);
        return;
    }

    (void)quicr_reply_accept(reply);
}

static void
accept_track_setup(quicr_session_t* session,
                   const quicr_setup_attributes_t* attributes,
                   quicr_reply_t* reply,
                   void* user_data)
{
    (void)session;
    (void)attributes;
    (void)user_data;

    (void)quicr_reply_accept(reply);
}

static void
connection_status_changed(quicr_session_t* session, quicr_session_status_t status, void* user_data)
{
    connection_context_t* context = user_data;
    (void)session;

    if (context != NULL && status == QUICR_SESSION_STATUS_READY) {
        quicr_test_event_signal(context->ready);
    }
}

static void
track_session_status_changed(quicr_session_t* session, quicr_session_status_t status, void* user_data)
{
    track_context_t* context = user_data;
    (void)session;

    if (context != NULL && status == QUICR_SESSION_STATUS_READY) {
        quicr_test_event_signal(context->session_ready);
    }
}

static void
connection_metrics_sampled(quicr_session_t* session, const quicr_connection_metrics_t* metrics, void* user_data)
{
    connection_context_t* context = user_data;
    (void)session;

    if (context != NULL && metrics != NULL) {
        quicr_test_event_signal(context->metrics);
    }
}

static void
session_removed(quicr_session_manager_t* manager, quicr_session_t* session, void* user_data)
{
    connection_context_t* context = user_data;
    (void)manager;
    (void)session;

    if (context != NULL) {
        quicr_test_event_signal(context->removed);
    }
}

static void
publish_status_changed(quicr_publish_track_handler_t* handler,
                       quicr_publish_track_handler_status_t status,
                       void* user_data)
{
    track_context_t* context = user_data;
    (void)handler;

    if (context != NULL && status == QUICR_PUBLISH_TRACK_HANDLER_STATUS_OK) {
        quicr_test_event_signal(context->ready);
    }
}

static void
subscribe_status_changed(quicr_subscribe_track_handler_t* handler,
                         quicr_subscribe_track_handler_status_t status,
                         void* user_data)
{
    track_context_t* context = user_data;
    (void)handler;

    if (context != NULL && status == QUICR_SUBSCRIBE_TRACK_HANDLER_STATUS_OK) {
        quicr_test_event_signal(context->ready);
    }
}

static void
publish_metrics_sampled(quicr_publish_track_handler_t* handler,
                        const quicr_publish_track_metrics_t* metrics,
                        void* user_data)
{
    track_context_t* context = user_data;
    (void)handler;

    if (context != NULL && metrics != NULL) {
        quicr_test_event_signal(context->metrics);
    }
}

static void
subscribe_metrics_sampled(quicr_subscribe_track_handler_t* handler,
                          const quicr_subscribe_track_metrics_t* metrics,
                          void* user_data)
{
    track_context_t* context = user_data;
    (void)handler;

    if (context != NULL && metrics != NULL) {
        quicr_test_event_signal(context->metrics);
    }
}

static void
object_received(const quicr_object_headers_t* headers,
                quicr_byte_span_t data,
                const quicr_stream_header_properties_t* properties,
                void* user_data)
{
    track_context_t* context = user_data;
    (void)properties;

    if (context == NULL || headers == NULL || data.size > sizeof(context->received)) {
        return;
    }

    if (data.size != 0U) {
        memcpy(context->received, data.data, data.size);
    }
    context->received_size = data.size;
    context->received_group_id = headers->group_id;
    context->received_object_id = headers->object_id;
    context->received_extension_count = headers->extensions == NULL ? 0U : headers->extensions->count;
    context->received_status = headers->status;
    if (atomic_load(&context->block_callback)) {
        // Both the scenario thread and the callback-releaser thread must observe entry.
        quicr_test_event_signal(context->callback_entered);
        quicr_test_event_signal(context->callback_entered);
        if (!quicr_test_event_wait(context->callback_release, QUICR_TEST_TIMEOUT_MS)) {
            atomic_store(&context->callback_release_timed_out, true);
        }
        quicr_test_event_signal(context->callback_finished);
    }
    quicr_test_event_signal(context->object);
}

static void
object_status_received(const quicr_object_headers_t* headers, void* user_data)
{
    track_context_t* context = user_data;
    if (context == NULL || headers == NULL) {
        return;
    }

    context->received_group_id = headers->group_id;
    context->received_object_id = headers->object_id;
    context->received_status = headers->status;
    quicr_test_event_signal(context->object_status);
}

static void
subgroup_ended(uint64_t group_id, uint64_t subgroup_id, bool reset, void* user_data)
{
    track_context_t* context = user_data;
    (void)group_id;
    (void)subgroup_id;

    if (context != NULL && !reset) {
        quicr_test_event_signal(context->subgroup_ended);
    }
}

static void
forwarding_subgroup_started(uint64_t group_id,
                            uint64_t subgroup_id,
                            bool has_priority,
                            uint8_t priority,
                            const quicr_stream_header_properties_t* properties,
                            void* user_data)
{
    track_context_t* context = user_data;
    (void)group_id;
    (void)subgroup_id;
    (void)has_priority;
    (void)priority;
    (void)properties;

    if (context != NULL) {
        quicr_test_event_signal(context->forwarding_started);
    }
}

static void
forwarding_stream_bytes(uint64_t group_id, uint64_t subgroup_id, quicr_byte_span_t data, void* user_data)
{
    track_context_t* context = user_data;
    (void)group_id;
    (void)subgroup_id;

    if (context != NULL && data.size != 0U) {
        quicr_test_event_signal(context->forwarding_data);
    }
}

static void
datagram_received(quicr_byte_span_t data, void* user_data)
{
    track_context_t* context = user_data;
    if (context != NULL && data.size != 0U) {
        context->datagram_size = data.size;
        quicr_test_event_signal(context->datagram);
    }
}

static void
request_session_status_changed(quicr_session_t* session, quicr_session_status_t status, void* user_data)
{
    request_context_t* context = user_data;
    (void)session;

    if (context != NULL && status == QUICR_SESSION_STATUS_READY) {
        quicr_test_event_signal(context->session_ready);
    }
}

static void
fetch_status_changed(quicr_fetch_track_handler_t* handler, quicr_fetch_track_handler_status_t status, void* user_data)
{
    request_context_t* context = user_data;
    (void)handler;

    if (context != NULL && status == QUICR_SUBSCRIBE_TRACK_HANDLER_STATUS_OK) {
        quicr_test_event_signal(context->fetch_ready);
    }
}

static void
fetch_object_received(const quicr_object_headers_t* headers,
                      quicr_byte_span_t data,
                      const quicr_stream_header_properties_t* properties,
                      void* user_data)
{
    request_context_t* context = user_data;
    (void)headers;
    (void)properties;

    if (context != NULL && data.size != 0U) {
        context->fetch_size += data.size;
        if (context->fetch_size >= context->expected_fetch_size) {
            quicr_test_event_signal(context->fetch_object);
        }
    }
}

static void
namespace_status_changed(quicr_subscribe_namespace_handler_t* handler,
                         quicr_subscribe_namespace_handler_status_t status,
                         void* user_data)
{
    request_context_t* context = user_data;
    (void)handler;

    if (context != NULL && status == QUICR_SUBSCRIBE_NAMESPACE_HANDLER_STATUS_OK) {
        quicr_test_event_signal(context->namespace_ready);
    }
}

static void
namespace_received(quicr_subscribe_namespace_handler_t* handler,
                   const quicr_track_namespace_t* track_namespace,
                   void* user_data)
{
    request_context_t* context = user_data;
    (void)handler;

    if (context != NULL && track_namespace != NULL) {
        context->namespace_count = quicr_track_namespace_count(track_namespace);
        quicr_test_event_signal(context->namespace_received);
    }
}

static void
publish_namespace_status_changed(quicr_publish_namespace_handler_t* handler,
                                 quicr_publish_namespace_handler_status_t status,
                                 void* user_data)
{
    request_context_t* context = user_data;
    (void)handler;

    if (context != NULL && status == QUICR_PUBLISH_NAMESPACE_HANDLER_STATUS_OK) {
        quicr_test_event_signal(context->publish_namespace_ready);
    }
}

static void
track_status_changed(quicr_track_status_track_handler_t* handler,
                     quicr_track_status_handler_status_t status,
                     void* user_data)
{
    request_context_t* context = user_data;
    (void)handler;

    if (context != NULL && status == QUICR_TRACK_STATUS_HANDLER_STATUS_OK) {
        quicr_test_event_signal(context->track_status_ready);
    }
}

static void
rejection_session_status_changed(quicr_session_t* session, quicr_session_status_t status, void* user_data)
{
    quicr_test_event_t* session_ready = user_data;
    (void)session;

    if (status == QUICR_SESSION_STATUS_READY) {
        quicr_test_event_signal(session_ready);
    }
}

static void
publish_rejected(quicr_publish_track_handler_t* handler, quicr_publish_track_handler_status_t status, void* user_data)
{
    rejection_context_t* context = user_data;
    (void)handler;

    if (context == NULL) {
        return;
    }
    context->publish_status = status;
    if (status == QUICR_PUBLISH_TRACK_HANDLER_STATUS_ANNOUNCE_NOT_AUTHORIZED ||
        status == QUICR_PUBLISH_TRACK_HANDLER_STATUS_DONE_BY_FIN ||
        status == QUICR_PUBLISH_TRACK_HANDLER_STATUS_UNSUBSCRIBED) {
        quicr_test_event_signal(context->publish_rejected);
    }
}

static void
subscribe_rejected(quicr_subscribe_track_handler_t* handler,
                   quicr_subscribe_track_handler_status_t status,
                   void* user_data)
{
    rejection_context_t* context = user_data;
    (void)handler;

    if (context == NULL) {
        return;
    }
    context->subscribe_status = status;
    if (status == QUICR_SUBSCRIBE_TRACK_HANDLER_STATUS_NOT_AUTHORIZED ||
        status == QUICR_SUBSCRIBE_TRACK_HANDLER_STATUS_ERROR ||
        status == QUICR_SUBSCRIBE_TRACK_HANDLER_STATUS_DONE_BY_FIN ||
        status == QUICR_SUBSCRIBE_TRACK_HANDLER_STATUS_DONE_BY_RESET ||
        status == QUICR_SUBSCRIBE_TRACK_HANDLER_STATUS_CANCELLED) {
        quicr_test_event_signal(context->subscribe_rejected);
    }
}

static void
track_status_rejected(quicr_track_status_track_handler_t* handler,
                      quicr_track_status_handler_status_t status,
                      void* user_data)
{
    rejection_context_t* context = user_data;
    (void)handler;

    if (context == NULL) {
        return;
    }
    context->track_status = status;
    if (status == QUICR_TRACK_STATUS_HANDLER_STATUS_ERROR) {
        quicr_test_event_signal(context->track_status_rejected);
    }
}

static void
server_fetch_new_session(quicr_session_manager_t* manager, quicr_session_t* session, void* user_data)
{
    server_fetch_context_t* context = user_data;
    (void)manager;

    if (context != NULL) {
        context->server_session = session;
        quicr_test_event_signal(context->server_session_ready);
    }
}

static void
server_fetch_client_status(quicr_session_t* session, quicr_session_status_t status, void* user_data)
{
    server_fetch_context_t* context = user_data;
    (void)session;

    if (context != NULL && status == QUICR_SESSION_STATUS_READY) {
        quicr_test_event_signal(context->client_ready);
    }
}

static void
server_fetch_status_changed(quicr_fetch_track_handler_t* handler,
                            quicr_fetch_track_handler_status_t status,
                            void* user_data)
{
    server_fetch_context_t* context = user_data;
    (void)handler;

    if (context == NULL) {
        return;
    }
    context->fetch_status = status;
    if (status == QUICR_SUBSCRIBE_TRACK_HANDLER_STATUS_OK) {
        quicr_test_event_signal(context->fetch_ready);
    } else if (status != QUICR_SUBSCRIBE_TRACK_HANDLER_STATUS_NOT_CONNECTED &&
               status != QUICR_SUBSCRIBE_TRACK_HANDLER_STATUS_PENDING_RESPONSE) {
        quicr_test_event_signal(context->fetch_rejected);
    }
}

static void
server_fetch_object_received(const quicr_object_headers_t* headers,
                             quicr_byte_span_t data,
                             const quicr_stream_header_properties_t* properties,
                             void* user_data)
{
    server_fetch_context_t* context = user_data;
    (void)headers;
    (void)properties;

    if (context != NULL && data.size != 0U) {
        context->fetch_size += data.size;
        if (context->fetch_size >= context->expected_fetch_size) {
            quicr_test_event_signal(context->fetch_object);
        }
    }
}

static void
server_fetch_object_status_received(const quicr_object_headers_t* headers, void* user_data)
{
    server_fetch_context_t* context = user_data;
    if (context != NULL && headers != NULL) {
        context->object_status = headers->status;
        quicr_test_event_signal(context->fetch_object_status);
    }
}

static void
server_standalone_fetch_received(quicr_session_t* session,
                                 uint64_t request_id,
                                 const quicr_full_track_name_t* full_track_name,
                                 const quicr_standalone_fetch_attributes_t* attributes,
                                 quicr_reply_t* reply,
                                 void* user_data)
{
    static const uint8_t payload[] = { 4U, 5U, 6U };
    server_fetch_context_t* context = user_data;
    (void)attributes;

    if (context == NULL) {
        (void)quicr_reply_reject(reply, QUICR_FETCH_ERROR_INTERNAL_ERROR, "missing test context", NULL);
        return;
    }

    ++context->fetch_request_count;
    if (context->fetch_request_count == 1U) {
        (void)quicr_reply_reject(reply, QUICR_FETCH_ERROR_INVALID_RANGE, "intentional rejection", NULL);
        return;
    }

    const quicr_status_t create_status = quicr_publish_fetch_handler_create(
      NULL, full_track_name, 0U, request_id, QUICR_GROUP_ORDER_ASCENDING, 1000U, &context->publish_fetch_handler);
    if (create_status != QUICR_STATUS_OK ||
        quicr_session_bind_publish_fetch_handler(session, context->publish_fetch_handler) != QUICR_STATUS_OK) {
        (void)quicr_reply_reject(reply, QUICR_FETCH_ERROR_INTERNAL_ERROR, "failed to bind response", NULL);
        return;
    }

    quicr_object_headers_t headers;
    quicr_object_headers_init(&headers);
    headers.group_id = 1U;
    headers.object_id = 2U;
    headers.payload_length = sizeof(payload);
    const quicr_byte_span_t data = { payload, sizeof(payload) };
    quicr_publish_object_status_t publish_status = QUICR_PUBLISH_OBJECT_STATUS_INTERNAL_ERROR;
    if (quicr_publish_fetch_handler_publish_object(context->publish_fetch_handler, &headers, data, &publish_status) !=
          QUICR_STATUS_OK ||
        publish_status != QUICR_PUBLISH_OBJECT_STATUS_OK) {
        (void)quicr_reply_reject(reply, QUICR_FETCH_ERROR_INTERNAL_ERROR, "failed to publish response", NULL);
        return;
    }

    const quicr_fetch_response_t response = {
        .largest_location = { .group = 1U, .object = 2U },
        .publisher_default_group_order = QUICR_GROUP_ORDER_ASCENDING,
        .has_largest_location = true,
    };
    (void)quicr_reply_accept_fetch(reply, &response);
}

static void
joining_fetch_rejected(quicr_session_t* session,
                       uint64_t request_id,
                       const quicr_full_track_name_t* full_track_name,
                       const quicr_joining_fetch_attributes_t* attributes,
                       quicr_reply_t* reply,
                       void* user_data)
{
    quicr_test_joining_server_t* server = user_data;
    (void)session;
    (void)request_id;
    (void)full_track_name;
    (void)attributes;

    if (server != NULL) {
        quicr_test_event_signal(server->joining_received);
    }

    (void)quicr_reply_reject(reply, QUICR_FETCH_ERROR_INVALID_RANGE, "intentional joining-fetch rejection", NULL);
}

static bool
validate_joining_subscribe_attributes(const quicr_subscribe_attributes_t* attributes)
{
    quicr_filter_t filter = { 0 };
    if (quicr_subscribe_attributes_filter(attributes, &filter) != QUICR_STATUS_OK ||
        quicr_subscribe_attributes_filter(NULL, &filter) != QUICR_STATUS_INVALID_ARGUMENT ||
        quicr_subscribe_attributes_filter(attributes, NULL) != QUICR_STATUS_INVALID_ARGUMENT) {
        return false;
    }

    quicr_group_order_t group_order = QUICR_GROUP_ORDER_UNSPECIFIED;
    if (!quicr_subscribe_attributes_group_order(attributes, &group_order) ||
        group_order != QUICR_GROUP_ORDER_ASCENDING || quicr_subscribe_attributes_group_order(attributes, NULL)) {
        return false;
    }

    uint64_t new_group_request_id = UINT64_MAX;
    if (quicr_subscribe_attributes_new_group_request_id(attributes, &new_group_request_id) ||
        new_group_request_id != UINT64_MAX || quicr_subscribe_attributes_new_group_request_id(attributes, NULL)) {
        return false;
    }

    return filter.type == QUICR_FILTER_TYPE_SUBSCRIPTION_FILTER &&
           filter.subscription.type == QUICR_SUBSCRIPTION_FILTER_TYPE_ABSOLUTE_RANGE &&
           filter.subscription.start_location.group == 2U && filter.subscription.start_location.object == 3U &&
           filter.subscription.end_group_delta == 4U;
}

static void
joining_subscribe_received(quicr_session_t* session,
                           uint64_t request_id,
                           const quicr_full_track_name_t* full_track_name,
                           const quicr_subscribe_attributes_t* attributes,
                           quicr_reply_t* reply,
                           void* user_data)
{
    quicr_test_joining_server_t* server = user_data;
    if (server == NULL || !validate_joining_subscribe_attributes(attributes)) {
        (void)quicr_reply_reject(reply, QUICR_REQUEST_ERROR_INTERNAL_ERROR, "failed to prepare relay publisher", NULL);
        return;
    }

    if (quicr_publish_track_handler_create(
          NULL, full_track_name, QUICR_TRACK_MODE_STREAM, 0U, 0U, 1000U, NULL, &server->relay_publish_handler) !=
        QUICR_STATUS_OK) {
        (void)quicr_reply_reject(reply, QUICR_REQUEST_ERROR_INTERNAL_ERROR, "failed to prepare relay publisher", NULL);
        return;
    }
    if (quicr_session_bind_publish_track_handler(session, 17U, request_id, server->relay_publish_handler, true) !=
        QUICR_STATUS_OK) {
        (void)quicr_reply_reject(reply, QUICR_REQUEST_ERROR_INTERNAL_ERROR, "failed to prepare relay publisher", NULL);
        return;
    }

    server->relay_session = session;
    server->relay_request_id = request_id;
    quicr_test_event_signal(server->subscribe_received);
    const quicr_subscribe_response_t response = { 0 };
    (void)quicr_reply_accept_subscribe(reply, &response);
}

static bool
validate_joining_publish_attributes(const quicr_publish_attributes_t* attributes)
{
    uint64_t expires = UINT64_MAX;
    if (quicr_publish_attributes_expires(attributes, &expires) || expires != UINT64_MAX ||
        quicr_publish_attributes_expires(attributes, NULL)) {
        return false;
    }

    quicr_location_t largest_object = { UINT64_MAX, UINT64_MAX };
    if (!quicr_publish_attributes_largest_object(attributes, &largest_object) || largest_object.group != 0U ||
        largest_object.object != 0U || quicr_publish_attributes_largest_object(attributes, NULL)) {
        return false;
    }

    uint64_t max_cache_duration = UINT64_MAX;
    if (quicr_publish_attributes_max_cache_duration(attributes, &max_cache_duration) ||
        max_cache_duration != UINT64_MAX || quicr_publish_attributes_max_cache_duration(attributes, NULL)) {
        return false;
    }

    uint64_t delivery_timeout = UINT64_MAX;
    return quicr_publish_attributes_delivery_timeout(attributes, &delivery_timeout) && delivery_timeout == 1000U &&
           !quicr_publish_attributes_delivery_timeout(attributes, NULL);
}

static void
joining_publish_received(quicr_session_t* session,
                         uint64_t request_id,
                         const quicr_publish_attributes_t* attributes,
                         const quicr_subscribe_namespace_handler_t* handler,
                         quicr_reply_t* reply,
                         void* user_data)
{
    quicr_test_joining_server_t* server = user_data;
    const quicr_full_track_name_t* full_track_name = quicr_publish_attributes_full_track_name(attributes);
    (void)session;
    (void)request_id;
    (void)handler;

    if (server == NULL || full_track_name == NULL || !validate_joining_publish_attributes(attributes)) {
        (void)quicr_reply_reject(reply, QUICR_PUBLISH_ERROR_INTERNAL_ERROR, "failed to create publish response", NULL);
        return;
    }

    if (quicr_subscribe_track_handler_create(
          NULL, full_track_name, 0U, QUICR_GROUP_ORDER_UNSPECIFIED, NULL, &server->received_publish_handler) !=
        QUICR_STATUS_OK) {
        (void)quicr_reply_reject(reply, QUICR_PUBLISH_ERROR_INTERNAL_ERROR, "failed to create publish response", NULL);
        return;
    }
    if (quicr_publish_response_create(server->received_publish_handler, &server->publish_response) != QUICR_STATUS_OK) {
        (void)quicr_reply_reject(reply, QUICR_PUBLISH_ERROR_INTERNAL_ERROR, "failed to create publish response", NULL);
        return;
    }

    (void)quicr_reply_accept_publish(reply, server->publish_response);
}

static void
joining_publish_done_received(quicr_session_t* session, uint64_t request_id, void* user_data)
{
    quicr_test_joining_server_t* server = user_data;
    (void)session;
    (void)request_id;

    if (server != NULL) {
        quicr_test_event_signal(server->publish_done_received);
    }
}

static void
joining_unsubscribe_received(quicr_session_t* session, uint64_t request_id, void* user_data)
{
    quicr_test_joining_server_t* server = user_data;
    (void)session;

    if (server != NULL && request_id == server->relay_request_id) {
        quicr_test_event_signal(server->unsubscribe_received);
    }
}

static void
joining_publish_namespace_done_received(quicr_session_t* session, uint64_t request_id, void* user_data)
{
    quicr_test_joining_server_t* server = user_data;
    (void)session;
    (void)request_id;

    if (server != NULL) {
        quicr_test_event_signal(server->publish_namespace_done_received);
    }
}

static void
joining_unsubscribe_namespace_received(quicr_session_t* session,
                                       const quicr_track_namespace_t* track_namespace,
                                       void* user_data)
{
    quicr_test_joining_server_t* server = user_data;
    (void)session;

    if (server != NULL && track_namespace != NULL) {
        quicr_test_event_signal(server->unsubscribe_namespace_received);
    }
}

static void
joining_subscribe_tracks_received(quicr_session_t* session,
                                  const quicr_track_namespace_t* prefix_namespace,
                                  const quicr_subscribe_namespace_attributes_t* attributes,
                                  quicr_reply_t* reply,
                                  void* user_data)
{
    quicr_test_joining_server_t* server = user_data;
    quicr_filter_type_t filter_type = QUICR_FILTER_TYPE_NONE;
    quicr_filter_t filter = { 0 };
    (void)session;
    (void)prefix_namespace;

    if (server != NULL &&
        quicr_subscribe_namespace_attributes_filter_type(attributes, &filter_type) == QUICR_STATUS_OK &&
        quicr_subscribe_namespace_attributes_filter(attributes, &filter) == QUICR_STATUS_OK &&
        quicr_subscribe_namespace_attributes_filter_type(NULL, &filter_type) == QUICR_STATUS_INVALID_ARGUMENT &&
        quicr_subscribe_namespace_attributes_filter_type(attributes, NULL) == QUICR_STATUS_INVALID_ARGUMENT &&
        quicr_subscribe_namespace_attributes_filter(NULL, &filter) == QUICR_STATUS_INVALID_ARGUMENT &&
        quicr_subscribe_namespace_attributes_filter(attributes, NULL) == QUICR_STATUS_INVALID_ARGUMENT &&
        filter_type == QUICR_FILTER_TYPE_TRACK_FILTER && filter.type == QUICR_FILTER_TYPE_TRACK_FILTER &&
        filter.track.property_type == 5U && filter.track.max_tracks_selected == 6U && filter.track.timeout == 7U) {
        quicr_test_event_signal(server->subscribe_tracks_received);
    }

    (void)quicr_reply_reject(reply, QUICR_REQUEST_ERROR_GOING_AWAY, "going away conversion test", NULL);
}

static void
server_publish_namespace_received(quicr_session_t* session,
                                  const quicr_track_namespace_t* track_namespace,
                                  const quicr_publish_namespace_attributes_t* attributes,
                                  quicr_reply_t* reply,
                                  void* user_data)
{
    server_fetch_context_t* context = user_data;
    (void)session;
    (void)track_namespace;
    (void)attributes;

    if (context == NULL) {
        (void)quicr_reply_reject(
          reply, QUICR_PUBLISH_NAMESPACE_ERROR_INTERNAL_ERROR, "intentional namespace rejection", NULL);
        return;
    }

    quicr_test_event_signal(context->namespace_request);
    if (++context->namespace_request_count == 1U) {
        (void)quicr_reply_reject(
          reply, QUICR_PUBLISH_NAMESPACE_ERROR_INTERNAL_ERROR, "intentional namespace rejection", NULL);
        return;
    }

    (void)quicr_reply_accept(reply);
}

static void
server_namespace_status_changed(quicr_publish_namespace_handler_t* handler,
                                quicr_publish_namespace_handler_status_t status,
                                void* user_data)
{
    server_fetch_context_t* context = user_data;
    (void)handler;

    if (context == NULL) {
        return;
    }
    if (status == QUICR_PUBLISH_NAMESPACE_HANDLER_STATUS_OK) {
        quicr_test_event_signal(context->namespace_accepted);
    } else if (status == QUICR_PUBLISH_NAMESPACE_HANDLER_STATUS_ERROR) {
        quicr_test_event_signal(context->namespace_rejected);
    }
}

static quicr_status_t
create_track_name(const char* track_data, quicr_full_track_name_t** full_track_name)
{
    static const uint8_t namespace_data[] = "c-api";
    const quicr_byte_span_t namespace_entry = { namespace_data, sizeof(namespace_data) - 1U };
    const quicr_byte_span_t track_name = { (const uint8_t*)track_data, strlen(track_data) };
    quicr_track_namespace_t* track_namespace = NULL;

    quicr_status_t status = quicr_track_namespace_create(&namespace_entry, 1U, &track_namespace);
    if (status == QUICR_STATUS_OK) {
        status = quicr_full_track_name_create(track_namespace, track_name, full_track_name);
    }
    quicr_track_namespace_destroy(track_namespace);
    return status;
}

quicr_c_test_result_t
quicr_test_c_connection(const char* connect_uri)
{
    quicr_c_test_result_t result = QUICR_C_TEST_SUCCESS;
    connection_context_t context = { 0 };
    quicr_session_manager_t* manager = NULL;
    quicr_session_t* session = NULL;
    quicr_session_status_t status = QUICR_SESSION_STATUS_NOT_CONNECTED;

    context.ready = quicr_test_event_create();
    context.removed = quicr_test_event_create();
    context.metrics = quicr_test_event_create();
    SCENARIO_REQUIRE(context.ready != NULL && context.removed != NULL && context.metrics != NULL);

    quicr_session_manager_callbacks_t manager_callbacks = {
        .user_data = &context,
        .session_removed = session_removed,
    };
    SCENARIO_REQUIRE(quicr_session_manager_create(&manager_callbacks, &manager) == QUICR_STATUS_OK);
    memset(&manager_callbacks, 0, sizeof(manager_callbacks));

    quicr_client_config_t config;
    quicr_client_config_init(&config);
    config.endpoint_id = "c-api-client";
    config.connect_uri = connect_uri;
#ifndef QUICR_TEST_NO_CERT_VERIFIER
    config.transport_config.tls_root_trust_filename = "server-cert.pem";
#endif
    config.transport_config.metrics_sample_ms = 10U;

    quicr_session_client_callbacks_t callbacks = {
        .user_data = &context,
        .setup_received = accept_setup,
        .status_changed = connection_status_changed,
        .metrics_sampled = connection_metrics_sampled,
    };
    SCENARIO_REQUIRE(quicr_session_manager_add_client(manager, &config, &callbacks, &session) == QUICR_STATUS_OK);
    memset(&callbacks, 0, sizeof(callbacks));
    SCENARIO_REQUIRE(quicr_test_event_wait(context.ready, QUICR_TEST_TIMEOUT_MS));
    SCENARIO_REQUIRE(context.setup_received);
    SCENARIO_REQUIRE(context.wrong_reply_kind_status == QUICR_STATUS_INVALID_ARGUMENT);
    SCENARIO_REQUIRE(context.reply_accept_status == QUICR_STATUS_OK);
    SCENARIO_REQUIRE(context.duplicate_reply_status == QUICR_STATUS_INVALID_ARGUMENT);
    SCENARIO_REQUIRE(quicr_session_get_status(session, &status) == QUICR_STATUS_OK);
    SCENARIO_REQUIRE(status == QUICR_SESSION_STATUS_READY);
    SCENARIO_REQUIRE(quicr_test_event_wait(context.metrics, QUICR_TEST_TIMEOUT_MS));

    SCENARIO_REQUIRE(quicr_session_disconnect(session) == QUICR_STATUS_OK);
    SCENARIO_REQUIRE(quicr_test_event_wait(context.removed, QUICR_TEST_TIMEOUT_MS));
    session = NULL;

    config.endpoint_id = "c-api-goaway-client";
    callbacks.user_data = &context;
    callbacks.setup_received = accept_setup;
    callbacks.status_changed = connection_status_changed;
    callbacks.metrics_sampled = connection_metrics_sampled;
    SCENARIO_REQUIRE(quicr_session_manager_add_client(manager, &config, &callbacks, &session) == QUICR_STATUS_OK);
    memset(&callbacks, 0, sizeof(callbacks));
    SCENARIO_REQUIRE(quicr_test_event_wait(context.ready, QUICR_TEST_TIMEOUT_MS));
    SCENARIO_REQUIRE(quicr_session_goaway(session, NULL, 0U, NULL) == QUICR_STATUS_OK);
    SCENARIO_REQUIRE(quicr_session_disconnect(session) == QUICR_STATUS_OK);
    SCENARIO_REQUIRE(quicr_test_event_wait(context.removed, QUICR_TEST_TIMEOUT_MS));
    session = NULL;

cleanup:
    quicr_session_manager_destroy(manager);
    quicr_test_event_destroy(context.metrics);
    quicr_test_event_destroy(context.removed);
    quicr_test_event_destroy(context.ready);
    return result;
}

static quicr_c_test_result_t
run_publish_subscribe(const char* connect_uri,
                      const int use_datagrams,
                      const int use_forwarding,
                      quicr_test_event_t* callback_entered,
                      quicr_test_event_t* callback_release,
                      quicr_test_event_t* callback_finished,
                      quicr_test_event_t* destroy_started,
                      quicr_test_event_t* destroy_returned)
{
    static const uint8_t payload[] = "C API integration payload";
    const char* track_name = "integration-stream";
    if (use_datagrams) {
        track_name = "integration-datagram";
    } else if (use_forwarding) {
        track_name = "integration-forwarding";
    } else if (strncmp(connect_uri, "moqt://", sizeof("moqt://") - 1U) == 0) {
        track_name = "integration-webtransport";
    }

    quicr_c_test_result_t result = QUICR_C_TEST_SUCCESS;
    track_context_t publisher = { 0 };
    track_context_t subscriber = { 0 };
    quicr_session_manager_t* manager = NULL;
    quicr_session_t* publish_session = NULL;
    quicr_session_t* subscribe_session = NULL;
    quicr_full_track_name_t* full_track_name = NULL;
    quicr_publish_track_handler_t* publish_handler = NULL;
    quicr_subscribe_track_handler_t* subscribe_handler = NULL;

    subscriber.callback_entered = callback_entered;
    subscriber.callback_release = callback_release;
    subscriber.callback_finished = callback_finished;

    publisher.session_ready = quicr_test_event_create();
    publisher.ready = quicr_test_event_create();
    publisher.metrics = quicr_test_event_create();
    subscriber.session_ready = quicr_test_event_create();
    subscriber.ready = quicr_test_event_create();
    subscriber.object = quicr_test_event_create();
    subscriber.object_status = quicr_test_event_create();
    subscriber.subgroup_ended = quicr_test_event_create();
    subscriber.metrics = quicr_test_event_create();
    subscriber.forwarding_started = quicr_test_event_create();
    subscriber.forwarding_data = quicr_test_event_create();
    subscriber.datagram = quicr_test_event_create();
    SCENARIO_REQUIRE(publisher.session_ready != NULL && publisher.ready != NULL && subscriber.session_ready != NULL &&
                     subscriber.ready != NULL && subscriber.object != NULL && subscriber.object_status != NULL &&
                     subscriber.subgroup_ended != NULL && publisher.metrics != NULL && subscriber.metrics != NULL &&
                     subscriber.forwarding_started != NULL && subscriber.forwarding_data != NULL &&
                     subscriber.datagram != NULL);
    SCENARIO_REQUIRE(quicr_session_manager_create(NULL, &manager) == QUICR_STATUS_OK);

    quicr_client_config_t publish_config;
    quicr_client_config_init(&publish_config);
    publish_config.endpoint_id = "c-api-publisher";
    publish_config.connect_uri = connect_uri;
    publish_config.transport_config.metrics_sample_ms = 10U;

    quicr_client_config_t subscribe_config;
    quicr_client_config_init(&subscribe_config);
    subscribe_config.endpoint_id = "c-api-subscriber";
    subscribe_config.connect_uri = connect_uri;
    subscribe_config.transport_config.metrics_sample_ms = 10U;

    const quicr_session_client_callbacks_t subscribe_session_callbacks = {
        .user_data = &subscriber,
        .setup_received = accept_track_setup,
        .status_changed = track_session_status_changed,
    };
    const quicr_session_client_callbacks_t publish_session_callbacks = {
        .user_data = &publisher,
        .setup_received = accept_track_setup,
        .status_changed = track_session_status_changed,
    };
    SCENARIO_REQUIRE(
      quicr_session_manager_add_client(manager, &subscribe_config, &subscribe_session_callbacks, &subscribe_session) ==
      QUICR_STATUS_OK);
    SCENARIO_REQUIRE(quicr_session_manager_add_client(
                       manager, &publish_config, &publish_session_callbacks, &publish_session) == QUICR_STATUS_OK);
    SCENARIO_REQUIRE(quicr_test_event_wait(subscriber.session_ready, QUICR_TEST_TIMEOUT_MS));
    SCENARIO_REQUIRE(quicr_test_event_wait(publisher.session_ready, QUICR_TEST_TIMEOUT_MS));
    SCENARIO_REQUIRE(create_track_name(track_name, &full_track_name) == QUICR_STATUS_OK);

    const quicr_subscribe_track_handler_callbacks_t subscribe_callbacks = {
        .user_data = &subscriber,
        .status_changed = subscribe_status_changed,
        .metrics_sampled = subscribe_metrics_sampled,
        .object_received = object_received,
        .object_status_received = object_status_received,
        .subgroup_ended = subgroup_ended,
        .datagram_received = use_datagrams ? datagram_received : NULL,
    };
    if (use_forwarding) {
        const quicr_forwarding_subscribe_track_handler_callbacks_t forwarding_callbacks = {
            .track_callbacks = subscribe_callbacks,
            .subgroup_started = forwarding_subgroup_started,
            .stream_bytes_forwarded = forwarding_stream_bytes,
        };
        SCENARIO_REQUIRE(
          quicr_forwarding_subscribe_track_handler_create(
            &forwarding_callbacks, full_track_name, 0U, QUICR_GROUP_ORDER_ASCENDING, NULL, &subscribe_handler) ==
          QUICR_STATUS_OK);
    } else {
        SCENARIO_REQUIRE(
          quicr_subscribe_track_handler_create(
            &subscribe_callbacks, full_track_name, 0U, QUICR_GROUP_ORDER_ASCENDING, NULL, &subscribe_handler) ==
          QUICR_STATUS_OK);
    }
    SCENARIO_REQUIRE(quicr_session_manager_add_handler(manager, subscribe_session, subscribe_handler) ==
                     QUICR_STATUS_OK);
    SCENARIO_REQUIRE(quicr_test_event_wait(subscriber.ready, QUICR_TEST_TIMEOUT_MS));

    const quicr_publish_track_handler_callbacks_t publish_callbacks = {
        .user_data = &publisher,
        .status_changed = publish_status_changed,
        .metrics_sampled = publish_metrics_sampled,
    };
    SCENARIO_REQUIRE(
      quicr_publish_track_handler_create(&publish_callbacks,
                                         full_track_name,
                                         use_datagrams ? QUICR_TRACK_MODE_DATAGRAM : QUICR_TRACK_MODE_STREAM,
                                         0U,
                                         0U,
                                         1000U,
                                         NULL,
                                         &publish_handler) == QUICR_STATUS_OK);
    SCENARIO_REQUIRE(quicr_session_manager_add_handler(manager, publish_session, publish_handler) == QUICR_STATUS_OK);
    SCENARIO_REQUIRE(quicr_test_event_wait(publisher.ready, QUICR_TEST_TIMEOUT_MS));
    SCENARIO_REQUIRE(quicr_test_event_wait(publisher.metrics, QUICR_TEST_TIMEOUT_MS));
    SCENARIO_REQUIRE(quicr_test_event_wait(subscriber.metrics, QUICR_TEST_TIMEOUT_MS));

    quicr_publish_track_handler_status_t publish_handler_status;
    quicr_subscribe_track_handler_status_t subscribe_handler_status;
    SCENARIO_REQUIRE(quicr_track_handler_get_status(publish_handler, &publish_handler_status) == QUICR_STATUS_OK);
    SCENARIO_REQUIRE(quicr_track_handler_get_status(subscribe_handler, &subscribe_handler_status) == QUICR_STATUS_OK);
    SCENARIO_REQUIRE(publish_handler_status == QUICR_PUBLISH_TRACK_HANDLER_STATUS_OK);
    SCENARIO_REQUIRE(subscribe_handler_status == QUICR_SUBSCRIBE_TRACK_HANDLER_STATUS_OK);

    static const uint8_t extension_data[] = { 0x10U, 0x20U };
    const quicr_byte_span_t extension_value = { extension_data, sizeof(extension_data) };
    const quicr_extension_entry_t extension_entry = { .type = 2U, .values = &extension_value, .count = 1U };
    const quicr_extensions_t extensions = { .entries = &extension_entry, .count = 1U };

    quicr_object_headers_t headers;
    quicr_object_headers_init(&headers);
    headers.group_id = 7U;
    headers.object_id = 11U;
    headers.subgroup_id = 0U;
    headers.payload_length = sizeof(payload) - 1U;
    headers.has_track_mode = true;
    headers.track_mode = use_datagrams ? QUICR_TRACK_MODE_DATAGRAM : QUICR_TRACK_MODE_STREAM;
    headers.extensions = &extensions;

    const quicr_byte_span_t data = { payload, sizeof(payload) - 1U };
    quicr_publish_object_status_t publish_status = QUICR_PUBLISH_OBJECT_STATUS_INTERNAL_ERROR;
    SCENARIO_REQUIRE(quicr_publish_track_handler_publish_object(
                       publish_handler, &headers, data, NULL, &publish_status) == QUICR_STATUS_OK);
    SCENARIO_REQUIRE(publish_status == QUICR_PUBLISH_OBJECT_STATUS_OK);
    if (use_datagrams) {
        SCENARIO_REQUIRE(quicr_test_event_wait(subscriber.datagram, QUICR_TEST_TIMEOUT_MS));
        SCENARIO_REQUIRE(subscriber.datagram_size > data.size);
    } else if (use_forwarding) {
        SCENARIO_REQUIRE(quicr_test_event_wait(subscriber.forwarding_started, QUICR_TEST_TIMEOUT_MS));
        SCENARIO_REQUIRE(quicr_test_event_wait(subscriber.forwarding_data, QUICR_TEST_TIMEOUT_MS));
    } else {
        SCENARIO_REQUIRE(quicr_test_event_wait(subscriber.object, QUICR_TEST_TIMEOUT_MS));
        SCENARIO_REQUIRE(subscriber.received_size == sizeof(payload) - 1U);
        SCENARIO_REQUIRE(memcmp(subscriber.received, payload, sizeof(payload) - 1U) == 0);
        SCENARIO_REQUIRE(subscriber.received_group_id == headers.group_id);
        SCENARIO_REQUIRE(subscriber.received_object_id == headers.object_id);
        SCENARIO_REQUIRE(subscriber.received_extension_count == 1U);

        headers.object_id = 12U;
        headers.payload_length = 0U;
        headers.extensions = NULL;
        SCENARIO_REQUIRE(quicr_publish_track_handler_publish_object(
                           publish_handler, &headers, (quicr_byte_span_t){ 0 }, NULL, &publish_status) ==
                         QUICR_STATUS_OK);
        SCENARIO_REQUIRE(publish_status == QUICR_PUBLISH_OBJECT_STATUS_OK);
        SCENARIO_REQUIRE(quicr_test_event_wait(subscriber.object, QUICR_TEST_TIMEOUT_MS));
        SCENARIO_REQUIRE(subscriber.received_size == 0U);
        SCENARIO_REQUIRE(subscriber.received_object_id == headers.object_id);

        headers.object_id = 13U;
        headers.status = QUICR_OBJECT_STATUS_DOES_NOT_EXIST;
        SCENARIO_REQUIRE(quicr_publish_track_handler_publish_object(
                           publish_handler, &headers, (quicr_byte_span_t){ 0 }, NULL, &publish_status) ==
                         QUICR_STATUS_OK);
        SCENARIO_REQUIRE(publish_status == QUICR_PUBLISH_OBJECT_STATUS_OK);
        SCENARIO_REQUIRE(quicr_test_event_wait(subscriber.object, QUICR_TEST_TIMEOUT_MS));
        SCENARIO_REQUIRE(subscriber.received_object_id == headers.object_id);
        SCENARIO_REQUIRE(subscriber.received_status == headers.status);
    }

    if (!use_datagrams) {
        SCENARIO_REQUIRE(quicr_publish_track_handler_end_subgroup(publish_handler, 7U, 0U, true) == QUICR_STATUS_OK);
        SCENARIO_REQUIRE(quicr_test_event_wait(subscriber.subgroup_ended, QUICR_TEST_TIMEOUT_MS));
    }

    if (callback_entered == NULL) {
        SCENARIO_REQUIRE(quicr_subscribe_track_handler_pause(subscribe_handler) == QUICR_STATUS_OK);
        SCENARIO_REQUIRE(quicr_subscribe_track_handler_pause(subscribe_handler) == QUICR_STATUS_NOT_APPLIED);
        SCENARIO_REQUIRE(quicr_subscribe_track_handler_resume(subscribe_handler) == QUICR_STATUS_OK);
        SCENARIO_REQUIRE(quicr_subscribe_track_handler_resume(subscribe_handler) == QUICR_STATUS_NOT_APPLIED);
        SCENARIO_REQUIRE(quicr_subscribe_track_handler_support_new_group_request(subscribe_handler, true) ==
                         QUICR_STATUS_OK);
        SCENARIO_REQUIRE(quicr_subscribe_track_handler_request_new_group(subscribe_handler, 0U) == QUICR_STATUS_OK);
        SCENARIO_REQUIRE(quicr_session_update_subscribe_track_handler(subscribe_session, subscribe_handler) ==
                         QUICR_STATUS_OK);
        SCENARIO_REQUIRE(quicr_session_goaway_handler(subscribe_session, NULL, 100U, subscribe_handler) ==
                         QUICR_STATUS_OK);
    }
    SCENARIO_REQUIRE(quicr_session_manager_remove_handler(manager, subscribe_session, publish_handler) ==
                     QUICR_STATUS_INVALID_ARGUMENT);

    if (callback_entered != NULL) {
        atomic_store(&subscriber.block_callback, true);
        bool callback_observed = false;
        for (uint64_t attempt = 0U; attempt < 100U && !callback_observed; ++attempt) {
            headers.group_id = 8U + attempt;
            headers.object_id = 0U;
            headers.payload_length = 0U;
            headers.status = QUICR_OBJECT_STATUS_AVAILABLE;
            SCENARIO_REQUIRE(quicr_publish_track_handler_publish_object(
                               publish_handler, &headers, (quicr_byte_span_t){ 0 }, NULL, &publish_status) ==
                             QUICR_STATUS_OK);
            SCENARIO_REQUIRE(publish_status == QUICR_PUBLISH_OBJECT_STATUS_OK);
            callback_observed = quicr_test_event_wait(callback_entered, 100U);
        }
        SCENARIO_REQUIRE(callback_observed);
        quicr_test_event_signal(destroy_started);
        quicr_subscribe_track_handler_destroy(subscribe_handler);
        subscribe_handler = NULL;
        quicr_test_event_signal(destroy_returned);
        SCENARIO_REQUIRE(quicr_test_event_wait(callback_finished, QUICR_TEST_TIMEOUT_MS));
        SCENARIO_REQUIRE(!atomic_load(&subscriber.callback_release_timed_out));
        headers.group_id = 108U;
        SCENARIO_REQUIRE(quicr_publish_track_handler_publish_object(
                           publish_handler, &headers, (quicr_byte_span_t){ 0 }, NULL, &publish_status) ==
                         QUICR_STATUS_OK);
        SCENARIO_REQUIRE(!quicr_test_event_wait(callback_entered, 100U));
    }

    quicr_publish_track_handler_destroy(publish_handler);
    publish_handler = NULL;
    quicr_subscribe_track_handler_destroy(subscribe_handler);
    subscribe_handler = NULL;

cleanup:
    quicr_session_manager_destroy(manager);
    quicr_track_handler_destroy(publish_handler);
    quicr_track_handler_destroy(subscribe_handler);
    quicr_full_track_name_destroy(full_track_name);
    quicr_test_event_destroy(subscriber.object);
    quicr_test_event_destroy(subscriber.object_status);
    quicr_test_event_destroy(subscriber.subgroup_ended);
    quicr_test_event_destroy(subscriber.metrics);
    quicr_test_event_destroy(subscriber.forwarding_data);
    quicr_test_event_destroy(subscriber.forwarding_started);
    quicr_test_event_destroy(subscriber.datagram);
    quicr_test_event_destroy(subscriber.ready);
    quicr_test_event_destroy(subscriber.session_ready);
    quicr_test_event_destroy(publisher.metrics);
    quicr_test_event_destroy(publisher.ready);
    quicr_test_event_destroy(publisher.session_ready);
    return result;
}

quicr_c_test_result_t
quicr_test_c_publish_subscribe(const char* connect_uri, const int use_datagrams, const int use_forwarding)
{
    return run_publish_subscribe(connect_uri, use_datagrams, use_forwarding, NULL, NULL, NULL, NULL, NULL);
}

quicr_c_test_result_t
quicr_test_c_callback_drain(const char* connect_uri,
                            quicr_test_event_t* callback_entered,
                            quicr_test_event_t* callback_release,
                            quicr_test_event_t* callback_finished,
                            quicr_test_event_t* destroy_started,
                            quicr_test_event_t* destroy_returned)
{
    return run_publish_subscribe(connect_uri,
                                 false,
                                 false,
                                 callback_entered,
                                 callback_release,
                                 callback_finished,
                                 destroy_started,
                                 destroy_returned);
}

quicr_c_test_result_t
quicr_test_c_fetch_namespace_status(const char* connect_uri)
{
    static const uint8_t namespace_data[] = "c-api";
    static const uint8_t track_data[] = "integration";

    quicr_c_test_result_t result = QUICR_C_TEST_SUCCESS;
    request_context_t context = { 0 };
    quicr_session_manager_t* manager = NULL;
    quicr_session_t* session = NULL;
    quicr_track_namespace_t* track_namespace = NULL;
    quicr_full_track_name_t* full_track_name = NULL;
    quicr_fetch_track_handler_t* fetch_handler = NULL;
    quicr_publish_track_handler_t* namespace_track_handler = NULL;
    quicr_publish_namespace_handler_t* publish_namespace_handler = NULL;
    quicr_publish_namespace_handler_t* wrong_publish_namespace_handler = NULL;
    quicr_subscribe_namespace_handler_t* namespace_handler = NULL;
    quicr_track_status_track_handler_t* track_status_handler = NULL;
    bool fetch_registered = false;
    bool publish_namespace_registered = false;
    bool namespace_registered = false;
    bool track_status_registered = false;

    context.session_ready = quicr_test_event_create();
    context.fetch_ready = quicr_test_event_create();
    context.fetch_object = quicr_test_event_create();
    context.namespace_ready = quicr_test_event_create();
    context.namespace_received = quicr_test_event_create();
    context.publish_namespace_ready = quicr_test_event_create();
    context.track_status_ready = quicr_test_event_create();
    context.expected_fetch_size = 3U;
    SCENARIO_REQUIRE(context.session_ready != NULL && context.fetch_ready != NULL && context.fetch_object != NULL &&
                     context.namespace_ready != NULL && context.namespace_received != NULL &&
                     context.publish_namespace_ready != NULL && context.track_status_ready != NULL);
    SCENARIO_REQUIRE(quicr_session_manager_create(NULL, &manager) == QUICR_STATUS_OK);

    quicr_client_config_t config;
    quicr_client_config_init(&config);
    config.endpoint_id = "c-api-request-client";
    config.connect_uri = connect_uri;
    const quicr_session_client_callbacks_t session_callbacks = {
        .user_data = &context,
        .setup_received = accept_track_setup,
        .status_changed = request_session_status_changed,
    };
    SCENARIO_REQUIRE(quicr_session_manager_add_client(manager, &config, &session_callbacks, &session) ==
                     QUICR_STATUS_OK);
    SCENARIO_REQUIRE(quicr_test_event_wait(context.session_ready, QUICR_TEST_TIMEOUT_MS));

    const quicr_byte_span_t namespace_entry = { namespace_data, sizeof(namespace_data) - 1U };
    const quicr_byte_span_t track_name = { track_data, sizeof(track_data) - 1U };
    SCENARIO_REQUIRE(quicr_track_namespace_create(&namespace_entry, 1U, &track_namespace) == QUICR_STATUS_OK);
    SCENARIO_REQUIRE(quicr_full_track_name_create(track_namespace, track_name, &full_track_name) == QUICR_STATUS_OK);

    const quicr_location_t start = { .group = 0U, .object = 0U };
    const quicr_fetch_end_location_t end = { .group = 100U, .has_object = false };
    const quicr_fetch_track_handler_callbacks_t fetch_callbacks = {
        .user_data = &context,
        .status_changed = fetch_status_changed,
        .object_received = fetch_object_received,
    };
    SCENARIO_REQUIRE(
      quicr_fetch_track_handler_create(
        &fetch_callbacks, full_track_name, 0U, &start, &end, QUICR_GROUP_ORDER_ASCENDING, &fetch_handler) ==
      QUICR_STATUS_OK);
    SCENARIO_REQUIRE(quicr_session_manager_add_handler(manager, session, fetch_handler) == QUICR_STATUS_OK);
    fetch_registered = true;
    SCENARIO_REQUIRE(quicr_test_event_wait(context.fetch_ready, QUICR_TEST_TIMEOUT_MS));
    SCENARIO_REQUIRE(quicr_test_event_wait(context.fetch_object, QUICR_TEST_TIMEOUT_MS));
    SCENARIO_REQUIRE(context.fetch_size == 3U);

    const quicr_subscribe_namespace_handler_callbacks_t namespace_callbacks = {
        .user_data = &context,
        .status_changed = namespace_status_changed,
        .namespace_received = namespace_received,
    };
    SCENARIO_REQUIRE(
      quicr_subscribe_namespace_handler_create(
        &namespace_callbacks, track_namespace, QUICR_SUBSCRIBE_NAMESPACE_MODE_NAMESPACES, NULL, &namespace_handler) ==
      QUICR_STATUS_OK);
    SCENARIO_REQUIRE(quicr_session_manager_add_handler(manager, session, namespace_handler) == QUICR_STATUS_OK);
    namespace_registered = true;
    SCENARIO_REQUIRE(quicr_test_event_wait(context.namespace_ready, QUICR_TEST_TIMEOUT_MS));
    SCENARIO_REQUIRE(quicr_test_event_wait(context.namespace_received, QUICR_TEST_TIMEOUT_MS));
    SCENARIO_REQUIRE(context.namespace_count == 1U);

    SCENARIO_REQUIRE(quicr_publish_track_handler_create(
                       NULL, full_track_name, QUICR_TRACK_MODE_STREAM, 0U, 0U, 1000U, NULL, &namespace_track_handler) ==
                     QUICR_STATUS_OK);
    const quicr_publish_namespace_handler_callbacks_t publish_namespace_callbacks = {
        .user_data = &context,
        .status_changed = publish_namespace_status_changed,
    };
    SCENARIO_REQUIRE(quicr_publish_namespace_handler_create(
                       &publish_namespace_callbacks, track_namespace, &publish_namespace_handler) == QUICR_STATUS_OK);
    SCENARIO_REQUIRE(quicr_session_manager_add_handler(manager, session, publish_namespace_handler) == QUICR_STATUS_OK);
    publish_namespace_registered = true;
    SCENARIO_REQUIRE(quicr_test_event_wait(context.publish_namespace_ready, QUICR_TEST_TIMEOUT_MS));
    SCENARIO_REQUIRE(quicr_publish_namespace_handler_publish_track(publish_namespace_handler,
                                                                   namespace_track_handler) == QUICR_STATUS_OK);
    SCENARIO_REQUIRE(quicr_publish_namespace_handler_create(NULL, track_namespace, &wrong_publish_namespace_handler) ==
                     QUICR_STATUS_OK);
    SCENARIO_REQUIRE(quicr_publish_namespace_handler_unpublish_track(
                       wrong_publish_namespace_handler, namespace_track_handler) == QUICR_STATUS_INVALID_ARGUMENT);

    SCENARIO_REQUIRE(quicr_session_manager_remove_publish_namespace_handler(
                       manager, session, publish_namespace_handler) == QUICR_STATUS_OK);
    publish_namespace_registered = false;
    SCENARIO_REQUIRE(quicr_session_manager_add_handler(manager, session, namespace_track_handler) == QUICR_STATUS_OK);
    SCENARIO_REQUIRE(quicr_session_manager_remove_publish_track_handler(manager, session, namespace_track_handler) ==
                     QUICR_STATUS_OK);

    SCENARIO_REQUIRE(quicr_session_manager_add_handler(manager, session, publish_namespace_handler) == QUICR_STATUS_OK);
    publish_namespace_registered = true;
    SCENARIO_REQUIRE(quicr_test_event_wait(context.publish_namespace_ready, QUICR_TEST_TIMEOUT_MS));
    SCENARIO_REQUIRE(quicr_publish_namespace_handler_publish_track(publish_namespace_handler,
                                                                   namespace_track_handler) == QUICR_STATUS_OK);
    SCENARIO_REQUIRE(quicr_session_manager_remove_publish_track_handler(manager, session, namespace_track_handler) ==
                     QUICR_STATUS_OK);
    SCENARIO_REQUIRE(quicr_session_manager_add_handler(manager, session, namespace_track_handler) == QUICR_STATUS_OK);

    quicr_publish_namespace_handler_destroy(publish_namespace_handler);
    publish_namespace_handler = NULL;
    publish_namespace_registered = false;
    quicr_publish_namespace_handler_destroy(wrong_publish_namespace_handler);
    wrong_publish_namespace_handler = NULL;
    quicr_publish_track_handler_destroy(namespace_track_handler);
    namespace_track_handler = NULL;

    const quicr_track_status_handler_callbacks_t track_status_callbacks = {
        .user_data = &context,
        .status_changed = track_status_changed,
    };
    SCENARIO_REQUIRE(quicr_track_status_handler_create(
                       &track_status_callbacks, full_track_name, &track_status_handler) == QUICR_STATUS_OK);
    SCENARIO_REQUIRE(quicr_session_manager_add_handler(manager, session, track_status_handler) == QUICR_STATUS_OK);
    track_status_registered = true;
    SCENARIO_REQUIRE(quicr_test_event_wait(context.track_status_ready, QUICR_TEST_TIMEOUT_MS));

    bool has_response = false;
    quicr_track_status_response_t response = { 0 };
    SCENARIO_REQUIRE(quicr_track_status_handler_get_response(track_status_handler, &has_response, &response, NULL) ==
                     QUICR_STATUS_OK);
    SCENARIO_REQUIRE(has_response);
    SCENARIO_REQUIRE(response.has_largest_location);
    SCENARIO_REQUIRE(response.largest_location.group == 1U);
    SCENARIO_REQUIRE(response.largest_location.object == 2U);

    quicr_error_code_t error_code = 0U;
    size_t reason_size = 0U;
    SCENARIO_REQUIRE(quicr_track_status_handler_get_error(track_status_handler, &error_code, NULL, &reason_size) ==
                     QUICR_STATUS_NOT_FOUND);

    quicr_track_status_handler_destroy(track_status_handler);
    track_status_handler = NULL;
    track_status_registered = false;
    quicr_subscribe_namespace_handler_destroy(namespace_handler);
    namespace_handler = NULL;
    namespace_registered = false;
    quicr_fetch_track_handler_destroy(fetch_handler);
    fetch_handler = NULL;
    fetch_registered = false;

cleanup:
    if (result.line == 0) {
        if (track_status_registered) {
            (void)quicr_session_manager_remove_handler(manager, session, track_status_handler);
        }
        if (namespace_registered) {
            (void)quicr_session_manager_remove_handler(manager, session, namespace_handler);
        }
        if (publish_namespace_registered) {
            (void)quicr_session_manager_remove_handler(manager, session, publish_namespace_handler);
        }
        if (fetch_registered) {
            (void)quicr_session_manager_remove_handler(manager, session, fetch_handler);
        }
    }
    quicr_session_manager_destroy(manager);
    manager = NULL;
    quicr_track_handler_destroy(track_status_handler);
    quicr_track_handler_destroy(namespace_handler);
    quicr_track_handler_destroy(publish_namespace_handler);
    quicr_track_handler_destroy(wrong_publish_namespace_handler);
    quicr_track_handler_destroy(namespace_track_handler);
    quicr_track_handler_destroy(fetch_handler);

    quicr_full_track_name_destroy(full_track_name);
    quicr_track_namespace_destroy(track_namespace);
    quicr_test_event_destroy(context.track_status_ready);
    quicr_test_event_destroy(context.namespace_received);
    quicr_test_event_destroy(context.namespace_ready);
    quicr_test_event_destroy(context.publish_namespace_ready);
    quicr_test_event_destroy(context.fetch_object);
    quicr_test_event_destroy(context.fetch_ready);
    quicr_test_event_destroy(context.session_ready);
    return result;
}

quicr_c_test_result_t
quicr_test_c_rejections(const char* connect_uri)
{
    quicr_c_test_result_t result = QUICR_C_TEST_SUCCESS;
    rejection_context_t context = { 0 };
    quicr_session_manager_t* manager = NULL;
    quicr_session_t* publish_session = NULL;
    quicr_session_t* subscribe_session = NULL;
    quicr_full_track_name_t* full_track_name = NULL;
    quicr_publish_track_handler_t* publish_handler = NULL;
    quicr_subscribe_track_handler_t* subscribe_handler = NULL;
    quicr_track_status_track_handler_t* track_status_handler = NULL;
    bool publish_registered = false;
    bool subscribe_registered = false;
    bool track_status_registered = false;

    context.publish_session_ready = quicr_test_event_create();
    context.subscribe_session_ready = quicr_test_event_create();
    context.publish_rejected = quicr_test_event_create();
    context.subscribe_rejected = quicr_test_event_create();
    context.track_status_rejected = quicr_test_event_create();
    SCENARIO_REQUIRE(context.publish_session_ready != NULL && context.subscribe_session_ready != NULL &&
                     context.publish_rejected != NULL && context.subscribe_rejected != NULL &&
                     context.track_status_rejected != NULL);
    SCENARIO_REQUIRE(quicr_session_manager_create(NULL, &manager) == QUICR_STATUS_OK);

    quicr_client_config_t publish_config;
    quicr_client_config_init(&publish_config);
    publish_config.endpoint_id = "c-api-rejected-publisher";
    publish_config.connect_uri = connect_uri;

    quicr_client_config_t subscribe_config;
    quicr_client_config_init(&subscribe_config);
    subscribe_config.endpoint_id = "c-api-rejected-subscriber";
    subscribe_config.connect_uri = connect_uri;

    const quicr_session_client_callbacks_t publish_session_callbacks = {
        .user_data = context.publish_session_ready,
        .setup_received = accept_track_setup,
        .status_changed = rejection_session_status_changed,
    };
    const quicr_session_client_callbacks_t subscribe_session_callbacks = {
        .user_data = context.subscribe_session_ready,
        .setup_received = accept_track_setup,
        .status_changed = rejection_session_status_changed,
    };
    SCENARIO_REQUIRE(quicr_session_manager_add_client(
                       manager, &publish_config, &publish_session_callbacks, &publish_session) == QUICR_STATUS_OK);
    SCENARIO_REQUIRE(
      quicr_session_manager_add_client(manager, &subscribe_config, &subscribe_session_callbacks, &subscribe_session) ==
      QUICR_STATUS_OK);
    SCENARIO_REQUIRE(quicr_test_event_wait(context.publish_session_ready, QUICR_TEST_TIMEOUT_MS));
    SCENARIO_REQUIRE(quicr_test_event_wait(context.subscribe_session_ready, QUICR_TEST_TIMEOUT_MS));
    SCENARIO_REQUIRE(create_track_name("integration", &full_track_name) == QUICR_STATUS_OK);

    const quicr_publish_track_handler_callbacks_t publish_callbacks = {
        .user_data = &context,
        .status_changed = publish_rejected,
    };
    SCENARIO_REQUIRE(
      quicr_publish_track_handler_create(
        &publish_callbacks, full_track_name, QUICR_TRACK_MODE_STREAM, 0U, 0U, 1000U, NULL, &publish_handler) ==
      QUICR_STATUS_OK);
    SCENARIO_REQUIRE(quicr_session_manager_add_handler(manager, publish_session, publish_handler) == QUICR_STATUS_OK);
    publish_registered = true;
    SCENARIO_REQUIRE(quicr_test_event_wait(context.publish_rejected, QUICR_TEST_TIMEOUT_MS));

    const quicr_subscribe_track_handler_callbacks_t subscribe_callbacks = {
        .user_data = &context,
        .status_changed = subscribe_rejected,
    };
    SCENARIO_REQUIRE(
      quicr_subscribe_track_handler_create(
        &subscribe_callbacks, full_track_name, 0U, QUICR_GROUP_ORDER_ASCENDING, NULL, &subscribe_handler) ==
      QUICR_STATUS_OK);
    SCENARIO_REQUIRE(quicr_session_manager_add_handler(manager, subscribe_session, subscribe_handler) ==
                     QUICR_STATUS_OK);
    subscribe_registered = true;
    SCENARIO_REQUIRE(quicr_test_event_wait(context.subscribe_rejected, QUICR_TEST_TIMEOUT_MS));

    const quicr_track_status_handler_callbacks_t track_status_callbacks = {
        .user_data = &context,
        .status_changed = track_status_rejected,
    };
    SCENARIO_REQUIRE(quicr_track_status_handler_create(
                       &track_status_callbacks, full_track_name, &track_status_handler) == QUICR_STATUS_OK);
    SCENARIO_REQUIRE(quicr_session_manager_add_handler(manager, subscribe_session, track_status_handler) ==
                     QUICR_STATUS_OK);
    track_status_registered = true;
    SCENARIO_REQUIRE(quicr_test_event_wait(context.track_status_rejected, QUICR_TEST_TIMEOUT_MS));

    quicr_error_code_t error_code = 0U;
    size_t reason_size = 0U;
    SCENARIO_REQUIRE(quicr_track_status_handler_get_error(track_status_handler, &error_code, NULL, &reason_size) ==
                     QUICR_STATUS_OK);
    SCENARIO_REQUIRE(error_code == QUICR_ERROR_INTERNAL_ERROR);
    SCENARIO_REQUIRE(reason_size != 0U);
    char reason[64] = { 0 };
    size_t reason_capacity = sizeof(reason);
    SCENARIO_REQUIRE(quicr_track_status_handler_get_error(
                       track_status_handler, &error_code, reason, &reason_capacity) == QUICR_STATUS_OK);
    SCENARIO_REQUIRE(reason_capacity == reason_size);
    SCENARIO_REQUIRE(strlen(reason) + 1U == reason_size);

cleanup:
    if (result.line == 0) {
        if (track_status_registered) {
            (void)quicr_session_manager_remove_handler(manager, subscribe_session, track_status_handler);
        }
        if (subscribe_registered) {
            (void)quicr_session_manager_remove_handler(manager, subscribe_session, subscribe_handler);
        }
        if (publish_registered) {
            (void)quicr_session_manager_remove_handler(manager, publish_session, publish_handler);
        }
    }
    quicr_session_manager_destroy(manager);
    manager = NULL;
    quicr_track_handler_destroy(track_status_handler);
    quicr_track_handler_destroy(subscribe_handler);
    quicr_track_handler_destroy(publish_handler);
    quicr_full_track_name_destroy(full_track_name);
    quicr_test_event_destroy(context.track_status_rejected);
    quicr_test_event_destroy(context.subscribe_rejected);
    quicr_test_event_destroy(context.publish_rejected);
    quicr_test_event_destroy(context.subscribe_session_ready);
    quicr_test_event_destroy(context.publish_session_ready);
    return result;
}

quicr_c_test_result_t
quicr_test_c_server_fetch(const char* connect_uri, const uint16_t server_port)
{
    quicr_c_test_result_t result = QUICR_C_TEST_SUCCESS;
    server_fetch_context_t context = { 0 };
    quicr_session_manager_t* server_manager = NULL;
    quicr_session_manager_t* client_manager = NULL;
    quicr_session_t* client_session = NULL;
    quicr_track_namespace_t* track_namespace = NULL;
    quicr_full_track_name_t* full_track_name = NULL;
    quicr_fetch_track_handler_t* rejected_fetch = NULL;
    quicr_fetch_track_handler_t* accepted_fetch = NULL;
    quicr_publish_namespace_handler_t* rejected_namespace = NULL;
    quicr_publish_namespace_handler_t* accepted_namespace = NULL;
    bool rejected_fetch_registered = false;
    bool accepted_fetch_registered = false;
    bool rejected_namespace_registered = false;
    bool accepted_namespace_registered = false;

    context.client_ready = quicr_test_event_create();
    context.fetch_rejected = quicr_test_event_create();
    context.fetch_ready = quicr_test_event_create();
    context.fetch_object = quicr_test_event_create();
    context.fetch_object_status = quicr_test_event_create();
    context.namespace_request = quicr_test_event_create();
    context.namespace_rejected = quicr_test_event_create();
    context.namespace_accepted = quicr_test_event_create();
    context.server_session_ready = quicr_test_event_create();
    context.expected_fetch_size = 3U;
    SCENARIO_REQUIRE(context.client_ready != NULL && context.fetch_rejected != NULL && context.fetch_ready != NULL &&
                     context.fetch_object != NULL && context.fetch_object_status != NULL &&
                     context.namespace_request != NULL && context.namespace_rejected != NULL &&
                     context.namespace_accepted != NULL && context.server_session_ready != NULL);

    const quicr_session_manager_callbacks_t server_manager_callbacks = {
        .user_data = &context,
        .new_server_session = server_fetch_new_session,
    };
    SCENARIO_REQUIRE(quicr_session_manager_create(&server_manager_callbacks, &server_manager) == QUICR_STATUS_OK);
    SCENARIO_REQUIRE(quicr_session_manager_create(NULL, &client_manager) == QUICR_STATUS_OK);

    quicr_server_config_t server_config;
    quicr_server_config_init(&server_config);
    server_config.endpoint_id = "c-api-server";
    server_config.bind_ip = "127.0.0.1";
    server_config.port = server_port;
    server_config.transport_config.tls_cert_filename = "server-cert.pem";
    server_config.transport_config.tls_key_filename = "server-key.pem";
    const quicr_session_server_callbacks_t server_callbacks = {
        .user_data = &context,
        .setup_received = accept_track_setup,
        .publish_namespace_received = server_publish_namespace_received,
        .standalone_fetch_received = server_standalone_fetch_received,
    };
    SCENARIO_REQUIRE(quicr_session_manager_add_server(server_manager, &server_config, &server_callbacks) ==
                     QUICR_STATUS_OK);

    quicr_client_config_t client_config;
    quicr_client_config_init(&client_config);
    client_config.endpoint_id = "c-api-fetch-client";
    client_config.connect_uri = connect_uri;
    const quicr_session_client_callbacks_t client_callbacks = {
        .user_data = &context,
        .setup_received = accept_track_setup,
        .status_changed = server_fetch_client_status,
    };
    SCENARIO_REQUIRE(quicr_session_manager_add_client(
                       client_manager, &client_config, &client_callbacks, &client_session) == QUICR_STATUS_OK);
    SCENARIO_REQUIRE(quicr_test_event_wait(context.client_ready, QUICR_TEST_TIMEOUT_MS));
    SCENARIO_REQUIRE(quicr_test_event_wait(context.server_session_ready, QUICR_TEST_TIMEOUT_MS));
    SCENARIO_REQUIRE(context.server_session != NULL);
    SCENARIO_REQUIRE(create_track_name("integration", &full_track_name) == QUICR_STATUS_OK);

    const quicr_location_t start = { .group = 0U, .object = 0U };
    const quicr_fetch_end_location_t end = { .group = 10U, .has_object = false };
    const quicr_fetch_track_handler_callbacks_t fetch_callbacks = {
        .user_data = &context,
        .status_changed = server_fetch_status_changed,
        .object_received = server_fetch_object_received,
        .object_status_received = server_fetch_object_status_received,
    };

    SCENARIO_REQUIRE(
      quicr_fetch_track_handler_create(
        &fetch_callbacks, full_track_name, 0U, &start, &end, QUICR_GROUP_ORDER_ASCENDING, &rejected_fetch) ==
      QUICR_STATUS_OK);
    SCENARIO_REQUIRE(quicr_session_manager_add_handler(client_manager, client_session, rejected_fetch) ==
                     QUICR_STATUS_OK);
    rejected_fetch_registered = true;
    SCENARIO_REQUIRE(quicr_test_event_wait(context.fetch_rejected, QUICR_TEST_TIMEOUT_MS));
    SCENARIO_REQUIRE(context.fetch_status != QUICR_SUBSCRIBE_TRACK_HANDLER_STATUS_OK);
    SCENARIO_REQUIRE(quicr_session_manager_remove_handler(client_manager, client_session, rejected_fetch) ==
                     QUICR_STATUS_OK);
    rejected_fetch_registered = false;
    quicr_track_handler_destroy(rejected_fetch);
    rejected_fetch = NULL;

    SCENARIO_REQUIRE(
      quicr_fetch_track_handler_create(
        &fetch_callbacks, full_track_name, 0U, &start, &end, QUICR_GROUP_ORDER_ASCENDING, &accepted_fetch) ==
      QUICR_STATUS_OK);
    SCENARIO_REQUIRE(quicr_session_manager_add_handler(client_manager, client_session, accepted_fetch) ==
                     QUICR_STATUS_OK);
    accepted_fetch_registered = true;
    SCENARIO_REQUIRE(quicr_session_manager_remove_handler(server_manager, client_session, accepted_fetch) ==
                     QUICR_STATUS_INVALID_ARGUMENT);
    SCENARIO_REQUIRE(quicr_test_event_wait(context.fetch_ready, QUICR_TEST_TIMEOUT_MS));
    SCENARIO_REQUIRE(quicr_test_event_wait(context.fetch_object, QUICR_TEST_TIMEOUT_MS));
    SCENARIO_REQUIRE(context.fetch_size == 3U);
    SCENARIO_REQUIRE(context.publish_fetch_handler != NULL);

    static const uint8_t namespace_data[] = "c-api-server";
    const quicr_byte_span_t namespace_entry = { namespace_data, sizeof(namespace_data) - 1U };
    SCENARIO_REQUIRE(quicr_track_namespace_create(&namespace_entry, 1U, &track_namespace) == QUICR_STATUS_OK);
    const quicr_publish_namespace_handler_callbacks_t namespace_callbacks = {
        .user_data = &context,
        .status_changed = server_namespace_status_changed,
    };
    SCENARIO_REQUIRE(quicr_publish_namespace_handler_create(
                       &namespace_callbacks, track_namespace, &rejected_namespace) == QUICR_STATUS_OK);
    SCENARIO_REQUIRE(quicr_session_manager_add_handler(client_manager, client_session, rejected_namespace) ==
                     QUICR_STATUS_OK);
    rejected_namespace_registered = true;
    SCENARIO_REQUIRE(quicr_test_event_wait(context.namespace_request, QUICR_TEST_TIMEOUT_MS));
    SCENARIO_REQUIRE(quicr_test_event_wait(context.namespace_rejected, QUICR_TEST_TIMEOUT_MS));

    quicr_error_code_t namespace_error = 0U;
    size_t namespace_reason_size = 0U;
    SCENARIO_REQUIRE(quicr_publish_namespace_handler_get_error(
                       rejected_namespace, &namespace_error, NULL, &namespace_reason_size) == QUICR_STATUS_OK);
    SCENARIO_REQUIRE(namespace_error == QUICR_ERROR_INTERNAL_ERROR);
    SCENARIO_REQUIRE(namespace_reason_size == sizeof("intentional namespace rejection"));
    SCENARIO_REQUIRE(quicr_session_manager_remove_handler(client_manager, client_session, rejected_namespace) ==
                     QUICR_STATUS_OK);
    rejected_namespace_registered = false;

    SCENARIO_REQUIRE(quicr_publish_namespace_handler_create(
                       &namespace_callbacks, track_namespace, &accepted_namespace) == QUICR_STATUS_OK);
    SCENARIO_REQUIRE(quicr_session_manager_add_handler(client_manager, client_session, accepted_namespace) ==
                     QUICR_STATUS_OK);
    accepted_namespace_registered = true;
    SCENARIO_REQUIRE(quicr_test_event_wait(context.namespace_request, QUICR_TEST_TIMEOUT_MS));
    SCENARIO_REQUIRE(quicr_test_event_wait(context.namespace_accepted, QUICR_TEST_TIMEOUT_MS));

cleanup:
    if (result.line == 0) {
        if (accepted_namespace_registered) {
            (void)quicr_session_manager_remove_handler(client_manager, client_session, accepted_namespace);
        }
        if (rejected_namespace_registered) {
            (void)quicr_session_manager_remove_handler(client_manager, client_session, rejected_namespace);
        }
        if (accepted_fetch_registered) {
            (void)quicr_session_manager_remove_handler(client_manager, client_session, accepted_fetch);
        }
        if (rejected_fetch_registered) {
            (void)quicr_session_manager_remove_handler(client_manager, client_session, rejected_fetch);
        }
    }
    quicr_track_handler_destroy(context.publish_fetch_handler);
    context.publish_fetch_handler = NULL;
    quicr_session_manager_destroy(client_manager);
    client_manager = NULL;
    quicr_session_manager_destroy(server_manager);
    server_manager = NULL;
    quicr_track_handler_destroy(accepted_fetch);
    quicr_track_handler_destroy(rejected_fetch);
    quicr_track_handler_destroy(accepted_namespace);
    quicr_track_handler_destroy(rejected_namespace);
    quicr_full_track_name_destroy(full_track_name);
    quicr_track_namespace_destroy(track_namespace);
    quicr_test_event_destroy(context.server_session_ready);
    quicr_test_event_destroy(context.namespace_accepted);
    quicr_test_event_destroy(context.namespace_rejected);
    quicr_test_event_destroy(context.namespace_request);
    quicr_test_event_destroy(context.fetch_object_status);
    quicr_test_event_destroy(context.fetch_object);
    quicr_test_event_destroy(context.fetch_ready);
    quicr_test_event_destroy(context.fetch_rejected);
    quicr_test_event_destroy(context.client_ready);
    return result;
}

quicr_c_test_result_t
quicr_test_c_joining_server_start(const uint16_t port, quicr_test_joining_server_t** server)
{
    quicr_c_test_result_t result = QUICR_C_TEST_SUCCESS;
    quicr_test_joining_server_t* created = NULL;

    if (server == NULL) {
        return (quicr_c_test_result_t){ __LINE__, "server != NULL" };
    }
    *server = NULL;

    created = calloc(1U, sizeof(*created));
    SCENARIO_REQUIRE(created != NULL);
    created->subscribe_received = quicr_test_event_create();
    SCENARIO_REQUIRE(created->subscribe_received != NULL);
    created->subscribe_tracks_received = quicr_test_event_create();
    SCENARIO_REQUIRE(created->subscribe_tracks_received != NULL);
    created->joining_received = quicr_test_event_create();
    SCENARIO_REQUIRE(created->joining_received != NULL);
    created->publish_done_received = quicr_test_event_create();
    SCENARIO_REQUIRE(created->publish_done_received != NULL);
    created->unsubscribe_received = quicr_test_event_create();
    SCENARIO_REQUIRE(created->unsubscribe_received != NULL);
    created->publish_namespace_done_received = quicr_test_event_create();
    SCENARIO_REQUIRE(created->publish_namespace_done_received != NULL);
    created->unsubscribe_namespace_received = quicr_test_event_create();
    SCENARIO_REQUIRE(created->unsubscribe_namespace_received != NULL);
    SCENARIO_REQUIRE(quicr_session_manager_create(NULL, &created->manager) == QUICR_STATUS_OK);

    quicr_server_config_t config;
    quicr_server_config_init(&config);
    config.endpoint_id = "c-api-joining-server";
    config.bind_ip = "127.0.0.1";
    config.port = port;
    config.transport_config.tls_cert_filename = "server-cert.pem";
    config.transport_config.tls_key_filename = "server-key.pem";

    const quicr_session_server_callbacks_t callbacks = {
        .user_data = created,
        .setup_received = accept_track_setup,
        .publish_received = joining_publish_received,
        .subscribe_tracks_received = joining_subscribe_tracks_received,
        .subscribe_received = joining_subscribe_received,
        .joining_fetch_received = joining_fetch_rejected,
        .publish_done_received = joining_publish_done_received,
        .unsubscribe_received = joining_unsubscribe_received,
        .publish_namespace_done_received = joining_publish_namespace_done_received,
        .unsubscribe_namespace_received = joining_unsubscribe_namespace_received,
    };
    SCENARIO_REQUIRE(quicr_session_manager_add_server(created->manager, &config, &callbacks) == QUICR_STATUS_OK);

    *server = created;
    return result;

cleanup:
    if (created != NULL) {
        quicr_subscribe_track_handler_destroy(created->received_publish_handler);
        quicr_publish_track_handler_destroy(created->relay_publish_handler);
        quicr_publish_response_destroy(created->publish_response);
        quicr_session_manager_destroy(created->manager);
        quicr_test_event_destroy(created->unsubscribe_namespace_received);
        quicr_test_event_destroy(created->publish_namespace_done_received);
        quicr_test_event_destroy(created->unsubscribe_received);
        quicr_test_event_destroy(created->publish_done_received);
        quicr_test_event_destroy(created->subscribe_received);
        quicr_test_event_destroy(created->subscribe_tracks_received);
        quicr_test_event_destroy(created->joining_received);
        free(created);
    }
    return result;
}

int
quicr_test_c_joining_server_wait(quicr_test_joining_server_t* server, const uint32_t timeout_ms)
{
    return server != NULL && quicr_test_event_wait(server->subscribe_tracks_received, timeout_ms) &&
           quicr_test_event_wait(server->joining_received, timeout_ms) &&
           quicr_test_event_wait(server->publish_done_received, timeout_ms) &&
           quicr_test_event_wait(server->unsubscribe_received, timeout_ms) &&
           quicr_session_unbind_publish_track_handler(
             server->relay_session, 18U, server->relay_publish_handler, true) == QUICR_STATUS_INVALID_ARGUMENT &&
           quicr_session_unbind_publish_track_handler(
             server->relay_session, 17U, server->relay_publish_handler, false) == QUICR_STATUS_OK &&
           quicr_test_event_wait(server->publish_namespace_done_received, timeout_ms) &&
           quicr_test_event_wait(server->unsubscribe_namespace_received, timeout_ms);
}

int
quicr_test_c_joining_server_send_namespace(quicr_test_joining_server_t* server)
{
    static const uint8_t first_entry_data[] = "c-api";
    static const uint8_t second_entry_data[] = "joining";
    const quicr_byte_span_t entries[] = {
        { first_entry_data, sizeof(first_entry_data) - 1U },
        { second_entry_data, sizeof(second_entry_data) - 1U },
    };
    quicr_track_namespace_t* track_namespace = NULL;

    if (server == NULL || !quicr_test_event_wait(server->subscribe_received, QUICR_TEST_TIMEOUT_MS) ||
        server->relay_session == NULL ||
        quicr_track_namespace_create(entries, 2U, &track_namespace) != QUICR_STATUS_OK) {
        return false;
    }

    const bool sent = quicr_session_send_namespace(server->relay_session, track_namespace) == QUICR_STATUS_OK &&
                      quicr_session_send_namespace_done(server->relay_session, track_namespace) == QUICR_STATUS_OK;
    quicr_track_namespace_destroy(track_namespace);
    return sent;
}

void
quicr_test_c_joining_server_destroy(quicr_test_joining_server_t* server)
{
    if (server == NULL) {
        return;
    }

    quicr_subscribe_track_handler_destroy(server->received_publish_handler);
    quicr_publish_track_handler_destroy(server->relay_publish_handler);
    quicr_publish_response_destroy(server->publish_response);
    quicr_session_manager_destroy(server->manager);
    quicr_test_event_destroy(server->unsubscribe_namespace_received);
    quicr_test_event_destroy(server->publish_namespace_done_received);
    quicr_test_event_destroy(server->unsubscribe_received);
    quicr_test_event_destroy(server->publish_done_received);
    quicr_test_event_destroy(server->subscribe_received);
    quicr_test_event_destroy(server->subscribe_tracks_received);
    quicr_test_event_destroy(server->joining_received);
    free(server);
}
