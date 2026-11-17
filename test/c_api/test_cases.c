// SPDX-FileCopyrightText: Copyright (c) 2026 Cisco Systems
// SPDX-License-Identifier: BSD-2-Clause

#include "test_cases.h"

#include <quicr/quicr.h>

#include <stddef.h>
#include <stdint.h>

static bool
BytesEqual(const quicr_byte_span_t span, const uint8_t* expected, const size_t expected_size)
{
    if (span.size != expected_size || (expected_size != 0 && span.data == NULL)) {
        return false;
    }

    for (size_t i = 0; i < expected_size; ++i) {
        if (span.data[i] != expected[i]) {
            return false;
        }
    }

    return true;
}

quicr_c_test_result_t
quicr_c_test_names(void)
{
    uint8_t first[] = { 'a', 0, 'b' };
    uint8_t second[] = { 'c' };
    const uint8_t expected_first[] = { 'a', 0, 'b' };
    const uint8_t expected_second[] = { 'c' };
    quicr_byte_span_t entries[] = {
        { .data = first, .size = sizeof(first) },
        { .data = second, .size = sizeof(second) },
    };

    quicr_track_namespace_t* track_namespace = NULL;
    QUICR_C_TEST_CHECK(quicr_track_namespace_create(entries, 2, &track_namespace) == QUICR_STATUS_OK);
    QUICR_C_TEST_CHECK(track_namespace != NULL);
    QUICR_C_TEST_CHECK(quicr_track_namespace_count(track_namespace) == 2);

    first[0] = 'z';
    second[0] = 'z';

    quicr_byte_span_t entry = { 0 };
    QUICR_C_TEST_CHECK(quicr_track_namespace_entry(track_namespace, 0, &entry) == QUICR_STATUS_OK);
    QUICR_C_TEST_CHECK(BytesEqual(entry, expected_first, sizeof(expected_first)));
    QUICR_C_TEST_CHECK(quicr_track_namespace_entry(track_namespace, 1, &entry) == QUICR_STATUS_OK);
    QUICR_C_TEST_CHECK(BytesEqual(entry, expected_second, sizeof(expected_second)));
    QUICR_C_TEST_CHECK(quicr_track_namespace_entry(track_namespace, 2, &entry) == QUICR_STATUS_INVALID_ARGUMENT);

    uint8_t track_name_data[] = { 't', 0, 'n' };
    const uint8_t expected_track_name[] = { 't', 0, 'n' };
    quicr_full_track_name_t* full_track_name = NULL;
    QUICR_C_TEST_CHECK(
      quicr_full_track_name_create(track_namespace,
                                   (quicr_byte_span_t){ .data = track_name_data, .size = sizeof(track_name_data) },
                                   &full_track_name) == QUICR_STATUS_OK);
    track_name_data[0] = 'z';

    QUICR_C_TEST_CHECK(full_track_name != NULL);
    QUICR_C_TEST_CHECK(quicr_full_track_name_namespace_count(full_track_name) == 2);
    QUICR_C_TEST_CHECK(
      BytesEqual(quicr_full_track_name_track_name(full_track_name), expected_track_name, sizeof(expected_track_name)));
    QUICR_C_TEST_CHECK(quicr_full_track_name_namespace_entry(full_track_name, 0, &entry) == QUICR_STATUS_OK);
    QUICR_C_TEST_CHECK(BytesEqual(entry, expected_first, sizeof(expected_first)));

    quicr_full_track_name_destroy(full_track_name);
    quicr_track_namespace_destroy(track_namespace);
    quicr_full_track_name_destroy(NULL);
    quicr_track_namespace_destroy(NULL);

    quicr_track_namespace_t* empty_namespace = NULL;
    QUICR_C_TEST_CHECK(quicr_track_namespace_create(NULL, 0, &empty_namespace) == QUICR_STATUS_OK);
    QUICR_C_TEST_CHECK(quicr_track_namespace_count(empty_namespace) == 0);
    quicr_track_namespace_destroy(empty_namespace);

    QUICR_C_TEST_CHECK(quicr_track_namespace_create(NULL, 1, &track_namespace) == QUICR_STATUS_INVALID_ARGUMENT);
    QUICR_C_TEST_CHECK(quicr_track_namespace_create(entries, 2, NULL) == QUICR_STATUS_INVALID_ARGUMENT);
    QUICR_C_TEST_CHECK(quicr_full_track_name_create(NULL, (quicr_byte_span_t){ 0 }, &full_track_name) ==
                       QUICR_STATUS_INVALID_ARGUMENT);

    return QUICR_C_TEST_SUCCESS;
}

