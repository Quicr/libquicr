// SPDX-FileCopyrightText: Copyright (c) 2026 Cisco Systems
// SPDX-License-Identifier: BSD-2-Clause

#ifndef QUICR_C_H
#define QUICR_C_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

    typedef int32_t quicr_status_t;
    enum
    {
        QUICR_STATUS_OK = 0,
        QUICR_STATUS_INVALID_ARGUMENT = 1,
        QUICR_STATUS_OUT_OF_MEMORY = 2,
        QUICR_STATUS_INTERNAL_ERROR = 3,
    };

    /*=======================================================================*/
    // Core Types
    /*=======================================================================*/

    typedef struct
    {
        const uint8_t* data;
        size_t size;
    } quicr_byte_span_t;

    typedef struct
    {
        const quicr_byte_span_t* entries;
        size_t count;
    } quicr_track_namespace_t;

    typedef struct
    {
        quicr_track_namespace_t ns;
        quicr_byte_span_t track_name;
    } quicr_full_track_name_t;

    typedef enum
    {
        QUICR_CONGESTION_CONTROL_BBR,
        QUICR_CONGESTION_CONTROL_C4,
        QUICR_CONGESTION_CONTROL_CUBIC,
        QUICR_CONGESTION_CONTROL_NEW_RENO,
    } quicr_congestion_control_t;

    typedef struct
    {
        const char* tls_cert_filename;
        const char* tls_key_filename;
        uint32_t time_queue_init_queue_size;
        uint32_t time_queue_max_duration;
        uint32_t time_queue_bucket_interval;
        uint32_t time_queue_rx_size;
        bool debug;
        uint64_t quic_cwin_minimum;
        uint32_t quic_wifi_shadow_rtt_us;
        uint64_t idle_timeout_ms;
        quicr_congestion_control_t congestion_control;
        const char* quic_qlog_path;
        uint8_t quic_priority_limit;
        size_t max_connections;
        bool ssl_keylog;
        size_t socket_buffer_size;
        uint32_t callback_queue_size;
        uint64_t metrics_sample_ms;
        uint64_t initial_max_stream_data;
        uint64_t initial_max_stream_id;
        size_t quic_shards;
        bool use_af_xdp;
    } quicr_transport_config_t;

    void quicr_transport_config_init(quicr_transport_config_t* config);

    typedef struct
    {
        const char* endpoint_id;
        quicr_transport_config_t transport_config;
        uint32_t unknown_stream_expiry_ms;

        const char* connect_uri;
        uint64_t tick_service_sleep_delay_us;
    } quicr_client_config_t;

    void quicr_client_config_init(quicr_client_config_t* config);

    typedef struct
    {
        const char* endpoint_id;
        quicr_transport_config_t transport_config;
        uint32_t unknown_stream_expiry_ms;

        const char* bind_ip;
        uint16_t port;
        uint64_t tick_service_sleep_delay_us;
    } quicr_server_config_t;

    void quicr_server_config_init(quicr_server_config_t* config);

    /*=======================================================================*/
    // Connection
    /*=======================================================================*/

    typedef enum
    {
        QUICR_STREAM_CLOSED_FLAG_FIN,
        QUICR_STREAM_CLOSED_FLAG_RESET,
        QUICR_STREAM_CLOSED_FLAG_STOP_SENDING,
    } quicr_stream_closed_flag_t;

    /*=======================================================================*/
    // Session
    /*=======================================================================*/

    typedef struct quicr_session quicr_session_t;

    typedef enum
    {
        QUICR_SESSION_STATUS_READY = 0,
        QUICR_SESSION_STATUS_NOT_READY,
        QUICR_SESSION_STATUS_INTERNAL_ERROR,
        QUICR_SESSION_STATUS_INVALID_PARAMS,
        QUICR_SESSION_STATUS_CONNECTING,
        QUICR_SESSION_STATUS_DISCONNECTING,
        QUICR_SESSION_STATUS_NOT_CONNECTED,
        QUICR_SESSION_STATUS_FAILED_TO_CONNECT,
        QUICR_SESSION_STATUS_PENDING_PEER_SETUP,
    } quicr_session_status_t;

    typedef struct quicr_session_callbacks
    {
        void* user_data;

        void (*on_stream_closed)(uint64_t stream_id, quicr_stream_closed_flag_t flag);
        void (*status_changed)(quicr_session_t* session, quicr_session_status_t status, void* user_data);
    } quicr_session_callbacks_t;

    typedef struct quicr_session_client_callbacks
    {
        quicr_session_callbacks_t common;
    } quicr_session_client_callbacks_t;

    typedef struct quicr_session_server_callbacks
    {
        quicr_session_callbacks_t common;
    } quicr_session_server_callbacks_t;

    quicr_status_t quicr_session_get_status(const quicr_session_t* session, quicr_session_status_t* status);

    /*=======================================================================*/
    // Handlers
    /*=======================================================================*/

    typedef struct quicr_track_handler quicr_track_handler_t;

    typedef enum
    {
        QUICR_TRACK_MODE_DATAGRAM,
        QUICR_TRACK_MODE_STREAM,
    } quicr_track_mode_t;

    typedef enum
    {
        QUICR_OBJECT_STATUS_AVAILABLE = 0x0,
        QUICR_OBJECT_STATUS_DOES_NOT_EXIST = 0x1,
        QUICR_OBJECT_STATUS_END_OF_GROUP = 0x3,
        QUICR_OBJECT_STATUS_END_OF_TRACK = 0x4,
        QUICR_OBJECT_STATUS_END_OF_SUBGROUP = 0x5
    } quicr_object_status_t;

    typedef struct
    {
        uint64_t type;
        const quicr_byte_span_t* values;
        size_t value_count;
    } quicr_extension_entry_t;

    typedef struct
    {
        const quicr_extension_entry_t* entries;
        size_t entry_count;
    } quicr_extensions_t;

    typedef struct
    {
        uint64_t group_id;
        uint64_t object_id;
        uint64_t subgroup_id;
        uint64_t payload_length;
        quicr_object_status_t status;

        bool has_priority;
        uint8_t priority;

        bool has_ttl;
        uint16_t ttl;

        bool has_track_mode;
        quicr_track_mode_t track_mode;

        const quicr_extensions_t* extensions;
        const quicr_extensions_t* immutable_extensions;
    } quicr_object_headers_t;

    void quicr_object_headers_init(quicr_object_headers_t* headers);

    typedef enum
    {
        QUICR_SUBGROUP_ID_TYPE_IS_ZERO = 1 << 0u,
        QUICR_SUBGROUP_ID_TYPE_SET_FROM_FIRST_OBJECT = 1 << 1u,
        QUICR_SUBGROUP_ID_TYPE_EXPLICIT = 1 << 2u,
        QUICR_SUBGROUP_ID_TYPE_RESERVED = 1 << 3u,
    } quicr_subgroup_id_type_t;

    typedef struct
    {
        bool extensions;
        quicr_subgroup_id_type_t subgroup_id_mode;
        bool end_of_group;
        bool default_priority;
        bool first_object;
    } quicr_stream_header_properties_t;

    void quicr_stream_header_properties_init(quicr_stream_header_properties_t* properties);

    typedef struct
    {
        uint64_t group;
        uint64_t object;
    } quicr_location_t;

    void quicr_location_init(quicr_location_t* location);

    typedef struct quicr_publish_track_handler quicr_publish_track_handler_t;

    const quicr_track_handler_t* quicr_publish_track_handler_as_track_handler(
      const quicr_publish_track_handler_t* handler);

    quicr_status_t quicr_publish_track_handler_create(const quicr_full_track_name_t* ftn,
                                                      quicr_track_mode_t track_mode,
                                                      uint8_t publisher_priority,
                                                      uint8_t publish_object_priority,
                                                      uint32_t default_ttl,
                                                      const quicr_location_t* largest_location,
                                                      quicr_publish_track_handler_t** handler);

    void quicr_publish_track_handler_destroy(quicr_publish_track_handler_t* handler);

    bool quicr_publish_track_handler_can_publish(const quicr_publish_track_handler_t* handler);

    typedef enum
    {
        QUICR_PUBLISH_TRACK_HANDLER_STATUS_OK = 0,
        QUICR_PUBLISH_TRACK_HANDLER_STATUS_NOT_CONNECTED,
        QUICR_PUBLISH_TRACK_HANDLER_STATUS_NOT_ANNOUNCED,
        QUICR_PUBLISH_TRACK_HANDLER_STATUS_PENDING_ANNOUNCE_RESPONSE,
        QUICR_PUBLISH_TRACK_HANDLER_STATUS_ANNOUNCE_NOT_AUTHORIZED,
        QUICR_PUBLISH_TRACK_HANDLER_STATUS_NO_SUBSCRIBERS,
        QUICR_PUBLISH_TRACK_HANDLER_STATUS_UNSUBSCRIBED,
        QUICR_PUBLISH_TRACK_HANDLER_STATUS_DONE_BY_FIN,
        QUICR_PUBLISH_TRACK_HANDLER_STATUS_SENDING_UNANNOUNCE,
        QUICR_PUBLISH_TRACK_HANDLER_STATUS_SUBSCRIPTION_UPDATED,
        QUICR_PUBLISH_TRACK_HANDLER_STATUS_NEW_GROUP_REQUESTED,
        QUICR_PUBLISH_TRACK_HANDLER_STATUS_PENDING_PUBLISH_OK,
        QUICR_PUBLISH_TRACK_HANDLER_STATUS_PAUSED,
    } quicr_publish_track_handler_status_t;

    quicr_status_t quicr_publish_track_handler_get_status(const quicr_publish_track_handler_t* handler,
                                                          quicr_publish_track_handler_status_t* status);

    typedef enum
    {
        QUICR_PUBLISH_OBJECT_STATUS_OK = 0,
        QUICR_PUBLISH_OBJECT_STATUS_INTERNAL_ERROR,
        QUICR_PUBLISH_OBJECT_STATUS_NOT_AUTHORIZED,
        QUICR_PUBLISH_OBJECT_STATUS_NOT_ANNOUNCED,
        QUICR_PUBLISH_OBJECT_STATUS_NO_SUBSCRIBERS,
        QUICR_PUBLISH_OBJECT_STATUS_OBJECT_PAYLOAD_LENGTH_EXCEEDED,
        QUICR_PUBLISH_OBJECT_STATUS_PREVIOUS_OBJECT_TRUNCATED,
        QUICR_PUBLISH_OBJECT_STATUS_NO_PREVIOUS_OBJECT,
        QUICR_PUBLISH_OBJECT_STATUS_OBJECT_DATA_COMPLETE,
        QUICR_PUBLISH_OBJECT_STATUS_OBJECT_CONTINUATION_DATA_NEEDED,
        QUICR_PUBLISH_OBJECT_STATUS_OBJECT_DATA_TOO_LARGE,
        QUICR_PUBLISH_OBJECT_STATUS_OBJECT_DATA_INCOMPLETE,
        QUICR_PUBLISH_OBJECT_STATUS_PREVIOUS_OBJECT_NOT_COMPLETE_MUST_START_NEW_GROUP,
        QUICR_PUBLISH_OBJECT_STATUS_PREVIOUS_OBJECT_NOT_COMPLETE_MUST_START_NEW_TRACK,
        QUICR_PUBLISH_OBJECT_STATUS_PAUSED,
        QUICR_PUBLISH_OBJECT_STATUS_PENDING_PUBLISH_OK,
    } quicr_publish_object_status_t;

    quicr_status_t quicr_publish_track_handler_publish_object(quicr_publish_track_handler_t* handler,
                                                              const quicr_object_headers_t* headers,
                                                              quicr_byte_span_t data,
                                                              const quicr_stream_header_properties_t* properties,
                                                              quicr_publish_object_status_t* status);

    typedef struct quicr_subscribe_track_handler quicr_subscribe_track_handler_t;

    typedef struct quicr_fetch_track_handler quicr_fetch_track_handler_t;

    typedef struct quicr_publish_namespace_handler quicr_publish_namespace_handler_t;

    typedef struct quicr_subscribe_namespace_handler quicr_subscribe_namespace_handler_t;

    /*=======================================================================*/
    // Session Manager
    /*=======================================================================*/

    typedef struct quicr_session_manager quicr_session_manager_t;

    typedef struct quicr_session_manager_callbacks
    {
        void* user_data;
        void (*new_server_session)(quicr_session_manager_t* manager, quicr_session_t* session, void* user_data);
        void (*session_removed)(quicr_session_manager_t* manager, quicr_session_t* session, void* user_data);
    } quicr_session_manager_callbacks_t;

    quicr_status_t quicr_session_manager_create(const quicr_session_manager_callbacks_t* callbacks,
                                                quicr_session_manager_t** manager);

    void quicr_session_manager_destroy(quicr_session_manager_t* manager);

    quicr_status_t quicr_session_manager_add_client(quicr_session_manager_t* manager,
                                                    const quicr_client_config_t* config,
                                                    const quicr_session_client_callbacks_t* callbacks,
                                                    quicr_session_t** session);

    quicr_status_t quicr_session_manager_add_server(quicr_session_manager_t* manager,
                                                    const quicr_server_config_t* config,
                                                    const quicr_session_server_callbacks_t* callbacks);

    quicr_status_t quicr_session_manager_add_handler(quicr_session_manager_t* manager,
                                                     quicr_session_t* session,
                                                     const quicr_track_handler_t* handler);

#ifdef __cplusplus
}
#endif

#endif
