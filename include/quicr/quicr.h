// SPDX-FileCopyrightText: Copyright (c) 2026 Cisco Systems
// SPDX-License-Identifier: BSD-2-Clause

#ifndef QUICR_H
#define QUICR_H

#include "quicr/quicr_c_export.h"

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
        QUICR_STATUS_NOT_FOUND = 4,
        QUICR_STATUS_BUFFER_TOO_SMALL = 5,
        QUICR_STATUS_BUSY = 6,
    };

    /*=======================================================================*/
    // Core Types
    /*=======================================================================*/

    typedef struct quicr_byte_span
    {
        const uint8_t* data;
        size_t size;
    } quicr_byte_span_t;

    typedef struct quicr_location
    {
        uint64_t group;
        uint64_t object;
    } quicr_location_t;

    typedef uint8_t quicr_group_order_t;
    enum
    {
        QUICR_GROUP_ORDER_UNSPECIFIED = 0,
        QUICR_GROUP_ORDER_ASCENDING = 0x1,
        QUICR_GROUP_ORDER_DESCENDING = 0x2,
    };

    typedef struct quicr_fetch_end_location
    {
        uint64_t group;
        bool has_object;
        uint64_t object;
    } quicr_fetch_end_location_t;

    typedef struct quicr_track_namespace quicr_track_namespace_t;
    typedef struct quicr_full_track_name quicr_full_track_name_t;
    typedef struct quicr_track_extensions quicr_track_extensions_t;

    QUICR_C_API quicr_status_t quicr_track_namespace_create(const quicr_byte_span_t* entries,
                                                            size_t count,
                                                            quicr_track_namespace_t** track_namespace);
    QUICR_C_API void quicr_track_namespace_destroy(quicr_track_namespace_t* track_namespace);
    QUICR_C_API size_t quicr_track_namespace_count(const quicr_track_namespace_t* track_namespace);
    QUICR_C_API quicr_status_t quicr_track_namespace_entry(const quicr_track_namespace_t* track_namespace,
                                                           size_t index,
                                                           quicr_byte_span_t* entry);

    QUICR_C_API quicr_status_t quicr_full_track_name_create(const quicr_track_namespace_t* track_namespace,
                                                            quicr_byte_span_t track_name,
                                                            quicr_full_track_name_t** full_track_name);
    QUICR_C_API void quicr_full_track_name_destroy(quicr_full_track_name_t* full_track_name);
    QUICR_C_API size_t quicr_full_track_name_namespace_count(const quicr_full_track_name_t* full_track_name);
    QUICR_C_API quicr_status_t quicr_full_track_name_namespace_entry(const quicr_full_track_name_t* full_track_name,
                                                                     size_t index,
                                                                     quicr_byte_span_t* entry);
    QUICR_C_API quicr_byte_span_t quicr_full_track_name_track_name(const quicr_full_track_name_t* full_track_name);

    typedef uint8_t quicr_request_error_code_t;
    enum
    {
        QUICR_REQUEST_ERROR_INTERNAL_ERROR = 1,
        QUICR_REQUEST_ERROR_UNAUTHORIZED = 2,
        QUICR_REQUEST_ERROR_TIMEOUT = 3,
        QUICR_REQUEST_ERROR_NOT_SUPPORTED = 4,
        QUICR_REQUEST_ERROR_MALFORMED_AUTH_TOKEN = 5,
        QUICR_REQUEST_ERROR_EXPIRED_AUTH_TOKEN = 6,
        QUICR_REQUEST_ERROR_DOES_NOT_EXIST = 7,
        QUICR_REQUEST_ERROR_INVALID_RANGE = 8,
        QUICR_REQUEST_ERROR_MALFORMED_TRACK = 9,
        QUICR_REQUEST_ERROR_DUPLICATE_SUBSCRIPTION = 10,
        QUICR_REQUEST_ERROR_UNINTERESTED = 11,
        QUICR_REQUEST_ERROR_PREFIX_OVERLAP = 12,
        QUICR_REQUEST_ERROR_INVALID_JOINING_REQUEST_ID = 13,
    };

    typedef uint8_t quicr_publish_error_code_t;
    enum
    {
        QUICR_PUBLISH_ERROR_INTERNAL_ERROR = 1,
        QUICR_PUBLISH_ERROR_NOT_SUPPORTED = 2,
        QUICR_PUBLISH_ERROR_NOT_AUTHORIZED = 3,
        QUICR_PUBLISH_ERROR_REJECTED = 4,
    };

    typedef uint8_t quicr_publish_namespace_error_code_t;
    enum
    {
        QUICR_PUBLISH_NAMESPACE_ERROR_OK = 0,
        QUICR_PUBLISH_NAMESPACE_ERROR_INTERNAL_ERROR = 1,
    };

    typedef uint8_t quicr_fetch_error_code_t;
    enum
    {
        QUICR_FETCH_ERROR_OK = 0,
        QUICR_FETCH_ERROR_INVALID_RANGE = 1,
        QUICR_FETCH_ERROR_NO_OBJECTS = 2,
        QUICR_FETCH_ERROR_INTERNAL_ERROR = 3,
    };

    typedef uint64_t quicr_error_code_t;
    enum
    {
        QUICR_ERROR_INTERNAL_ERROR = 0x0,
        QUICR_ERROR_UNAUTHORIZED = 0x1,
        QUICR_ERROR_TIMEOUT = 0x2,
        QUICR_ERROR_NOT_SUPPORTED = 0x3,
        QUICR_ERROR_MALFORMED_AUTH_TOKEN = 0x4,
        QUICR_ERROR_EXPIRED_AUTH_TOKEN = 0x5,
        QUICR_ERROR_DOES_NOT_EXIST = 0x10,
        QUICR_ERROR_INVALID_RANGE = 0x11,
        QUICR_ERROR_MALFORMED_TRACK = 0x12,
        QUICR_ERROR_DUPLICATE_SUBSCRIPTION = 0x19,
        QUICR_ERROR_UNINTERESTED = 0x20,
        QUICR_ERROR_PREFIX_OVERLAP = 0x30,
        QUICR_ERROR_INVALID_JOINING_REQUEST_ID = 0x32,
    };

    typedef uint8_t quicr_congestion_control_t;
    enum
    {
        QUICR_CONGESTION_CONTROL_BBR = 0,
        QUICR_CONGESTION_CONTROL_C4,
        QUICR_CONGESTION_CONTROL_CUBIC,
        QUICR_CONGESTION_CONTROL_NEW_RENO,
    };

    typedef struct quicr_transport_config
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

    QUICR_C_API void quicr_transport_config_init(quicr_transport_config_t* config);

    typedef struct quicr_client_config
    {
        const char* endpoint_id;
        quicr_transport_config_t transport_config;
        uint32_t unknown_stream_expiry_ms;

        const char* connect_uri;
        uint64_t tick_service_sleep_delay_us;
    } quicr_client_config_t;

    QUICR_C_API void quicr_client_config_init(quicr_client_config_t* config);

    typedef struct quicr_server_config
    {
        const char* endpoint_id;
        quicr_transport_config_t transport_config;
        uint32_t unknown_stream_expiry_ms;

        const char* bind_ip;
        uint16_t port;
        uint64_t tick_service_sleep_delay_us;
    } quicr_server_config_t;

    QUICR_C_API void quicr_server_config_init(quicr_server_config_t* config);

    /*=======================================================================*/
    // Connection
    /*=======================================================================*/

    typedef uint8_t quicr_stream_closed_flag_t;
    enum
    {
        QUICR_STREAM_CLOSED_FLAG_FIN = 0,
        QUICR_STREAM_CLOSED_FLAG_RESET,
        QUICR_STREAM_CLOSED_FLAG_STOP_SENDING,
    };

    /*=======================================================================*/
    // Session
    /*=======================================================================*/

    typedef struct quicr_session quicr_session_t;
    typedef struct quicr_publish_response quicr_publish_response_t;

    typedef struct quicr_track_status_response
    {
        bool has_largest_location;
        quicr_location_t largest_location;
        const quicr_track_extensions_t* track_extensions;
    } quicr_track_status_response_t;

    typedef struct quicr_subscribe_response
    {
        quicr_track_status_response_t track_status;
        bool is_publisher_initiated;
        bool has_expires;
        uint64_t expires_ms;
    } quicr_subscribe_response_t;

    typedef struct quicr_fetch_response
    {
        bool has_largest_location;
        quicr_location_t largest_location;
        quicr_group_order_t publisher_default_group_order;
    } quicr_fetch_response_t;

    typedef struct quicr_setup_attributes quicr_setup_attributes_t;
    typedef struct quicr_publish_attributes quicr_publish_attributes_t;
    typedef struct quicr_publish_namespace_attributes quicr_publish_namespace_attributes_t;
    typedef struct quicr_standalone_fetch_attributes quicr_standalone_fetch_attributes_t;
    typedef struct quicr_joining_fetch_attributes quicr_joining_fetch_attributes_t;
    typedef struct quicr_subscribe_attributes quicr_subscribe_attributes_t;
    typedef struct quicr_subscribe_namespace_attributes quicr_subscribe_namespace_attributes_t;
    typedef struct quicr_subscribe_namespace_handler quicr_subscribe_namespace_handler_t;

    typedef struct quicr_track_namespace_list
    {
        const quicr_track_namespace_t* const* entries;
        size_t count;
    } quicr_track_namespace_list_t;

    typedef struct quicr_request_error
    {
        quicr_request_error_code_t code;
        const char* reason;
    } quicr_request_error_t;

    typedef struct quicr_publish_error
    {
        quicr_publish_error_code_t code;
        const char* reason;
    } quicr_publish_error_t;

    typedef struct quicr_publish_namespace_error
    {
        quicr_publish_namespace_error_code_t code;
        const char* reason;
    } quicr_publish_namespace_error_t;

    typedef struct quicr_fetch_error
    {
        quicr_fetch_error_code_t code;
        const char* reason;
    } quicr_fetch_error_t;

    typedef struct quicr_error
    {
        quicr_error_code_t code;
        const char* reason;
    } quicr_error_t;

    typedef uint8_t quicr_reply_result_t;
    enum
    {
        QUICR_REPLY_RESULT_ACCEPT = 0,
        QUICR_REPLY_RESULT_REJECT,
    };

    typedef struct quicr_publish_reply
    {
        quicr_reply_result_t result;
        union
        {
            const quicr_publish_response_t* response;
            quicr_publish_error_t error;
        };
    } quicr_publish_reply_t;

    typedef struct quicr_subscribe_reply
    {
        quicr_reply_result_t result;
        union
        {
            quicr_subscribe_response_t response;
            quicr_request_error_t error;
        };
    } quicr_subscribe_reply_t;

    typedef struct quicr_publish_namespace_reply
    {
        quicr_reply_result_t result;
        quicr_publish_namespace_error_t error;
    } quicr_publish_namespace_reply_t;

    typedef struct quicr_fetch_reply
    {
        quicr_reply_result_t result;
        union
        {
            quicr_fetch_response_t response;
            quicr_fetch_error_t error;
        };
    } quicr_fetch_reply_t;

    typedef struct quicr_fetch_cancel_reply
    {
        quicr_reply_result_t result;
        quicr_fetch_error_t error;
    } quicr_fetch_cancel_reply_t;

    typedef struct quicr_track_status_reply
    {
        quicr_reply_result_t result;
        union
        {
            quicr_track_status_response_t response;
            quicr_request_error_t error;
        };
    } quicr_track_status_reply_t;

    typedef struct quicr_track_namespaces_reply
    {
        quicr_reply_result_t result;
        union
        {
            const quicr_track_namespace_list_t* response;
            quicr_request_error_t error;
        };
    } quicr_track_namespaces_reply_t;

    typedef struct quicr_request_reply
    {
        quicr_reply_result_t result;
        quicr_request_error_t error;
    } quicr_request_reply_t;

    typedef struct quicr_error_reply
    {
        quicr_reply_result_t result;
        quicr_error_t error;
    } quicr_error_reply_t;

    typedef uint8_t quicr_session_status_t;
    enum
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
    };

    typedef struct quicr_session_client_callbacks
    {
        void* user_data;
        quicr_error_reply_t (*setup_received)(quicr_session_t* session,
                                              const quicr_setup_attributes_t* attributes,
                                              void* user_data);
        void (*on_stream_closed)(uint64_t stream_id, quicr_stream_closed_flag_t flag, void* user_data);
        void (*status_changed)(quicr_session_t* session, quicr_session_status_t status, void* user_data);
        /* handler is borrowed and valid only for the duration of the callback. */
        quicr_publish_reply_t (*publish_received)(quicr_session_t* session,
                                                  uint64_t request_id,
                                                  const quicr_publish_attributes_t* attributes,
                                                  const quicr_subscribe_namespace_handler_t* handler,
                                                  void* user_data);
        quicr_publish_namespace_reply_t (*publish_namespace_received)(
          quicr_session_t* session,
          const quicr_track_namespace_t* track_namespace,
          const quicr_publish_namespace_attributes_t* attributes,
          void* user_data);
        quicr_fetch_reply_t (*standalone_fetch_received)(quicr_session_t* session,
                                                         uint64_t request_id,
                                                         const quicr_full_track_name_t* full_track_name,
                                                         const quicr_standalone_fetch_attributes_t* attributes,
                                                         void* user_data);
        quicr_fetch_reply_t (*joining_fetch_received)(quicr_session_t* session,
                                                      uint64_t request_id,
                                                      const quicr_full_track_name_t* full_track_name,
                                                      const quicr_joining_fetch_attributes_t* attributes,
                                                      void* user_data);
        quicr_fetch_cancel_reply_t (*fetch_cancel_received)(quicr_session_t* session,
                                                            uint64_t request_id,
                                                            void* user_data);
        quicr_track_status_reply_t (*track_status_received)(quicr_session_t* session,
                                                            const quicr_full_track_name_t* full_track_name,
                                                            void* user_data);
        quicr_error_reply_t (*unpublished_subscribe_received)(quicr_session_t* session,
                                                              const quicr_full_track_name_t* full_track_name,
                                                              const quicr_subscribe_attributes_t* attributes,
                                                              void* user_data);
    } quicr_session_client_callbacks_t;

    typedef struct quicr_session_server_callbacks
    {
        void* user_data;
        quicr_error_reply_t (*setup_received)(quicr_session_t* session,
                                              const quicr_setup_attributes_t* attributes,
                                              void* user_data);
        void (*on_stream_closed)(uint64_t stream_id, quicr_stream_closed_flag_t flag, void* user_data);
        void (*status_changed)(quicr_session_t* session, quicr_session_status_t status, void* user_data);
        /* handler is borrowed and valid only for the duration of the callback. */
        quicr_publish_reply_t (*publish_received)(quicr_session_t* session,
                                                  uint64_t request_id,
                                                  const quicr_publish_attributes_t* attributes,
                                                  const quicr_subscribe_namespace_handler_t* handler,
                                                  void* user_data);
        quicr_publish_namespace_reply_t (*publish_namespace_received)(
          quicr_session_t* session,
          const quicr_track_namespace_t* track_namespace,
          const quicr_publish_namespace_attributes_t* attributes,
          void* user_data);
        quicr_fetch_reply_t (*standalone_fetch_received)(quicr_session_t* session,
                                                         uint64_t request_id,
                                                         const quicr_full_track_name_t* full_track_name,
                                                         const quicr_standalone_fetch_attributes_t* attributes,
                                                         void* user_data);
        quicr_fetch_reply_t (*joining_fetch_received)(quicr_session_t* session,
                                                      uint64_t request_id,
                                                      const quicr_full_track_name_t* full_track_name,
                                                      const quicr_joining_fetch_attributes_t* attributes,
                                                      void* user_data);
        quicr_fetch_cancel_reply_t (*fetch_cancel_received)(quicr_session_t* session,
                                                            uint64_t request_id,
                                                            void* user_data);
        quicr_track_status_reply_t (*track_status_received)(quicr_session_t* session,
                                                            const quicr_full_track_name_t* full_track_name,
                                                            void* user_data);
        quicr_track_namespaces_reply_t (*subscribe_namespace_received)(
          quicr_session_t* session,
          const quicr_track_namespace_t* prefix_namespace,
          const quicr_subscribe_namespace_attributes_t* attributes,
          void* user_data);
        quicr_track_namespaces_reply_t (*subscribe_tracks_received)(
          quicr_session_t* session,
          const quicr_track_namespace_t* prefix_namespace,
          const quicr_subscribe_namespace_attributes_t* attributes,
          void* user_data);
        quicr_subscribe_reply_t (*subscribe_received)(quicr_session_t* session,
                                                      uint64_t request_id,
                                                      const quicr_full_track_name_t* full_track_name,
                                                      const quicr_subscribe_attributes_t* attributes,
                                                      void* user_data);
        quicr_error_reply_t (*new_group_requested)(const quicr_full_track_name_t* full_track_name,
                                                   uint64_t group_id,
                                                   void* user_data);
    } quicr_session_server_callbacks_t;

    QUICR_C_API quicr_status_t quicr_session_get_status(const quicr_session_t* session, quicr_session_status_t* status);

    /*=======================================================================*/
    // Handlers
    /*=======================================================================*/

    typedef uint8_t quicr_track_mode_t;
    enum
    {
        QUICR_TRACK_MODE_DATAGRAM = 0,
        QUICR_TRACK_MODE_STREAM,
    };

    typedef uint8_t quicr_object_status_t;
    enum
    {
        QUICR_OBJECT_STATUS_AVAILABLE = 0x0,
        QUICR_OBJECT_STATUS_DOES_NOT_EXIST = 0x1,
        QUICR_OBJECT_STATUS_END_OF_GROUP = 0x3,
        QUICR_OBJECT_STATUS_END_OF_TRACK = 0x4,
        QUICR_OBJECT_STATUS_END_OF_SUBGROUP = 0x5
    };

    typedef struct quicr_extension_entry
    {
        uint64_t type;
        const quicr_byte_span_t* values;
        size_t count;
    } quicr_extension_entry_t;

    typedef struct quicr_extensions
    {
        const quicr_extension_entry_t* entries;
        size_t count;
    } quicr_extensions_t;

    typedef struct quicr_object_headers
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

    QUICR_C_API void quicr_object_headers_init(quicr_object_headers_t* headers);

    typedef uint8_t quicr_subgroup_id_type_t;
    enum
    {
        QUICR_SUBGROUP_ID_TYPE_IS_ZERO = 1 << 0u,
        QUICR_SUBGROUP_ID_TYPE_SET_FROM_FIRST_OBJECT = 1 << 1u,
        QUICR_SUBGROUP_ID_TYPE_EXPLICIT = 1 << 2u,
        QUICR_SUBGROUP_ID_TYPE_RESERVED = 1 << 3u,
    };

    typedef struct quicr_stream_header_properties
    {
        bool extensions;
        quicr_subgroup_id_type_t subgroup_id_mode;
        bool end_of_group;
        bool default_priority;
        bool first_object;
    } quicr_stream_header_properties_t;

    QUICR_C_API void quicr_stream_header_properties_init(quicr_stream_header_properties_t* properties);

    QUICR_C_API void quicr_location_init(quicr_location_t* location);

    typedef struct quicr_min_max_avg
    {
        uint64_t min;
        uint64_t max;
        uint64_t avg;
        uint64_t value_sum;
        uint64_t value_count;
    } quicr_min_max_avg_t;

    typedef struct quicr_subscribe_track_metrics
    {
        uint64_t last_sample_time;
        uint64_t bytes_received;
        uint64_t objects_received;
    } quicr_subscribe_track_metrics_t;

    typedef struct quicr_publish_track_quic_metrics
    {
        uint64_t tx_buffer_drops;
        uint64_t tx_queue_discards;
        uint64_t tx_queue_expired;
        uint64_t tx_delayed_callback;
        quicr_min_max_avg_t tx_queue_size;
        quicr_min_max_avg_t tx_callback_ms;
        quicr_min_max_avg_t tx_object_duration_us;
    } quicr_publish_track_quic_metrics_t;

    typedef struct quicr_publish_track_metrics
    {
        uint64_t last_sample_time;
        uint64_t bytes_published;
        uint64_t objects_published;
        uint64_t objects_dropped_not_ok;
        quicr_publish_track_quic_metrics_t quic;
    } quicr_publish_track_metrics_t;

    typedef struct quicr_publish_track_handler quicr_publish_track_handler_t;

    typedef uint8_t quicr_publish_track_handler_status_t;
    enum
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
    };

    typedef struct quicr_publish_track_handler_callbacks
    {
        void* user_data;
        void (*status_changed)(quicr_publish_track_handler_t* handler,
                               quicr_publish_track_handler_status_t status,
                               void* user_data);
        void (*metrics_sampled)(quicr_publish_track_handler_t* handler,
                                const quicr_publish_track_metrics_t* metrics,
                                void* user_data);
    } quicr_publish_track_handler_callbacks_t;

    QUICR_C_API quicr_status_t
    quicr_publish_track_handler_create(const quicr_publish_track_handler_callbacks_t* callbacks,
                                       const quicr_full_track_name_t* ftn,
                                       quicr_track_mode_t track_mode,
                                       uint8_t publisher_priority,
                                       uint8_t publish_object_priority,
                                       uint32_t default_ttl,
                                       const quicr_location_t* largest_location,
                                       quicr_publish_track_handler_t** handler);

    QUICR_C_API quicr_status_t quicr_publish_track_handler_destroy(quicr_publish_track_handler_t* handler);

    QUICR_C_API bool quicr_publish_track_handler_can_publish(const quicr_publish_track_handler_t* handler);

    QUICR_C_API quicr_status_t quicr_publish_track_handler_get_status(const quicr_publish_track_handler_t* handler,
                                                                      quicr_publish_track_handler_status_t* status);

    typedef uint8_t quicr_publish_object_status_t;
    enum
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
        QUICR_PUBLISH_OBJECT_STATUS_SUBGROUP_CLOSED,
        QUICR_PUBLISH_OBJECT_STATUS_FETCH_CLOSED,
    };

    QUICR_C_API quicr_status_t
    quicr_publish_track_handler_publish_object(quicr_publish_track_handler_t* handler,
                                               const quicr_object_headers_t* headers,
                                               quicr_byte_span_t data,
                                               const quicr_stream_header_properties_t* properties,
                                               quicr_publish_object_status_t* status);

    QUICR_C_API quicr_status_t quicr_publish_track_handler_end_subgroup(quicr_publish_track_handler_t* handler,
                                                                        uint64_t group_id,
                                                                        uint64_t subgroup_id,
                                                                        bool completed);

    QUICR_C_API quicr_status_t quicr_publish_track_handler_end_all_subgroups(quicr_publish_track_handler_t* handler,
                                                                             bool completed);

    typedef struct quicr_subscribe_track_handler quicr_subscribe_track_handler_t;

    typedef uint8_t quicr_subscribe_track_handler_status_t;
    enum
    {
        QUICR_SUBSCRIBE_TRACK_HANDLER_STATUS_OK = 0,
        QUICR_SUBSCRIBE_TRACK_HANDLER_STATUS_NOT_CONNECTED,
        QUICR_SUBSCRIBE_TRACK_HANDLER_STATUS_ERROR,
        QUICR_SUBSCRIBE_TRACK_HANDLER_STATUS_NOT_AUTHORIZED,
        QUICR_SUBSCRIBE_TRACK_HANDLER_STATUS_NOT_SUBSCRIBED,
        QUICR_SUBSCRIBE_TRACK_HANDLER_STATUS_PENDING_RESPONSE,
        QUICR_SUBSCRIBE_TRACK_HANDLER_STATUS_SENDING_UNSUBSCRIBE,
        QUICR_SUBSCRIBE_TRACK_HANDLER_STATUS_PAUSED,
        QUICR_SUBSCRIBE_TRACK_HANDLER_STATUS_NEW_GROUP_REQUESTED,
        QUICR_SUBSCRIBE_TRACK_HANDLER_STATUS_CANCELLED,
        QUICR_SUBSCRIBE_TRACK_HANDLER_STATUS_DONE_BY_FIN,
        QUICR_SUBSCRIBE_TRACK_HANDLER_STATUS_DONE_BY_RESET,
    };

    typedef struct quicr_subscribe_track_handler_callbacks
    {
        void* user_data;
        void (*status_changed)(quicr_subscribe_track_handler_t* handler,
                               quicr_subscribe_track_handler_status_t status,
                               void* user_data);
        void (*metrics_sampled)(quicr_subscribe_track_handler_t* handler,
                                const quicr_subscribe_track_metrics_t* metrics,
                                void* user_data);
        void (*object_received)(const quicr_object_headers_t* headers,
                                quicr_byte_span_t data,
                                const quicr_stream_header_properties_t* properties,
                                void* user_data);
        void (*object_status_received)(const quicr_object_headers_t* headers, void* user_data);
    } quicr_subscribe_track_handler_callbacks_t;

    typedef uint64_t quicr_filter_type_t;
    enum
    {
        QUICR_FILTER_TYPE_NONE = 0,
        QUICR_FILTER_TYPE_SUBSCRIPTION_FILTER,
        QUICR_FILTER_TYPE_TRACK_FILTER,
    };

    typedef int32_t quicr_subscription_filter_type_t;
    enum
    {
        QUICR_SUBSCRIPTION_FILTER_TYPE_NEXT_GROUP_START = 0x1,
        QUICR_SUBSCRIPTION_FILTER_TYPE_LARGEST_OBJECT = 0x2,
        QUICR_SUBSCRIPTION_FILTER_TYPE_ABSOLUTE_START = 0x3,
        QUICR_SUBSCRIPTION_FILTER_TYPE_ABSOLUTE_RANGE = 0x4,
    };

    typedef struct quicr_subscription_filter
    {
        quicr_subscription_filter_type_t type;
        quicr_location_t start_location;
        uint64_t end_group_delta;
    } quicr_subscription_filter_t;

    QUICR_C_API void quicr_subscription_filter_init(quicr_subscription_filter_t* filter);

    typedef struct quicr_track_filter
    {
        uint64_t property_type;
        uint64_t max_tracks_selected;
        uint64_t timeout;
    } quicr_track_filter_t;

    QUICR_C_API void quicr_track_filter_init(quicr_track_filter_t* filter);

    typedef struct quicr_filter
    {
        quicr_filter_type_t type;

        union
        {
            quicr_subscription_filter_t subscription;
            quicr_track_filter_t track;
        };
    } quicr_filter_t;

    QUICR_C_API quicr_status_t
    quicr_subscribe_track_handler_create(const quicr_subscribe_track_handler_callbacks_t* callbacks,
                                         const quicr_full_track_name_t* ftn,
                                         uint8_t priority,
                                         quicr_group_order_t group_order,
                                         const quicr_filter_t* filter,
                                         quicr_subscribe_track_handler_t** handler);

    QUICR_C_API quicr_status_t quicr_subscribe_track_handler_destroy(quicr_subscribe_track_handler_t* handler);

    QUICR_C_API quicr_status_t quicr_subscribe_track_handler_get_status(const quicr_subscribe_track_handler_t* handler,
                                                                        quicr_subscribe_track_handler_status_t* status);

    QUICR_C_API quicr_status_t quicr_subscribe_track_handler_pause(quicr_subscribe_track_handler_t* handler);

    QUICR_C_API quicr_status_t quicr_subscribe_track_handler_resume(quicr_subscribe_track_handler_t* handler);

    QUICR_C_API quicr_status_t quicr_subscribe_track_handler_request_new_group(quicr_subscribe_track_handler_t* handler,
                                                                               uint64_t group_id);

    QUICR_C_API quicr_status_t
    quicr_subscribe_track_handler_support_new_group_request(quicr_subscribe_track_handler_t* handler, bool supported);

    typedef struct quicr_fetch_track_handler quicr_fetch_track_handler_t;
    typedef quicr_subscribe_track_handler_status_t quicr_fetch_track_handler_status_t;

    typedef struct quicr_fetch_track_handler_callbacks
    {
        void* user_data;
        void (*status_changed)(quicr_fetch_track_handler_t* handler,
                               quicr_fetch_track_handler_status_t status,
                               void* user_data);
        void (*metrics_sampled)(quicr_fetch_track_handler_t* handler,
                                const quicr_subscribe_track_metrics_t* metrics,
                                void* user_data);
        void (*object_received)(const quicr_object_headers_t* headers,
                                quicr_byte_span_t data,
                                const quicr_stream_header_properties_t* properties,
                                void* user_data);
        void (*object_status_received)(const quicr_object_headers_t* headers, void* user_data);
    } quicr_fetch_track_handler_callbacks_t;

    QUICR_C_API quicr_status_t quicr_fetch_track_handler_create(const quicr_fetch_track_handler_callbacks_t* callbacks,
                                                                const quicr_full_track_name_t* ftn,
                                                                uint8_t priority,
                                                                const quicr_location_t* start_location,
                                                                const quicr_fetch_end_location_t* end_location,
                                                                quicr_group_order_t group_order,
                                                                quicr_fetch_track_handler_t** handler);
    QUICR_C_API quicr_status_t quicr_fetch_track_handler_destroy(quicr_fetch_track_handler_t* handler);
    QUICR_C_API quicr_status_t quicr_fetch_track_handler_get_status(const quicr_fetch_track_handler_t* handler,
                                                                    quicr_fetch_track_handler_status_t* status);

    QUICR_C_API quicr_status_t quicr_fetch_track_handler_pause(quicr_fetch_track_handler_t* handler);

    QUICR_C_API quicr_status_t quicr_fetch_track_handler_resume(quicr_fetch_track_handler_t* handler);

    QUICR_C_API quicr_status_t quicr_fetch_track_handler_request_new_group(quicr_fetch_track_handler_t* handler,
                                                                           uint64_t group_id);

    QUICR_C_API quicr_status_t quicr_fetch_track_handler_support_new_group_request(quicr_fetch_track_handler_t* handler,
                                                                                   bool supported);

    typedef struct quicr_publish_namespace_handler quicr_publish_namespace_handler_t;

    typedef uint8_t quicr_publish_namespace_handler_status_t;
    enum
    {
        QUICR_PUBLISH_NAMESPACE_HANDLER_STATUS_OK = 0,
        QUICR_PUBLISH_NAMESPACE_HANDLER_STATUS_NOT_CONNECTED,
        QUICR_PUBLISH_NAMESPACE_HANDLER_STATUS_NOT_PUBLISHED,
        QUICR_PUBLISH_NAMESPACE_HANDLER_STATUS_PENDING_RESPONSE,
        QUICR_PUBLISH_NAMESPACE_HANDLER_STATUS_NOT_AUTHORIZED,
        QUICR_PUBLISH_NAMESPACE_HANDLER_STATUS_SENDING_DONE,
        QUICR_PUBLISH_NAMESPACE_HANDLER_STATUS_ERROR,
    };

    typedef struct quicr_publish_namespace_handler_callbacks
    {
        void* user_data;
        void (*status_changed)(quicr_publish_namespace_handler_t* handler,
                               quicr_publish_namespace_handler_status_t status,
                               void* user_data);
    } quicr_publish_namespace_handler_callbacks_t;

    QUICR_C_API quicr_status_t
    quicr_publish_namespace_handler_create(const quicr_publish_namespace_handler_callbacks_t* callbacks,
                                           const quicr_track_namespace_t* prefix,
                                           quicr_publish_namespace_handler_t** handler);
    QUICR_C_API quicr_status_t quicr_publish_namespace_handler_destroy(quicr_publish_namespace_handler_t* handler);
    QUICR_C_API quicr_status_t
    quicr_publish_namespace_handler_get_status(const quicr_publish_namespace_handler_t* handler,
                                               quicr_publish_namespace_handler_status_t* status);

    /* reason_size is input capacity and output required size, including the null terminator. */
    QUICR_C_API quicr_status_t
    quicr_publish_namespace_handler_get_error(const quicr_publish_namespace_handler_t* handler,
                                              quicr_error_code_t* code,
                                              char* reason,
                                              size_t* reason_size);

    QUICR_C_API quicr_status_t
    quicr_publish_namespace_handler_publish_track(quicr_publish_namespace_handler_t* handler,
                                                  const quicr_publish_track_handler_t* track_handler);

    QUICR_C_API quicr_status_t
    quicr_publish_namespace_handler_unpublish_track(quicr_publish_namespace_handler_t* handler,
                                                    const quicr_publish_track_handler_t* track_handler);

    typedef struct quicr_subscribe_namespace_handler quicr_subscribe_namespace_handler_t;

    typedef int32_t quicr_subscribe_namespace_mode_t;
    enum
    {
        QUICR_SUBSCRIBE_NAMESPACE_MODE_NAMESPACES = 0,
        QUICR_SUBSCRIBE_NAMESPACE_MODE_TRACKS = 1,
    };

    typedef uint8_t quicr_subscribe_namespace_handler_status_t;
    enum
    {
        QUICR_SUBSCRIBE_NAMESPACE_HANDLER_STATUS_OK = 0,
        QUICR_SUBSCRIBE_NAMESPACE_HANDLER_STATUS_NOT_SUBSCRIBED,
        QUICR_SUBSCRIBE_NAMESPACE_HANDLER_STATUS_ERROR,
    };

    typedef struct quicr_subscribe_namespace_handler_callbacks
    {
        void* user_data;
        void (*status_changed)(quicr_subscribe_namespace_handler_t* handler,
                               quicr_subscribe_namespace_handler_status_t status,
                               void* user_data);
        void (*namespace_received)(quicr_subscribe_namespace_handler_t* handler,
                                   const quicr_track_namespace_t* track_namespace,
                                   void* user_data);
        void (*namespace_done_received)(quicr_subscribe_namespace_handler_t* handler,
                                        const quicr_track_namespace_t* track_namespace,
                                        void* user_data);
    } quicr_subscribe_namespace_handler_callbacks_t;

    QUICR_C_API quicr_status_t
    quicr_subscribe_namespace_handler_create(const quicr_subscribe_namespace_handler_callbacks_t* callbacks,
                                             const quicr_track_namespace_t* prefix,
                                             quicr_subscribe_namespace_mode_t mode,
                                             const quicr_filter_t* filter,
                                             quicr_subscribe_namespace_handler_t** handler);
    QUICR_C_API quicr_status_t quicr_subscribe_namespace_handler_destroy(quicr_subscribe_namespace_handler_t* handler);
    QUICR_C_API quicr_status_t
    quicr_subscribe_namespace_handler_get_status(const quicr_subscribe_namespace_handler_t* handler,
                                                 quicr_subscribe_namespace_handler_status_t* status);

    QUICR_C_API quicr_status_t
    quicr_subscribe_namespace_handler_get_error(const quicr_subscribe_namespace_handler_t* handler,
                                                quicr_error_code_t* code,
                                                char* reason,
                                                size_t* reason_size);

    typedef struct quicr_track_status_track_handler quicr_track_status_track_handler_t;

    typedef uint8_t quicr_track_status_handler_status_t;
    enum
    {
        QUICR_TRACK_STATUS_HANDLER_STATUS_NOT_REQUESTED = 0,
        QUICR_TRACK_STATUS_HANDLER_STATUS_PENDING_RESPONSE,
        QUICR_TRACK_STATUS_HANDLER_STATUS_OK,
        QUICR_TRACK_STATUS_HANDLER_STATUS_ERROR,
        QUICR_TRACK_STATUS_HANDLER_STATUS_DONE_BY_FIN,
        QUICR_TRACK_STATUS_HANDLER_STATUS_DONE_BY_RESET,
        QUICR_TRACK_STATUS_HANDLER_STATUS_NOT_CONNECTED,
    };

    typedef struct quicr_track_status_handler_callbacks
    {
        void* user_data;
        void (*status_changed)(quicr_track_status_track_handler_t* handler,
                               quicr_track_status_handler_status_t status,
                               void* user_data);
    } quicr_track_status_handler_callbacks_t;

    QUICR_C_API quicr_status_t
    quicr_track_status_handler_create(const quicr_track_status_handler_callbacks_t* callbacks,
                                      const quicr_full_track_name_t* ftn,
                                      quicr_track_status_track_handler_t** handler);
    QUICR_C_API quicr_status_t quicr_track_status_handler_destroy(quicr_track_status_track_handler_t* handler);
    QUICR_C_API quicr_status_t quicr_track_status_handler_get_status(const quicr_track_status_track_handler_t* handler,
                                                                     quicr_track_status_handler_status_t* status);

    /* track_extensions may be null when extension data is not needed. */
    QUICR_C_API quicr_status_t
    quicr_track_status_handler_get_response(const quicr_track_status_track_handler_t* handler,
                                            bool* has_response,
                                            quicr_track_status_response_t* response,
                                            quicr_track_extensions_t* track_extensions);

    QUICR_C_API quicr_status_t quicr_track_status_handler_get_error(const quicr_track_status_track_handler_t* handler,
                                                                    quicr_error_code_t* code,
                                                                    char* reason,
                                                                    size_t* reason_size);

    /*=======================================================================*/
    // Attributes
    /*=======================================================================*/

    struct quicr_setup_attributes
    {
        quicr_byte_span_t endpoint_id;
    };

    struct quicr_publish_namespace_attributes
    {
        uint64_t request_id;
    };

    struct quicr_standalone_fetch_attributes
    {
        uint8_t priority;
        quicr_group_order_t group_order;
        quicr_group_order_t publisher_default_group_order;
        quicr_location_t start_location;
        quicr_fetch_end_location_t end_location;
    };

    struct quicr_joining_fetch_attributes
    {
        uint8_t priority;
        quicr_group_order_t group_order;
        quicr_group_order_t publisher_default_group_order;
        uint64_t joining_request_id;
        bool relative;
        uint64_t joining_start;
    };

    typedef uint64_t quicr_token_alias_type_t;
    enum
    {
        QUICR_TOKEN_ALIAS_TYPE_DELETE = 0,
        QUICR_TOKEN_ALIAS_TYPE_REGISTER = 1,
        QUICR_TOKEN_ALIAS_TYPE_USE_ALIAS = 2,
        QUICR_TOKEN_ALIAS_TYPE_USE_VALUE = 3,
    };

    typedef struct quicr_token
    {
        quicr_token_alias_type_t alias_type;
        bool has_token_alias;
        uint64_t token_alias;
        bool has_token_type;
        uint64_t token_type;
        quicr_byte_span_t token_value;
    } quicr_token_t;

    QUICR_C_API quicr_status_t quicr_track_extensions_create(quicr_track_extensions_t** extensions);

    QUICR_C_API void quicr_track_extensions_destroy(quicr_track_extensions_t* extensions);

    QUICR_C_API quicr_status_t quicr_track_extensions_add(quicr_track_extensions_t* extensions,
                                                          uint64_t type,
                                                          quicr_byte_span_t value);

    QUICR_C_API quicr_status_t quicr_track_extensions_add_immutable(quicr_track_extensions_t* extensions,
                                                                    uint64_t type,
                                                                    quicr_byte_span_t value);

    QUICR_C_API size_t quicr_track_extensions_count(const quicr_track_extensions_t* extensions);

    QUICR_C_API size_t quicr_track_extensions_immutable_count(const quicr_track_extensions_t* extensions);

    QUICR_C_API quicr_status_t quicr_track_extensions_entry(const quicr_track_extensions_t* extensions,
                                                            size_t index,
                                                            uint64_t* type,
                                                            quicr_byte_span_t* value);

    QUICR_C_API quicr_status_t quicr_track_extensions_immutable_entry(const quicr_track_extensions_t* extensions,
                                                                      size_t index,
                                                                      uint64_t* type,
                                                                      quicr_byte_span_t* value);

    QUICR_C_API const quicr_full_track_name_t* quicr_publish_attributes_full_track_name(
      const quicr_publish_attributes_t* attributes);

    QUICR_C_API uint64_t quicr_publish_attributes_track_alias(const quicr_publish_attributes_t* attributes);

    QUICR_C_API size_t quicr_publish_attributes_auth_token_count(const quicr_publish_attributes_t* attributes);

    QUICR_C_API quicr_status_t quicr_publish_attributes_auth_token(const quicr_publish_attributes_t* attributes,
                                                                   size_t index,
                                                                   quicr_token_t* token);

    QUICR_C_API quicr_status_t quicr_publish_attributes_expires(const quicr_publish_attributes_t* attributes,
                                                                bool* has_expires,
                                                                uint64_t* expires);

    QUICR_C_API quicr_status_t quicr_publish_attributes_largest_object(const quicr_publish_attributes_t* attributes,
                                                                       bool* has_largest_object,
                                                                       quicr_location_t* largest_object);

    QUICR_C_API bool quicr_publish_attributes_forward(const quicr_publish_attributes_t* attributes);

    QUICR_C_API quicr_status_t quicr_publish_attributes_group_order(const quicr_publish_attributes_t* attributes,
                                                                    quicr_group_order_t* group_order);

    QUICR_C_API bool quicr_publish_attributes_dynamic_groups(const quicr_publish_attributes_t* attributes);

    QUICR_C_API uint8_t quicr_publish_attributes_publisher_priority(const quicr_publish_attributes_t* attributes);

    QUICR_C_API const quicr_track_extensions_t* quicr_publish_attributes_track_extensions(
      const quicr_publish_attributes_t* attributes);

    QUICR_C_API quicr_status_t quicr_publish_attributes_max_cache_duration(const quicr_publish_attributes_t* attributes,
                                                                           bool* has_max_cache_duration,
                                                                           uint64_t* max_cache_duration);

    QUICR_C_API quicr_status_t quicr_publish_attributes_delivery_timeout(const quicr_publish_attributes_t* attributes,
                                                                         bool* has_delivery_timeout,
                                                                         uint64_t* delivery_timeout);

    QUICR_C_API uint8_t quicr_subscribe_attributes_priority(const quicr_subscribe_attributes_t* attributes);

    QUICR_C_API quicr_status_t quicr_subscribe_attributes_group_order(const quicr_subscribe_attributes_t* attributes,
                                                                      bool* has_group_order,
                                                                      quicr_group_order_t* group_order);

    QUICR_C_API quicr_status_t
    quicr_subscribe_attributes_publisher_group_order(const quicr_subscribe_attributes_t* attributes,
                                                     quicr_group_order_t* group_order);

    QUICR_C_API uint64_t quicr_subscribe_attributes_delivery_timeout(const quicr_subscribe_attributes_t* attributes);

    QUICR_C_API uint64_t quicr_subscribe_attributes_expires(const quicr_subscribe_attributes_t* attributes);

    QUICR_C_API bool quicr_subscribe_attributes_forward(const quicr_subscribe_attributes_t* attributes);

    QUICR_C_API quicr_status_t
    quicr_subscribe_attributes_new_group_request_id(const quicr_subscribe_attributes_t* attributes,
                                                    bool* has_new_group_request_id,
                                                    uint64_t* new_group_request_id);

    QUICR_C_API bool quicr_subscribe_attributes_is_publisher_initiated(const quicr_subscribe_attributes_t* attributes);

    QUICR_C_API quicr_location_t
    quicr_subscribe_attributes_start_location(const quicr_subscribe_attributes_t* attributes);

    QUICR_C_API uint64_t quicr_subscribe_attributes_rendezvous_timeout(const quicr_subscribe_attributes_t* attributes);

    QUICR_C_API uint64_t
    quicr_subscribe_namespace_attributes_request_id(const quicr_subscribe_namespace_attributes_t* attributes);

    /*=======================================================================*/
    // Responses
    /*=======================================================================*/

    QUICR_C_API quicr_status_t quicr_publish_response_create(quicr_subscribe_track_handler_t* handler,
                                                             quicr_publish_response_t** response);

    QUICR_C_API void quicr_publish_response_destroy(quicr_publish_response_t* response);

    QUICR_C_API quicr_status_t quicr_publish_response_set_subscriber_priority(quicr_publish_response_t* response,
                                                                              uint8_t priority);

    QUICR_C_API quicr_status_t quicr_publish_response_set_group_order(quicr_publish_response_t* response,
                                                                      quicr_group_order_t group_order);

    QUICR_C_API quicr_status_t quicr_publish_response_set_filter(quicr_publish_response_t* response,
                                                                 const quicr_filter_t* filter);

    QUICR_C_API quicr_status_t quicr_publish_response_set_forward(quicr_publish_response_t* response, bool forward);

    QUICR_C_API quicr_status_t quicr_publish_response_set_subgroup_delivery_timeout(quicr_publish_response_t* response,
                                                                                    uint64_t timeout_ms);

    QUICR_C_API quicr_status_t quicr_publish_response_set_object_delivery_timeout(quicr_publish_response_t* response,
                                                                                  uint64_t timeout_ms);

    QUICR_C_API quicr_status_t quicr_publish_response_set_new_group_request_id(quicr_publish_response_t* response,
                                                                               uint64_t request_id);

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

    QUICR_C_API quicr_status_t quicr_session_manager_create(const quicr_session_manager_callbacks_t* callbacks,
                                                            quicr_session_manager_t** manager);

    QUICR_C_API void quicr_session_manager_destroy(quicr_session_manager_t* manager);

    QUICR_C_API quicr_status_t quicr_session_manager_add_client(quicr_session_manager_t* manager,
                                                                const quicr_client_config_t* config,
                                                                const quicr_session_client_callbacks_t* callbacks,
                                                                quicr_session_t** session);

    QUICR_C_API quicr_status_t quicr_session_manager_add_server(quicr_session_manager_t* manager,
                                                                const quicr_server_config_t* config,
                                                                const quicr_session_server_callbacks_t* callbacks);

    QUICR_C_API quicr_status_t
    quicr_session_manager_add_publish_track_handler(quicr_session_manager_t* manager,
                                                    quicr_session_t* session,
                                                    const quicr_publish_track_handler_t* handler);

    QUICR_C_API quicr_status_t
    quicr_session_manager_remove_publish_track_handler(quicr_session_manager_t* manager,
                                                       quicr_session_t* session,
                                                       const quicr_publish_track_handler_t* handler);

    QUICR_C_API quicr_status_t
    quicr_session_manager_add_subscribe_track_handler(quicr_session_manager_t* manager,
                                                      quicr_session_t* session,
                                                      const quicr_subscribe_track_handler_t* handler);

    QUICR_C_API quicr_status_t
    quicr_session_manager_remove_subscribe_track_handler(quicr_session_manager_t* manager,
                                                         quicr_session_t* session,
                                                         const quicr_subscribe_track_handler_t* handler);

    QUICR_C_API quicr_status_t
    quicr_session_manager_add_fetch_track_handler(quicr_session_manager_t* manager,
                                                  quicr_session_t* session,
                                                  const quicr_fetch_track_handler_t* handler);

    QUICR_C_API quicr_status_t
    quicr_session_manager_remove_fetch_track_handler(quicr_session_manager_t* manager,
                                                     quicr_session_t* session,
                                                     const quicr_fetch_track_handler_t* handler);

    QUICR_C_API quicr_status_t
    quicr_session_manager_add_publish_namespace_handler(quicr_session_manager_t* manager,
                                                        quicr_session_t* session,
                                                        const quicr_publish_namespace_handler_t* handler);

    QUICR_C_API quicr_status_t
    quicr_session_manager_remove_publish_namespace_handler(quicr_session_manager_t* manager,
                                                           quicr_session_t* session,
                                                           const quicr_publish_namespace_handler_t* handler);

    QUICR_C_API quicr_status_t
    quicr_session_manager_add_subscribe_namespace_handler(quicr_session_manager_t* manager,
                                                          quicr_session_t* session,
                                                          const quicr_subscribe_namespace_handler_t* handler);

    QUICR_C_API quicr_status_t
    quicr_session_manager_remove_subscribe_namespace_handler(quicr_session_manager_t* manager,
                                                             quicr_session_t* session,
                                                             const quicr_subscribe_namespace_handler_t* handler);

    QUICR_C_API quicr_status_t
    quicr_session_manager_add_track_status_track_handler(quicr_session_manager_t* manager,
                                                         quicr_session_t* session,
                                                         const quicr_track_status_track_handler_t* handler);

    QUICR_C_API quicr_status_t
    quicr_session_manager_remove_track_status_track_handler(quicr_session_manager_t* manager,
                                                            quicr_session_t* session,
                                                            const quicr_track_status_track_handler_t* handler);

#ifdef __cplusplus
}
#endif

#endif