quicr_c_test_result_t
quicr_c_test_extensions(void)
{
    quicr_track_extensions_t* extensions = NULL;
    QUICR_C_TEST_CHECK(quicr_track_extensions_create(&extensions) == QUICR_STATUS_OK);
    QUICR_C_TEST_CHECK(extensions != NULL);
    QUICR_C_TEST_CHECK(quicr_track_extensions_count(extensions) == 0);
    QUICR_C_TEST_CHECK(quicr_track_extensions_immutable_count(extensions) == 0);

    uint8_t mutable_value[] = { 1, 2, 3 };
    const uint8_t expected_mutable[] = { 1, 2, 3 };
    QUICR_C_TEST_CHECK(quicr_track_extensions_add(
                         extensions, 10, (quicr_byte_span_t){ .data = mutable_value, .size = sizeof(mutable_value) }) ==
                       QUICR_STATUS_OK);
    mutable_value[0] = 9;

    uint64_t type = 0;
    quicr_byte_span_t value = { 0 };
    QUICR_C_TEST_CHECK(quicr_track_extensions_count(extensions) == 1);
    QUICR_C_TEST_CHECK(quicr_track_extensions_entry(extensions, 0, &type, &value) == QUICR_STATUS_OK);
    QUICR_C_TEST_CHECK(type == 10);
    QUICR_C_TEST_CHECK(BytesEqual(value, expected_mutable, sizeof(expected_mutable)));

    const uint8_t replacement[] = { 4 };
    QUICR_C_TEST_CHECK(quicr_track_extensions_add(
                         extensions, 10, (quicr_byte_span_t){ .data = replacement, .size = sizeof(replacement) }) ==
                       QUICR_STATUS_OK);
    QUICR_C_TEST_CHECK(quicr_track_extensions_count(extensions) == 1);
    QUICR_C_TEST_CHECK(quicr_track_extensions_entry(extensions, 0, &type, &value) == QUICR_STATUS_OK);
    QUICR_C_TEST_CHECK(BytesEqual(value, replacement, sizeof(replacement)));

    const uint8_t immutable_value[] = { 5, 6 };
    QUICR_C_TEST_CHECK(
      quicr_track_extensions_add_immutable(
        extensions, 20, (quicr_byte_span_t){ .data = immutable_value, .size = sizeof(immutable_value) }) ==
      QUICR_STATUS_OK);
    QUICR_C_TEST_CHECK(quicr_track_extensions_immutable_count(extensions) == 1);
    QUICR_C_TEST_CHECK(quicr_track_extensions_immutable_entry(extensions, 0, &type, &value) == QUICR_STATUS_OK);
    QUICR_C_TEST_CHECK(type == 20);
    QUICR_C_TEST_CHECK(BytesEqual(value, immutable_value, sizeof(immutable_value)));

    QUICR_C_TEST_CHECK(quicr_track_extensions_entry(extensions, 1, &type, &value) == QUICR_STATUS_INVALID_ARGUMENT);
    QUICR_C_TEST_CHECK(quicr_track_extensions_entry(NULL, 0, &type, &value) == QUICR_STATUS_INVALID_ARGUMENT);
    QUICR_C_TEST_CHECK(quicr_track_extensions_add(NULL, 0, (quicr_byte_span_t){ 0 }) == QUICR_STATUS_INVALID_ARGUMENT);

    quicr_track_extensions_destroy(extensions);
    quicr_track_extensions_destroy(NULL);
    return QUICR_C_TEST_SUCCESS;
}

static void
Poison(void* object, const size_t size)
{
    unsigned char* bytes = object;
    for (size_t i = 0; i < size; ++i) {
        bytes[i] = 0xa5;
    }
}

static bool
EmptyString(const char* value)
{
    return value != NULL && value[0] == '\0';
}

static quicr_c_test_result_t
CheckTransportDefaults(const quicr_transport_config_t* config)
{
    QUICR_C_TEST_CHECK(EmptyString(config->tls_cert_filename));
    QUICR_C_TEST_CHECK(EmptyString(config->tls_key_filename));
    QUICR_C_TEST_CHECK(EmptyString(config->tls_root_trust_filename));
    QUICR_C_TEST_CHECK(config->time_queue_init_queue_size == 1000);
    QUICR_C_TEST_CHECK(config->time_queue_max_duration == 2000);
    QUICR_C_TEST_CHECK(config->time_queue_bucket_interval == 500);
    QUICR_C_TEST_CHECK(config->time_queue_rx_size == 1000);
    QUICR_C_TEST_CHECK(!config->debug);
    QUICR_C_TEST_CHECK(config->quic_cwin_minimum == 131072);
    QUICR_C_TEST_CHECK(config->quic_wifi_shadow_rtt_us == 20000);
    QUICR_C_TEST_CHECK(config->idle_timeout_ms == 30000);
    QUICR_C_TEST_CHECK(config->congestion_control == QUICR_CONGESTION_CONTROL_BBR);
    QUICR_C_TEST_CHECK(EmptyString(config->quic_qlog_path));
    QUICR_C_TEST_CHECK(config->quic_priority_limit == 0);
    QUICR_C_TEST_CHECK(config->max_connections == 2);
    QUICR_C_TEST_CHECK(!config->ssl_keylog);
    QUICR_C_TEST_CHECK(config->socket_buffer_size == 1000000);
    QUICR_C_TEST_CHECK(config->callback_queue_size == 2000);
    QUICR_C_TEST_CHECK(config->metrics_sample_ms == 5000);
    QUICR_C_TEST_CHECK(config->initial_max_stream_data == 0);
    QUICR_C_TEST_CHECK(config->initial_max_stream_id == 512);
    QUICR_C_TEST_CHECK(config->quic_shards == 1);
    QUICR_C_TEST_CHECK(config->use_af_xdp);
    return QUICR_C_TEST_SUCCESS;
}

quicr_c_test_result_t
quicr_c_test_initializers(void)
{
    QUICR_C_TEST_CHECK(QUICR_REQUEST_ERROR_GOING_AWAY == 15);
    QUICR_C_TEST_CHECK(QUICR_ERROR_GOING_AWAY == 0x6);

    quicr_transport_config_t transport;
    Poison(&transport, sizeof(transport));
    quicr_transport_config_init(&transport);
    const quicr_c_test_result_t transport_result = CheckTransportDefaults(&transport);
    QUICR_C_TEST_CHECK(transport_result.line == 0);

    quicr_client_config_t client;
    Poison(&client, sizeof(client));
    quicr_client_config_init(&client);
    QUICR_C_TEST_CHECK(EmptyString(client.endpoint_id));
    QUICR_C_TEST_CHECK(EmptyString(client.connect_uri));
    QUICR_C_TEST_CHECK(client.unknown_stream_expiry_ms == 3000);
    QUICR_C_TEST_CHECK(client.tick_service_sleep_delay_us == 333);
    const quicr_c_test_result_t client_transport_result = CheckTransportDefaults(&client.transport_config);
    QUICR_C_TEST_CHECK(client_transport_result.line == 0);

    quicr_server_config_t server;
    Poison(&server, sizeof(server));
    quicr_server_config_init(&server);
    QUICR_C_TEST_CHECK(EmptyString(server.endpoint_id));
    QUICR_C_TEST_CHECK(EmptyString(server.bind_ip));
    QUICR_C_TEST_CHECK(server.unknown_stream_expiry_ms == 3000);
    QUICR_C_TEST_CHECK(server.port == 0);
    QUICR_C_TEST_CHECK(server.tick_service_sleep_delay_us == 333);
    const quicr_c_test_result_t server_transport_result = CheckTransportDefaults(&server.transport_config);
    QUICR_C_TEST_CHECK(server_transport_result.line == 0);

    quicr_object_headers_t headers;
    Poison(&headers, sizeof(headers));
    quicr_object_headers_init(&headers);
    QUICR_C_TEST_CHECK(headers.group_id == 0);
    QUICR_C_TEST_CHECK(headers.object_id == 0);
    QUICR_C_TEST_CHECK(headers.subgroup_id == 0);
    QUICR_C_TEST_CHECK(headers.payload_length == 0);
    QUICR_C_TEST_CHECK(headers.status == QUICR_OBJECT_STATUS_AVAILABLE);
    QUICR_C_TEST_CHECK(!headers.has_priority);
    QUICR_C_TEST_CHECK(!headers.has_ttl);
    QUICR_C_TEST_CHECK(!headers.has_track_mode);
    QUICR_C_TEST_CHECK(headers.extensions == NULL);
    QUICR_C_TEST_CHECK(headers.immutable_extensions == NULL);

    quicr_stream_header_properties_t properties;
    Poison(&properties, sizeof(properties));
    quicr_stream_header_properties_init(&properties);
    QUICR_C_TEST_CHECK(!properties.extensions);
    QUICR_C_TEST_CHECK(properties.subgroup_id_mode == QUICR_SUBGROUP_ID_TYPE_IS_ZERO);
    QUICR_C_TEST_CHECK(!properties.end_of_group);
    QUICR_C_TEST_CHECK(!properties.default_priority);
    QUICR_C_TEST_CHECK(!properties.first_object);

    quicr_location_t location = { .group = 1, .object = 2 };
    quicr_location_init(&location);
    QUICR_C_TEST_CHECK(location.group == 0);
    QUICR_C_TEST_CHECK(location.object == 0);

    quicr_subscription_filter_t subscription_filter;
    Poison(&subscription_filter, sizeof(subscription_filter));
    quicr_subscription_filter_init(&subscription_filter);
    QUICR_C_TEST_CHECK(subscription_filter.type == QUICR_SUBSCRIPTION_FILTER_TYPE_NEXT_GROUP_START);
    QUICR_C_TEST_CHECK(subscription_filter.start_location.group == 0);
    QUICR_C_TEST_CHECK(subscription_filter.start_location.object == 0);
    QUICR_C_TEST_CHECK(subscription_filter.end_group_delta == 0);

    quicr_track_filter_t track_filter;
    Poison(&track_filter, sizeof(track_filter));
    quicr_track_filter_init(&track_filter);
    QUICR_C_TEST_CHECK(track_filter.property_type == 0);
    QUICR_C_TEST_CHECK(track_filter.max_tracks_selected == 0);
    QUICR_C_TEST_CHECK(track_filter.timeout == 0);

    quicr_transport_config_init(NULL);
    quicr_client_config_init(NULL);
    quicr_server_config_init(NULL);
    quicr_object_headers_init(NULL);
    quicr_stream_header_properties_init(NULL);
    quicr_location_init(NULL);
    quicr_subscription_filter_init(NULL);
    quicr_track_filter_init(NULL);

    return QUICR_C_TEST_SUCCESS;
}

quicr_c_test_result_t
quicr_c_test_optional_attribute_getters(void)
{
    uint64_t scalar = UINT64_MAX;
    quicr_location_t location = { UINT64_MAX, UINT64_MAX };
    quicr_group_order_t group_order = QUICR_GROUP_ORDER_UNSPECIFIED;

    QUICR_C_TEST_CHECK(!quicr_publish_attributes_expires(NULL, &scalar));
    QUICR_C_TEST_CHECK(scalar == UINT64_MAX);
    QUICR_C_TEST_CHECK(!quicr_publish_attributes_largest_object(NULL, &location));
    QUICR_C_TEST_CHECK(location.group == UINT64_MAX && location.object == UINT64_MAX);
    QUICR_C_TEST_CHECK(!quicr_publish_attributes_max_cache_duration(NULL, &scalar));
    QUICR_C_TEST_CHECK(scalar == UINT64_MAX);
    QUICR_C_TEST_CHECK(!quicr_publish_attributes_delivery_timeout(NULL, &scalar));
    QUICR_C_TEST_CHECK(scalar == UINT64_MAX);
    QUICR_C_TEST_CHECK(!quicr_subscribe_attributes_group_order(NULL, &group_order));
    QUICR_C_TEST_CHECK(group_order == QUICR_GROUP_ORDER_UNSPECIFIED);
    QUICR_C_TEST_CHECK(!quicr_subscribe_attributes_new_group_request_id(NULL, &scalar));
    QUICR_C_TEST_CHECK(scalar == UINT64_MAX);

    QUICR_C_TEST_CHECK(!quicr_publish_attributes_expires(NULL, NULL));
    QUICR_C_TEST_CHECK(!quicr_publish_attributes_largest_object(NULL, NULL));
    QUICR_C_TEST_CHECK(!quicr_publish_attributes_max_cache_duration(NULL, NULL));
    QUICR_C_TEST_CHECK(!quicr_publish_attributes_delivery_timeout(NULL, NULL));
    QUICR_C_TEST_CHECK(!quicr_subscribe_attributes_group_order(NULL, NULL));
    QUICR_C_TEST_CHECK(!quicr_subscribe_attributes_new_group_request_id(NULL, NULL));

    return QUICR_C_TEST_SUCCESS;
}

quicr_c_test_result_t
quicr_c_test_responses(void)
{
    quicr_track_namespace_t* track_namespace = NULL;
    quicr_full_track_name_t* full_track_name = NULL;
    quicr_subscribe_track_handler_t* handler = NULL;
    quicr_publish_response_t* response = NULL;
    const uint8_t track_name[] = { 'r' };

    QUICR_C_TEST_CHECK(quicr_track_namespace_create(NULL, 0, &track_namespace) == QUICR_STATUS_OK);
    QUICR_C_TEST_CHECK(
      quicr_full_track_name_create(track_namespace,
                                   (quicr_byte_span_t){ .data = track_name, .size = sizeof(track_name) },
                                   &full_track_name) == QUICR_STATUS_OK);
    QUICR_C_TEST_CHECK(quicr_subscribe_track_handler_create(
                         NULL, full_track_name, 1, QUICR_GROUP_ORDER_UNSPECIFIED, NULL, &handler) == QUICR_STATUS_OK);
    QUICR_C_TEST_CHECK(quicr_publish_response_create(handler, &response) == QUICR_STATUS_OK);
    QUICR_C_TEST_CHECK(response != NULL);

    QUICR_C_TEST_CHECK(quicr_publish_response_set_subscriber_priority(response, 2) == QUICR_STATUS_OK);
    QUICR_C_TEST_CHECK(quicr_publish_response_set_group_order(response, QUICR_GROUP_ORDER_ASCENDING) ==
                       QUICR_STATUS_OK);
    QUICR_C_TEST_CHECK(quicr_publish_response_set_group_order(response, QUICR_GROUP_ORDER_UNSPECIFIED) ==
                       QUICR_STATUS_OK);
    QUICR_C_TEST_CHECK(quicr_publish_response_set_forward(response, true) == QUICR_STATUS_OK);
    QUICR_C_TEST_CHECK(quicr_publish_response_set_subgroup_delivery_timeout(response, 100) == QUICR_STATUS_OK);
    QUICR_C_TEST_CHECK(quicr_publish_response_set_object_delivery_timeout(response, 200) == QUICR_STATUS_OK);
    QUICR_C_TEST_CHECK(quicr_publish_response_set_new_group_request_id(response, 3) == QUICR_STATUS_OK);

    quicr_filter_t filter = {
        .type = QUICR_FILTER_TYPE_SUBSCRIPTION_FILTER,
        .subscription = {
          .type = QUICR_SUBSCRIPTION_FILTER_TYPE_ABSOLUTE_START,
          .start_location = { .group = 4, .object = 5 },
        },
    };
    QUICR_C_TEST_CHECK(quicr_publish_response_set_filter(response, &filter) == QUICR_STATUS_OK);

    QUICR_C_TEST_CHECK(quicr_subscribe_track_handler_destroy(handler) == QUICR_STATUS_BUSY);
    QUICR_C_TEST_CHECK(quicr_publish_response_set_forward(response, false) == QUICR_STATUS_OK);

    QUICR_C_TEST_CHECK(quicr_publish_response_set_subscriber_priority(NULL, 0) == QUICR_STATUS_INVALID_ARGUMENT);
    QUICR_C_TEST_CHECK(quicr_publish_response_set_group_order(NULL, 0) == QUICR_STATUS_INVALID_ARGUMENT);
    QUICR_C_TEST_CHECK(quicr_publish_response_set_group_order(response, UINT8_MAX) == QUICR_STATUS_INVALID_ARGUMENT);
    QUICR_C_TEST_CHECK(quicr_publish_response_set_filter(NULL, &filter) == QUICR_STATUS_INVALID_ARGUMENT);
    QUICR_C_TEST_CHECK(quicr_publish_response_set_filter(response, NULL) == QUICR_STATUS_INVALID_ARGUMENT);
    QUICR_C_TEST_CHECK(quicr_publish_response_set_subgroup_delivery_timeout(response, UINT64_MAX) ==
                       QUICR_STATUS_INVALID_ARGUMENT);
    QUICR_C_TEST_CHECK(quicr_publish_response_set_object_delivery_timeout(response, UINT64_MAX) ==
                       QUICR_STATUS_INVALID_ARGUMENT);

    const quicr_redirect_t redirect = {
        .connect_uri = "moq://redirect.example:443/relay",
        .track_namespace = track_namespace,
        .track_name = NULL,
    };
    const quicr_publish_namespace_reply_t redirect_reply = {
        .result = QUICR_REPLY_RESULT_REJECT,
        .error = {
          .code = QUICR_PUBLISH_NAMESPACE_ERROR_REDIRECT,
          .reason = "use another relay",
          .redirect = &redirect,
        },
    };
    QUICR_C_TEST_CHECK(redirect_reply.error.redirect->track_namespace == track_namespace);

    quicr_publish_response_destroy(response);
    quicr_publish_response_destroy(NULL);
    quicr_full_track_name_destroy(full_track_name);
    quicr_track_namespace_destroy(track_namespace);

    QUICR_C_TEST_CHECK(quicr_publish_response_create(NULL, &response) == QUICR_STATUS_INVALID_ARGUMENT);
    QUICR_C_TEST_CHECK(quicr_publish_response_create(handler, NULL) == QUICR_STATUS_INVALID_ARGUMENT);
    QUICR_C_TEST_CHECK(quicr_subscribe_track_handler_destroy(handler) == QUICR_STATUS_OK);
    return QUICR_C_TEST_SUCCESS;
}

typedef struct test_names
{
    quicr_track_namespace_t* track_namespace;
    quicr_full_track_name_t* full_track_name;
} test_names_t;

static quicr_status_t
CreateTestNames(test_names_t* names)
{
    const uint8_t namespace_data[] = { 'n' };
    const uint8_t track_data[] = { 't' };
    const quicr_byte_span_t entry = { .data = namespace_data, .size = sizeof(namespace_data) };

    names->track_namespace = NULL;
    names->full_track_name = NULL;
    quicr_status_t status = quicr_track_namespace_create(&entry, 1, &names->track_namespace);
    if (status != QUICR_STATUS_OK) {
        return status;
    }

    status = quicr_full_track_name_create(names->track_namespace,
                                          (quicr_byte_span_t){ .data = track_data, .size = sizeof(track_data) },
                                          &names->full_track_name);
    if (status != QUICR_STATUS_OK) {
        quicr_track_namespace_destroy(names->track_namespace);
        names->track_namespace = NULL;
    }

    return status;
}

static void
DestroyTestNames(test_names_t* names)
{
    quicr_full_track_name_destroy(names->full_track_name);
    quicr_track_namespace_destroy(names->track_namespace);
}

quicr_c_test_result_t
quicr_c_test_handlers(void)
{
    test_names_t names;
    QUICR_C_TEST_CHECK(CreateTestNames(&names) == QUICR_STATUS_OK);

    quicr_publish_track_handler_t* publish = NULL;
    QUICR_C_TEST_CHECK(quicr_publish_track_handler_create(
                         NULL, names.full_track_name, QUICR_TRACK_MODE_STREAM, 1, 2, 1000, NULL, &publish) ==
                       QUICR_STATUS_OK);
    quicr_publish_track_handler_status_t publish_status;
    QUICR_C_TEST_CHECK(quicr_publish_track_handler_get_status(publish, &publish_status) == QUICR_STATUS_OK);
    QUICR_C_TEST_CHECK(!quicr_publish_track_handler_can_publish(publish));

    uint8_t priority = 0;
    uint32_t ttl = 0;
    quicr_track_mode_t track_mode = QUICR_TRACK_MODE_DATAGRAM;
    QUICR_C_TEST_CHECK(quicr_publish_track_handler_get_publisher_priority(publish, &priority) == QUICR_STATUS_OK);
    QUICR_C_TEST_CHECK(priority == 1);
    QUICR_C_TEST_CHECK(quicr_publish_track_handler_set_publisher_priority(publish, 3) == QUICR_STATUS_OK);
    QUICR_C_TEST_CHECK(quicr_publish_track_handler_get_publisher_priority(publish, &priority) == QUICR_STATUS_OK);
    QUICR_C_TEST_CHECK(priority == 3);
    QUICR_C_TEST_CHECK(quicr_publish_track_handler_get_publish_object_priority(publish, &priority) == QUICR_STATUS_OK);
    QUICR_C_TEST_CHECK(priority == 2);
    QUICR_C_TEST_CHECK(quicr_publish_track_handler_set_publish_object_priority(publish, 4) == QUICR_STATUS_OK);
    QUICR_C_TEST_CHECK(quicr_publish_track_handler_get_publish_object_priority(publish, &priority) == QUICR_STATUS_OK);
    QUICR_C_TEST_CHECK(priority == 4);
    QUICR_C_TEST_CHECK(quicr_publish_track_handler_get_default_ttl(publish, &ttl) == QUICR_STATUS_OK);
    QUICR_C_TEST_CHECK(ttl == 1000);
    QUICR_C_TEST_CHECK(quicr_publish_track_handler_set_default_ttl(publish, 2000) == QUICR_STATUS_OK);
    QUICR_C_TEST_CHECK(quicr_publish_track_handler_get_default_ttl(publish, &ttl) == QUICR_STATUS_OK);
    QUICR_C_TEST_CHECK(ttl == 2000);
    QUICR_C_TEST_CHECK(quicr_publish_track_handler_get_default_track_mode(publish, &track_mode) == QUICR_STATUS_OK);
    QUICR_C_TEST_CHECK(track_mode == QUICR_TRACK_MODE_STREAM);
    QUICR_C_TEST_CHECK(quicr_publish_track_handler_set_default_track_mode(publish, QUICR_TRACK_MODE_DATAGRAM) ==
                       QUICR_STATUS_OK);
    QUICR_C_TEST_CHECK(quicr_publish_track_handler_get_default_track_mode(publish, &track_mode) == QUICR_STATUS_OK);
    QUICR_C_TEST_CHECK(track_mode == QUICR_TRACK_MODE_DATAGRAM);

    QUICR_C_TEST_CHECK(quicr_publish_track_handler_get_publisher_priority(NULL, &priority) ==
                       QUICR_STATUS_INVALID_ARGUMENT);
    QUICR_C_TEST_CHECK(quicr_publish_track_handler_get_publisher_priority(publish, NULL) ==
                       QUICR_STATUS_INVALID_ARGUMENT);
    QUICR_C_TEST_CHECK(quicr_publish_track_handler_set_publisher_priority(NULL, 0) == QUICR_STATUS_INVALID_ARGUMENT);
    QUICR_C_TEST_CHECK(quicr_publish_track_handler_get_publish_object_priority(NULL, &priority) ==
                       QUICR_STATUS_INVALID_ARGUMENT);
    QUICR_C_TEST_CHECK(quicr_publish_track_handler_get_publish_object_priority(publish, NULL) ==
                       QUICR_STATUS_INVALID_ARGUMENT);
    QUICR_C_TEST_CHECK(quicr_publish_track_handler_set_publish_object_priority(NULL, 0) ==
                       QUICR_STATUS_INVALID_ARGUMENT);
    QUICR_C_TEST_CHECK(quicr_publish_track_handler_get_default_ttl(NULL, &ttl) == QUICR_STATUS_INVALID_ARGUMENT);
    QUICR_C_TEST_CHECK(quicr_publish_track_handler_get_default_ttl(publish, NULL) == QUICR_STATUS_INVALID_ARGUMENT);
    QUICR_C_TEST_CHECK(quicr_publish_track_handler_set_default_ttl(NULL, 0) == QUICR_STATUS_INVALID_ARGUMENT);
    QUICR_C_TEST_CHECK(quicr_publish_track_handler_get_default_track_mode(NULL, &track_mode) ==
                       QUICR_STATUS_INVALID_ARGUMENT);
    QUICR_C_TEST_CHECK(quicr_publish_track_handler_get_default_track_mode(publish, NULL) ==
                       QUICR_STATUS_INVALID_ARGUMENT);
    QUICR_C_TEST_CHECK(quicr_publish_track_handler_set_default_track_mode(NULL, QUICR_TRACK_MODE_STREAM) ==
                       QUICR_STATUS_INVALID_ARGUMENT);
    QUICR_C_TEST_CHECK(quicr_publish_track_handler_set_default_track_mode(publish, UINT8_MAX) ==
                       QUICR_STATUS_INVALID_ARGUMENT);

    quicr_object_headers_t headers;
    quicr_object_headers_init(&headers);
    quicr_publish_object_status_t object_status = QUICR_PUBLISH_OBJECT_STATUS_OK;
    QUICR_C_TEST_CHECK(quicr_publish_track_handler_publish_object(
                         publish, &headers, (quicr_byte_span_t){ 0 }, NULL, &object_status) == QUICR_STATUS_OK);
    QUICR_C_TEST_CHECK(object_status != QUICR_PUBLISH_OBJECT_STATUS_OK);
    QUICR_C_TEST_CHECK(quicr_publish_track_handler_destroy(publish) == QUICR_STATUS_OK);

    quicr_publish_fetch_handler_t* publish_fetch = NULL;
    QUICR_C_TEST_CHECK(quicr_publish_fetch_handler_create(
                         NULL, names.full_track_name, 1, 7, QUICR_GROUP_ORDER_ASCENDING, 1000, &publish_fetch) ==
                       QUICR_STATUS_OK);
    quicr_publish_fetch_handler_status_t publish_fetch_status;
    QUICR_C_TEST_CHECK(quicr_publish_fetch_handler_get_status(publish_fetch, &publish_fetch_status) == QUICR_STATUS_OK);
    QUICR_C_TEST_CHECK(!quicr_publish_fetch_handler_can_publish(publish_fetch));
    QUICR_C_TEST_CHECK(quicr_publish_fetch_handler_destroy(publish_fetch) == QUICR_STATUS_OK);

    quicr_subscribe_track_handler_t* subscribe = NULL;
    QUICR_C_TEST_CHECK(quicr_subscribe_track_handler_create(
                         NULL, names.full_track_name, 1, QUICR_GROUP_ORDER_UNSPECIFIED, NULL, &subscribe) ==
                       QUICR_STATUS_OK);
    quicr_subscribe_track_handler_status_t subscribe_status;
    QUICR_C_TEST_CHECK(quicr_subscribe_track_handler_get_status(subscribe, &subscribe_status) == QUICR_STATUS_OK);
    QUICR_C_TEST_CHECK(quicr_subscribe_track_handler_pause(subscribe) == QUICR_STATUS_NOT_APPLIED);
    QUICR_C_TEST_CHECK(quicr_subscribe_track_handler_resume(subscribe) == QUICR_STATUS_NOT_APPLIED);
    QUICR_C_TEST_CHECK(quicr_subscribe_track_handler_request_new_group(subscribe, 0) == QUICR_STATUS_NOT_APPLIED);
    QUICR_C_TEST_CHECK(quicr_subscribe_track_handler_pause(NULL) == QUICR_STATUS_INVALID_ARGUMENT);
    QUICR_C_TEST_CHECK(quicr_subscribe_track_handler_resume(NULL) == QUICR_STATUS_INVALID_ARGUMENT);
    QUICR_C_TEST_CHECK(quicr_subscribe_track_handler_request_new_group(NULL, 0) == QUICR_STATUS_INVALID_ARGUMENT);
    QUICR_C_TEST_CHECK(quicr_subscribe_track_handler_destroy(subscribe) == QUICR_STATUS_OK);

    quicr_subscribe_track_handler_t* forwarding = NULL;
    QUICR_C_TEST_CHECK(quicr_forwarding_subscribe_track_handler_create(
                         NULL, names.full_track_name, 1, QUICR_GROUP_ORDER_UNSPECIFIED, NULL, &forwarding) ==
                       QUICR_STATUS_OK);
    QUICR_C_TEST_CHECK(quicr_subscribe_track_handler_destroy(forwarding) == QUICR_STATUS_OK);

    const quicr_location_t start = { .group = 0, .object = 0 };
    const quicr_fetch_end_location_t end = { .group = 1, .has_object = true, .object = 2 };
    quicr_fetch_track_handler_t* fetch = NULL;
    QUICR_C_TEST_CHECK(quicr_fetch_track_handler_create(
                         NULL, names.full_track_name, 1, &start, &end, QUICR_GROUP_ORDER_ASCENDING, &fetch) ==
                       QUICR_STATUS_OK);
    quicr_fetch_track_handler_status_t fetch_status;
    QUICR_C_TEST_CHECK(quicr_fetch_track_handler_get_status(fetch, &fetch_status) == QUICR_STATUS_OK);
    QUICR_C_TEST_CHECK(quicr_fetch_track_handler_pause(fetch) == QUICR_STATUS_NOT_APPLIED);
    QUICR_C_TEST_CHECK(quicr_fetch_track_handler_resume(fetch) == QUICR_STATUS_NOT_APPLIED);
    QUICR_C_TEST_CHECK(quicr_fetch_track_handler_request_new_group(fetch, 0) == QUICR_STATUS_NOT_APPLIED);
    QUICR_C_TEST_CHECK(quicr_fetch_track_handler_pause(NULL) == QUICR_STATUS_INVALID_ARGUMENT);
    QUICR_C_TEST_CHECK(quicr_fetch_track_handler_resume(NULL) == QUICR_STATUS_INVALID_ARGUMENT);
    QUICR_C_TEST_CHECK(quicr_fetch_track_handler_request_new_group(NULL, 0) == QUICR_STATUS_INVALID_ARGUMENT);
    QUICR_C_TEST_CHECK(quicr_fetch_track_handler_destroy(fetch) == QUICR_STATUS_OK);

    quicr_publish_namespace_handler_t* publish_namespace = NULL;
    QUICR_C_TEST_CHECK(quicr_publish_namespace_handler_create(NULL, names.track_namespace, &publish_namespace) ==
                       QUICR_STATUS_OK);
    quicr_publish_namespace_handler_status_t publish_namespace_status;
    QUICR_C_TEST_CHECK(quicr_publish_namespace_handler_get_status(publish_namespace, &publish_namespace_status) ==
                       QUICR_STATUS_OK);
    quicr_error_code_t error_code = 0;
    size_t reason_size = 0;
    QUICR_C_TEST_CHECK(quicr_publish_namespace_handler_get_error(publish_namespace, &error_code, NULL, &reason_size) ==
                       QUICR_STATUS_NOT_FOUND);
    QUICR_C_TEST_CHECK(quicr_publish_namespace_handler_destroy(publish_namespace) == QUICR_STATUS_OK);

    quicr_subscribe_namespace_handler_t* subscribe_namespace = NULL;
    QUICR_C_TEST_CHECK(
      quicr_subscribe_namespace_handler_create(
        NULL, names.track_namespace, QUICR_SUBSCRIBE_NAMESPACE_MODE_NAMESPACES, NULL, &subscribe_namespace) ==
      QUICR_STATUS_OK);
    quicr_subscribe_namespace_handler_status_t subscribe_namespace_status;
    QUICR_C_TEST_CHECK(quicr_subscribe_namespace_handler_get_status(subscribe_namespace, &subscribe_namespace_status) ==
                       QUICR_STATUS_OK);
    QUICR_C_TEST_CHECK(quicr_subscribe_namespace_handler_get_error(
                         subscribe_namespace, &error_code, NULL, &reason_size) == QUICR_STATUS_NOT_FOUND);
    QUICR_C_TEST_CHECK(quicr_subscribe_namespace_handler_destroy(subscribe_namespace) == QUICR_STATUS_OK);

    quicr_track_status_track_handler_t* track_status = NULL;
    QUICR_C_TEST_CHECK(quicr_track_status_handler_create(NULL, names.full_track_name, &track_status) ==
                       QUICR_STATUS_OK);
    quicr_track_status_handler_status_t track_status_value;
    QUICR_C_TEST_CHECK(quicr_track_status_handler_get_status(track_status, &track_status_value) == QUICR_STATUS_OK);
    bool has_response = true;
    quicr_track_status_response_t response;
    QUICR_C_TEST_CHECK(quicr_track_status_handler_get_response(track_status, &has_response, &response, NULL) ==
                       QUICR_STATUS_OK);
    QUICR_C_TEST_CHECK(!has_response);
    QUICR_C_TEST_CHECK(quicr_track_status_handler_get_error(track_status, &error_code, NULL, &reason_size) ==
                       QUICR_STATUS_NOT_FOUND);
    QUICR_C_TEST_CHECK(quicr_track_status_handler_destroy(track_status) == QUICR_STATUS_OK);

    QUICR_C_TEST_CHECK(quicr_publish_track_handler_destroy(NULL) == QUICR_STATUS_OK);
    QUICR_C_TEST_CHECK(quicr_publish_fetch_handler_destroy(NULL) == QUICR_STATUS_OK);
    QUICR_C_TEST_CHECK(quicr_subscribe_track_handler_destroy(NULL) == QUICR_STATUS_OK);
    QUICR_C_TEST_CHECK(quicr_fetch_track_handler_destroy(NULL) == QUICR_STATUS_OK);
    QUICR_C_TEST_CHECK(quicr_publish_namespace_handler_destroy(NULL) == QUICR_STATUS_OK);
    QUICR_C_TEST_CHECK(quicr_subscribe_namespace_handler_destroy(NULL) == QUICR_STATUS_OK);
    QUICR_C_TEST_CHECK(quicr_track_status_handler_destroy(NULL) == QUICR_STATUS_OK);

    DestroyTestNames(&names);
    return QUICR_C_TEST_SUCCESS;
}

quicr_c_test_result_t
quicr_c_test_generic_handlers(void)
{
    test_names_t names;
    QUICR_C_TEST_CHECK(CreateTestNames(&names) == QUICR_STATUS_OK);

    quicr_publish_track_handler_t* publish = NULL;
    quicr_publish_fetch_handler_t* publish_fetch = NULL;
    quicr_subscribe_track_handler_t* subscribe = NULL;
    quicr_fetch_track_handler_t* fetch = NULL;
    quicr_publish_namespace_handler_t* publish_namespace = NULL;
    quicr_subscribe_namespace_handler_t* subscribe_namespace = NULL;
    quicr_track_status_track_handler_t* track_status = NULL;
    const quicr_location_t start = { 0 };
    const quicr_fetch_end_location_t end = { .group = 1 };

    QUICR_C_TEST_CHECK(quicr_publish_track_handler_create(
                         NULL, names.full_track_name, QUICR_TRACK_MODE_STREAM, 1, 1, 1000, NULL, &publish) ==
                       QUICR_STATUS_OK);
    QUICR_C_TEST_CHECK(quicr_publish_fetch_handler_create(
                         NULL, names.full_track_name, 1, 7, QUICR_GROUP_ORDER_ASCENDING, 1000, &publish_fetch) ==
                       QUICR_STATUS_OK);
    QUICR_C_TEST_CHECK(quicr_subscribe_track_handler_create(
                         NULL, names.full_track_name, 1, QUICR_GROUP_ORDER_UNSPECIFIED, NULL, &subscribe) ==
                       QUICR_STATUS_OK);
    QUICR_C_TEST_CHECK(quicr_fetch_track_handler_create(
                         NULL, names.full_track_name, 1, &start, &end, QUICR_GROUP_ORDER_ASCENDING, &fetch) ==
                       QUICR_STATUS_OK);
    QUICR_C_TEST_CHECK(quicr_publish_namespace_handler_create(NULL, names.track_namespace, &publish_namespace) ==
                       QUICR_STATUS_OK);
    QUICR_C_TEST_CHECK(
      quicr_subscribe_namespace_handler_create(
        NULL, names.track_namespace, QUICR_SUBSCRIBE_NAMESPACE_MODE_NAMESPACES, NULL, &subscribe_namespace) ==
      QUICR_STATUS_OK);
    QUICR_C_TEST_CHECK(quicr_track_status_handler_create(NULL, names.full_track_name, &track_status) ==
                       QUICR_STATUS_OK);

    quicr_publish_track_handler_status_t publish_status;
    quicr_publish_fetch_handler_status_t publish_fetch_status;
    quicr_subscribe_track_handler_status_t subscribe_status;
    quicr_fetch_track_handler_status_t fetch_status;
    quicr_publish_namespace_handler_status_t publish_namespace_status;
    quicr_subscribe_namespace_handler_status_t subscribe_namespace_status;
    quicr_track_status_handler_status_t track_status_value;
    QUICR_C_TEST_CHECK(quicr_track_handler_get_status(publish, &publish_status) == QUICR_STATUS_OK);
    QUICR_C_TEST_CHECK(quicr_track_handler_get_status(publish_fetch, &publish_fetch_status) == QUICR_STATUS_OK);
    QUICR_C_TEST_CHECK(quicr_track_handler_get_status(subscribe, &subscribe_status) == QUICR_STATUS_OK);
    QUICR_C_TEST_CHECK(quicr_track_handler_get_status(fetch, &fetch_status) == QUICR_STATUS_OK);
    QUICR_C_TEST_CHECK(quicr_track_handler_get_status(publish_namespace, &publish_namespace_status) == QUICR_STATUS_OK);
    QUICR_C_TEST_CHECK(quicr_track_handler_get_status(subscribe_namespace, &subscribe_namespace_status) ==
                       QUICR_STATUS_OK);
    QUICR_C_TEST_CHECK(quicr_track_handler_get_status(track_status, &track_status_value) == QUICR_STATUS_OK);

    QUICR_C_TEST_CHECK(quicr_session_manager_add_handler(NULL, NULL, publish) == QUICR_STATUS_INVALID_ARGUMENT);
    QUICR_C_TEST_CHECK(quicr_session_manager_add_handler(NULL, NULL, subscribe) == QUICR_STATUS_INVALID_ARGUMENT);
    QUICR_C_TEST_CHECK(quicr_session_manager_add_handler(NULL, NULL, fetch) == QUICR_STATUS_INVALID_ARGUMENT);
    QUICR_C_TEST_CHECK(quicr_session_manager_add_handler(NULL, NULL, publish_namespace) ==
                       QUICR_STATUS_INVALID_ARGUMENT);
    QUICR_C_TEST_CHECK(quicr_session_manager_add_handler(NULL, NULL, subscribe_namespace) ==
                       QUICR_STATUS_INVALID_ARGUMENT);
    QUICR_C_TEST_CHECK(quicr_session_manager_add_handler(NULL, NULL, track_status) == QUICR_STATUS_INVALID_ARGUMENT);
    QUICR_C_TEST_CHECK(quicr_session_bind_publish_track_handler(NULL, 1U, 2U, publish, false) ==
                       QUICR_STATUS_INVALID_ARGUMENT);
    QUICR_C_TEST_CHECK(quicr_session_unbind_publish_track_handler(NULL, 1U, publish, false) ==
                       QUICR_STATUS_INVALID_ARGUMENT);
    QUICR_C_TEST_CHECK(quicr_session_send_namespace(NULL, names.track_namespace) == QUICR_STATUS_INVALID_ARGUMENT);
    QUICR_C_TEST_CHECK(quicr_session_send_namespace_done(NULL, names.track_namespace) == QUICR_STATUS_INVALID_ARGUMENT);

    QUICR_C_TEST_CHECK(quicr_track_handler_destroy(publish) == QUICR_STATUS_OK);
    QUICR_C_TEST_CHECK(quicr_track_handler_destroy(publish_fetch) == QUICR_STATUS_OK);
    QUICR_C_TEST_CHECK(quicr_track_handler_destroy(subscribe) == QUICR_STATUS_OK);
    QUICR_C_TEST_CHECK(quicr_track_handler_destroy(fetch) == QUICR_STATUS_OK);
    QUICR_C_TEST_CHECK(quicr_track_handler_destroy(publish_namespace) == QUICR_STATUS_OK);
    QUICR_C_TEST_CHECK(quicr_track_handler_destroy(subscribe_namespace) == QUICR_STATUS_OK);
    QUICR_C_TEST_CHECK(quicr_track_handler_destroy(track_status) == QUICR_STATUS_OK);

    DestroyTestNames(&names);
    return QUICR_C_TEST_SUCCESS;
}
