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

    /**
     * @name C API status codes
     * @{
     */

    /**
     * @brief Status returned by fallible C API operations.
     *
     * Unless stated otherwise, pointer parameters are required, input data is copied before return, output
     * parameters are written only as documented, and allocation failures or unexpected C++ exceptions map to
     * `QUICR_STATUS_OUT_OF_MEMORY` or `QUICR_STATUS_INTERNAL_ERROR`.
     */
    typedef int32_t quicr_status_t;
    enum
    {
        QUICR_STATUS_OK = 0,               ///< The operation completed.
        QUICR_STATUS_INVALID_ARGUMENT = 1, ///< A required pointer, index, value, or object state was invalid.
        QUICR_STATUS_OUT_OF_MEMORY = 2,    ///< Storage could not be allocated.
        QUICR_STATUS_INTERNAL_ERROR = 3,   ///< An unexpected implementation error occurred.
        QUICR_STATUS_NOT_FOUND = 4,        ///< The referenced live session or resource no longer exists.
        QUICR_STATUS_BUFFER_TOO_SMALL = 5, ///< The supplied output buffer is too small; retry with the reported size.
        QUICR_STATUS_BUSY = 6, ///< The object is registered or a callback is active. Remove it first, return from the
                               ///< callback if applicable, then retry the same operation.
    };
    /** @} */

    /*=======================================================================*/
    // Core Types
    /*=======================================================================*/

    /**
     * @brief Non-owning byte span.
     *
     * Input spans need only remain valid for the call. Output and callback spans are borrowed and remain valid only
     * for the documented owner or callback duration; callers must copy data they retain.
     */
    typedef struct quicr_byte_span
    {
        const uint8_t* data; ///< First byte, or NULL when size is zero.
        size_t size;         ///< Number of bytes at data.
    } quicr_byte_span_t;

    /**
     * @brief Object location within a track.
     */
    typedef struct quicr_location
    {
        uint64_t group;  ///< Group identifier.
        uint64_t object; ///< Object identifier within the group.
    } quicr_location_t;

    /**
     * @name Group ordering constants
     * @{
     */

    /** @brief Requested or selected group delivery order. */
    typedef uint8_t quicr_group_order_t;
    enum
    {
        QUICR_GROUP_ORDER_UNSPECIFIED = 0,  ///< No order preference; valid only where optional order is supported.
        QUICR_GROUP_ORDER_ASCENDING = 0x1,  ///< Deliver lower group IDs first.
        QUICR_GROUP_ORDER_DESCENDING = 0x2, ///< Deliver higher group IDs first.
    };
    /** @} */

    /**
     * @brief Inclusive fetch range endpoint.
     */
    typedef struct quicr_fetch_end_location
    {
        uint64_t group;  ///< Last group to fetch.
        bool has_object; ///< Whether object is present.
        uint64_t object; ///< Last object when has_object is true; otherwise ignored.
    } quicr_fetch_end_location_t;

    /** @brief Opaque, owned track namespace. */
    typedef struct quicr_track_namespace quicr_track_namespace_t;

    /** @brief Opaque, owned namespace plus track-name tuple. */
    typedef struct quicr_full_track_name quicr_full_track_name_t;

    /** @brief Opaque, owned mutable collection of track extensions. */
    typedef struct quicr_track_extensions quicr_track_extensions_t;

    /**
     * @brief Create a namespace by copying count entries.
     *
     * @param entries Input array; may be NULL only when count is zero. Each span is copied.
     * @param count Number of entries.
     * @param track_namespace Receives a new owned object on success and NULL on failure.
     *
     * @return `OK` on success; `INVALID_ARGUMENT` for invalid pointers or entries; `OUT_OF_MEMORY` if allocation
     * fails; otherwise `INTERNAL_ERROR`.
     */
    QUICR_C_API quicr_status_t quicr_track_namespace_create(const quicr_byte_span_t* entries,
                                                            size_t count,
                                                            quicr_track_namespace_t** track_namespace);

    /**
     * @brief Destroy an owned namespace. NULL is allowed.
     *
     * @param track_namespace Owned namespace to destroy, or NULL.
     */
    QUICR_C_API void quicr_track_namespace_destroy(quicr_track_namespace_t* track_namespace);

    /**
     * @brief Read the namespace entry count.
     *
     * @param track_namespace Namespace to inspect, or NULL.
     *
     * @return The entry count, or zero when track_namespace is NULL.
     */
    QUICR_C_API size_t quicr_track_namespace_count(const quicr_track_namespace_t* track_namespace);

    /**
     * @brief Borrow one namespace entry.
     *
     * @param track_namespace Namespace owner.
     * @param index Zero-based entry index.
     * @param entry Receives a span valid until track_namespace is destroyed.
     *
     * @return `OK` on success; `INVALID_ARGUMENT` for NULL or an out-of-range index; otherwise `INTERNAL_ERROR`.
     */
    QUICR_C_API quicr_status_t quicr_track_namespace_entry(const quicr_track_namespace_t* track_namespace,
                                                           size_t index,
                                                           quicr_byte_span_t* entry);

    /**
     * @brief Create a full track name by copying its namespace and track name.
     *
     * @param track_namespace Namespace to copy.
     * @param track_name Track-name bytes; NULL data is valid only for an empty span.
     * @param full_track_name Receives a new owned object on success and NULL on failure.
     *
     * @return `OK` on success; `INVALID_ARGUMENT` for invalid pointers or track-name data; `OUT_OF_MEMORY` if
     * allocation fails; otherwise `INTERNAL_ERROR`.
     */
    QUICR_C_API quicr_status_t quicr_full_track_name_create(const quicr_track_namespace_t* track_namespace,
                                                            quicr_byte_span_t track_name,
                                                            quicr_full_track_name_t** full_track_name);

    /**
     * @brief Destroy an owned full track name. NULL is allowed.
     *
     * @param full_track_name Owned full track name to destroy, or NULL.
     */
    QUICR_C_API void quicr_full_track_name_destroy(quicr_full_track_name_t* full_track_name);

    /**
     * @brief Read the namespace entry count.
     *
     * @param full_track_name Full track name to inspect, or NULL.
     *
     * @return The namespace entry count, or zero when full_track_name is NULL.
     */
    QUICR_C_API size_t quicr_full_track_name_namespace_count(const quicr_full_track_name_t* full_track_name);

    /**
     * @brief Borrow a namespace entry.
     *
     * @param full_track_name Full track name to inspect.
     * @param index Zero-based namespace entry index.
     * @param entry Receives a span valid until full_track_name is destroyed.
     *
     * @return `OK` on success; `INVALID_ARGUMENT` for NULL or an out-of-range index; otherwise `INTERNAL_ERROR`.
     */
    QUICR_C_API quicr_status_t quicr_full_track_name_namespace_entry(const quicr_full_track_name_t* full_track_name,
                                                                     size_t index,
                                                                     quicr_byte_span_t* entry);

    /**
     * @brief Borrow the track-name bytes.
     *
     * @param full_track_name Full track name to inspect, or NULL.
     *
     * @return Bytes valid until destruction, or an empty span when full_track_name is NULL.
     */
    QUICR_C_API quicr_byte_span_t quicr_full_track_name_track_name(const quicr_full_track_name_t* full_track_name);

    /**
     * @name Request error codes
     * @{
     */

    /** @brief Rejection code for general requests. */
    typedef uint8_t quicr_request_error_code_t;
    enum
    {
        QUICR_REQUEST_ERROR_INTERNAL_ERROR = 1,              ///< Unspecified endpoint failure.
        QUICR_REQUEST_ERROR_UNAUTHORIZED = 2,                ///< Requester is not authorized.
        QUICR_REQUEST_ERROR_TIMEOUT = 3,                     ///< Request expired.
        QUICR_REQUEST_ERROR_NOT_SUPPORTED = 4,               ///< Operation is unsupported.
        QUICR_REQUEST_ERROR_MALFORMED_AUTH_TOKEN = 5,        ///< Token is malformed.
        QUICR_REQUEST_ERROR_EXPIRED_AUTH_TOKEN = 6,          ///< Token expired.
        QUICR_REQUEST_ERROR_DOES_NOT_EXIST = 7,              ///< Resource does not exist.
        QUICR_REQUEST_ERROR_INVALID_RANGE = 8,               ///< Object range is invalid.
        QUICR_REQUEST_ERROR_MALFORMED_TRACK = 9,             ///< Track data is malformed.
        QUICR_REQUEST_ERROR_DUPLICATE_SUBSCRIPTION = 10,     ///< Subscription already exists.
        QUICR_REQUEST_ERROR_UNINTERESTED = 11,               ///< Endpoint is no longer interested.
        QUICR_REQUEST_ERROR_PREFIX_OVERLAP = 12,             ///< Namespace prefixes conflict.
        QUICR_REQUEST_ERROR_INVALID_JOINING_REQUEST_ID = 13, ///< Joining request ID is invalid.
    };
    /** @} */

    /**
     * @name Publish error codes
     * @{
     */

    /** @brief Rejection code for an inbound publish. */
    typedef uint8_t quicr_publish_error_code_t;
    enum
    {
        QUICR_PUBLISH_ERROR_INTERNAL_ERROR = 1, ///< Unspecified endpoint failure.
        QUICR_PUBLISH_ERROR_NOT_SUPPORTED = 2,  ///< Receiving publication is unsupported.
        QUICR_PUBLISH_ERROR_NOT_AUTHORIZED = 3, ///< Publisher is unauthorized.
        QUICR_PUBLISH_ERROR_REJECTED = 4,       ///< Publication was declined.
    };
    /** @} */

    /**
     * @name Namespace-publish error codes
     * @{
     */

    /** @brief Rejection code for namespace publication. */
    typedef uint8_t quicr_publish_namespace_error_code_t;
    enum
    {
        QUICR_PUBLISH_NAMESPACE_ERROR_OK = 0,             ///< Vestigial success value; do not use in a rejection.
        QUICR_PUBLISH_NAMESPACE_ERROR_INTERNAL_ERROR = 1, ///< Unspecified endpoint failure.
    };
    /** @} */

    /**
     * @name Fetch error codes
     * @{
     */

    /** @brief Rejection code for fetch operations. */
    typedef uint8_t quicr_fetch_error_code_t;
    enum
    {
        QUICR_FETCH_ERROR_OK = 0,             ///< Vestigial success value; do not use in a rejection.
        QUICR_FETCH_ERROR_INVALID_RANGE = 1,  ///< Requested range is invalid.
        QUICR_FETCH_ERROR_NO_OBJECTS = 2,     ///< No objects are available.
        QUICR_FETCH_ERROR_INTERNAL_ERROR = 3, ///< Unspecified endpoint failure.
    };
    /** @} */

    /**
     * @name Protocol error codes
     * @{
     */

    /** @brief Protocol error code used by generic error replies and handlers. */
    typedef uint64_t quicr_error_code_t;
    enum
    {
        QUICR_ERROR_INTERNAL_ERROR = 0x0,              ///< Unspecified endpoint failure.
        QUICR_ERROR_UNAUTHORIZED = 0x1,                ///< Requester is unauthorized.
        QUICR_ERROR_TIMEOUT = 0x2,                     ///< Operation timed out.
        QUICR_ERROR_NOT_SUPPORTED = 0x3,               ///< Operation is unsupported.
        QUICR_ERROR_MALFORMED_AUTH_TOKEN = 0x4,        ///< Token is malformed.
        QUICR_ERROR_EXPIRED_AUTH_TOKEN = 0x5,          ///< Token expired.
        QUICR_ERROR_DOES_NOT_EXIST = 0x10,             ///< Resource does not exist.
        QUICR_ERROR_INVALID_RANGE = 0x11,              ///< Range is invalid.
        QUICR_ERROR_MALFORMED_TRACK = 0x12,            ///< Track data is malformed.
        QUICR_ERROR_DUPLICATE_SUBSCRIPTION = 0x19,     ///< Subscription already exists.
        QUICR_ERROR_UNINTERESTED = 0x20,               ///< Endpoint is uninterested.
        QUICR_ERROR_PREFIX_OVERLAP = 0x30,             ///< Namespace prefixes conflict.
        QUICR_ERROR_INVALID_JOINING_REQUEST_ID = 0x32, ///< Joining request ID is invalid.
    };
    /** @} */

    /**
     * @name Congestion-control constants
     * @{
     */

    /** @brief QUIC congestion-control algorithm. */
    typedef uint8_t quicr_congestion_control_t;
    enum
    {
        QUICR_CONGESTION_CONTROL_BBR = 0,  ///< BBR.
        QUICR_CONGESTION_CONTROL_C4,       ///< C4.
        QUICR_CONGESTION_CONTROL_CUBIC,    ///< CUBIC.
        QUICR_CONGESTION_CONTROL_NEW_RENO, ///< New Reno.
    };
    /** @} */

    /**
     * @brief Transport configuration initialized by quicr_transport_config_init().
     *
     * String pointers are borrowed during transport creation. All are nullable; NULL is converted to an empty
     * string. Time suffixes specify units. Other numeric fields use the transport implementation's native units and
     * initialized defaults.
     */
    typedef struct quicr_transport_config
    {
        const char* tls_cert_filename;                 ///< TLS certificate path; NULL is converted to an empty string.
        const char* tls_key_filename;                  ///< TLS private-key path; NULL is converted to an empty string.
        uint32_t time_queue_init_queue_size;           ///< Initial time-queue capacity; default 1000.
        uint32_t time_queue_max_duration;              ///< Maximum duration in milliseconds; default 2000.
        uint32_t time_queue_bucket_interval;           ///< Bucket width in milliseconds; default 500.
        uint32_t time_queue_rx_size;                   ///< Receive queue capacity; default 1000.
        bool debug;                                    ///< Enable debug behavior; default false.
        uint64_t quic_cwin_minimum;                    ///< Minimum congestion window in bytes; default 131072.
        uint32_t quic_wifi_shadow_rtt_us;              ///< Wi-Fi shadow RTT in microseconds; default 20000.
        uint64_t idle_timeout_ms;                      ///< Idle timeout in milliseconds; default 30000.
        quicr_congestion_control_t congestion_control; ///< Congestion-control selection; default BBR.
        const char* quic_qlog_path;       ///< QLOG path; NULL is converted to empty, which disables output.
        uint8_t quic_priority_limit;      ///< Pacing/CC bypass limit; default 0.
        size_t max_connections;           ///< Maximum active connections; default 2.
        bool ssl_keylog;                  ///< Enable TLS key logging; default false.
        size_t socket_buffer_size;        ///< Requested socket bytes; default 1000000.
        uint32_t callback_queue_size;     ///< Queued callback capacity; default 2000.
        uint64_t metrics_sample_ms;       ///< Metrics period in milliseconds; default 5000.
        uint64_t initial_max_stream_data; ///< Initial receive-window bytes; default 0 uses QUIC default.
        uint64_t initial_max_stream_id;   ///< Initial stream limit; default 512.
        size_t quic_shards;               ///< Server QUIC-instance count; default 1.
        bool use_af_xdp;                  ///< Enable AF_XDP where supported; default true.
    } quicr_transport_config_t;

    /**
     * @brief Fill config with library defaults. NULL is ignored; existing contents are overwritten.
     *
     * @param config Configuration to initialize, or NULL.
     */
    QUICR_C_API void quicr_transport_config_init(quicr_transport_config_t* config);

    /**
     * @brief Client session configuration.
     */
    typedef struct quicr_client_config
    {
        const char* endpoint_id;                   ///< Borrowed endpoint ID; NULL is converted to an empty string.
        quicr_transport_config_t transport_config; ///< Transport settings.
        uint32_t unknown_stream_expiry_ms;         ///< Unclassified-stream lifetime in milliseconds; default 3000.

        const char* connect_uri;              ///< Borrowed connection URI; NULL is converted to an empty string.
        uint64_t tick_service_sleep_delay_us; ///< Tick delay in microseconds; default 333.
    } quicr_client_config_t;

    /**
     * @brief Fill config, including nested transport settings, with defaults. NULL is ignored.
     *
     * @param config Configuration to initialize, or NULL.
     */
    QUICR_C_API void quicr_client_config_init(quicr_client_config_t* config);

    /**
     * @brief Server transport configuration.
     */
    typedef struct quicr_server_config
    {
        const char* endpoint_id;                   ///< Borrowed endpoint ID; NULL is converted to an empty string.
        quicr_transport_config_t transport_config; ///< Transport settings.
        uint32_t unknown_stream_expiry_ms;         ///< Unclassified-stream lifetime in milliseconds; default 3000.

        const char* bind_ip;                  ///< Borrowed bind address; NULL becomes empty, which selects the default.
        uint16_t port;                        ///< UDP port; default 0 requests automatic selection.
        uint64_t tick_service_sleep_delay_us; ///< Tick delay in microseconds; default 333.
    } quicr_server_config_t;

    /**
     * @brief Fill config, including nested transport settings, with defaults. NULL is ignored.
     *
     * @param config Configuration to initialize, or NULL.
     */
    QUICR_C_API void quicr_server_config_init(quicr_server_config_t* config);

    /*=======================================================================*/
    // Connection
    /*=======================================================================*/

    /**
     * @name Stream closure flags
     * @{
     */

    /** @brief Reason a QUIC stream direction closed. */
    typedef uint8_t quicr_stream_closed_flag_t;
    enum
    {
        QUICR_STREAM_CLOSED_FLAG_FIN = 0,      ///< Graceful FIN.
        QUICR_STREAM_CLOSED_FLAG_RESET,        ///< Peer or transport reset.
        QUICR_STREAM_CLOSED_FLAG_STOP_SENDING, ///< Peer requested that sending stop.
    };
    /** @} */

    /*=======================================================================*/
    // Session
    /*=======================================================================*/

    /**
     * @brief Borrowed session handle owned by its session manager.
     *
     * A client handle is valid from successful add_client until session_removed or manager destruction. A server
     * handle is valid from new_server_session through session_removed. The handle passed to session_removed is
     * callback-scoped and becomes invalid on return. Never free a session handle.
     */
    typedef struct quicr_session quicr_session_t;

    /** @brief Owned response builder used only while returning an accepted publish reply. */
    typedef struct quicr_publish_response quicr_publish_response_t;

    /**
     * @brief Track-status response.
     */
    typedef struct quicr_track_status_response
    {
        bool has_largest_location;         ///< Whether largest_location is present.
        quicr_location_t largest_location; ///< Largest location when present.
        const quicr_track_extensions_t*
          track_extensions; ///< Borrowed extensions; callback-scoped in replies, or points to caller storage supplied
                            ///< to quicr_track_status_handler_get_response().
    } quicr_track_status_response_t;

    /**
     * @brief Successful subscribe response returned by a server callback.
     */
    typedef struct quicr_subscribe_response
    {
        quicr_track_status_response_t track_status; ///< Track status.
        bool is_publisher_initiated;                ///< Whether the publisher initiated the subscription.
        bool has_expires;                           ///< Whether expires_ms is present.
        uint64_t expires_ms;                        ///< Expiration interval in milliseconds when present.
    } quicr_subscribe_response_t;

    /**
     * @brief Successful fetch response.
     */
    typedef struct quicr_fetch_response
    {
        bool has_largest_location;                         ///< Whether largest_location is present.
        quicr_location_t largest_location;                 ///< Largest available object when present.
        quicr_group_order_t publisher_default_group_order; ///< Publisher's default group order.
    } quicr_fetch_response_t;

    /** @brief Callback-scoped peer setup attributes. */
    typedef struct quicr_setup_attributes quicr_setup_attributes_t;

    /** @brief Callback-scoped inbound publish attributes; inspect with accessor functions. */
    typedef struct quicr_publish_attributes quicr_publish_attributes_t;

    /** @brief Callback-scoped namespace-publish attributes. */
    typedef struct quicr_publish_namespace_attributes quicr_publish_namespace_attributes_t;

    /** @brief Callback-scoped standalone-fetch attributes. */
    typedef struct quicr_standalone_fetch_attributes quicr_standalone_fetch_attributes_t;

    /** @brief Callback-scoped joining-fetch attributes. */
    typedef struct quicr_joining_fetch_attributes quicr_joining_fetch_attributes_t;

    /** @brief Callback-scoped subscribe attributes; inspect with accessor functions. */
    typedef struct quicr_subscribe_attributes quicr_subscribe_attributes_t;

    /** @brief Callback-scoped namespace-subscribe attributes. */
    typedef struct quicr_subscribe_namespace_attributes quicr_subscribe_namespace_attributes_t;

    /** @brief Opaque namespace-subscription handler. */
    typedef struct quicr_subscribe_namespace_handler quicr_subscribe_namespace_handler_t;

    /**
     * @brief Borrowed list returned from a callback.
     *
     * The list, array, namespaces, and their data need only remain valid until the callback returns; the library
     * copies them before return processing completes.
     */
    typedef struct quicr_track_namespace_list
    {
        const quicr_track_namespace_t* const* entries; ///< Array of borrowed namespace pointers.
        size_t count;                                  ///< Number of entries.
    } quicr_track_namespace_list_t;

    /**
     * @brief General request rejection.
     */
    typedef struct quicr_request_error
    {
        quicr_request_error_code_t code; ///< Rejection code.
        const char* reason; ///< Optional borrowed UTF-8 reason; NULL means no reason and is copied on return.
    } quicr_request_error_t;

    /**
     * @brief Publish rejection.
     */
    typedef struct quicr_publish_error
    {
        quicr_publish_error_code_t code; ///< Rejection code.
        const char* reason;              ///< Optional borrowed reason copied before callback return.
    } quicr_publish_error_t;

    /**
     * @brief Namespace-publish rejection.
     */
    typedef struct quicr_publish_namespace_error
    {
        quicr_publish_namespace_error_code_t code; ///< Rejection code.
        const char* reason;                        ///< Optional borrowed reason copied before callback return.
    } quicr_publish_namespace_error_t;

    /**
     * @brief Fetch rejection.
     */
    typedef struct quicr_fetch_error
    {
        quicr_fetch_error_code_t code; ///< Rejection code.
        const char* reason;            ///< Optional borrowed reason copied before callback return.
    } quicr_fetch_error_t;

    /**
     * @brief Generic protocol rejection.
     */
    typedef struct quicr_error
    {
        quicr_error_code_t code; ///< Protocol error code.
        const char* reason;      ///< Optional borrowed reason copied before callback return.
    } quicr_error_t;

    /**
     * @name Reply result constants
     * @{
     */

    /** @brief Discriminator for callback reply structures. */
    typedef uint8_t quicr_reply_result_t;
    enum
    {
        QUICR_REPLY_RESULT_ACCEPT = 0, ///< Use the response union member, or ignore error for response-less replies.
        QUICR_REPLY_RESULT_REJECT,     ///< Use the error union member.
    };
    /** @} */

    /**
     * @brief Reply to an inbound publish.
     */
    typedef struct quicr_publish_reply
    {
        quicr_reply_result_t result; ///< Union discriminator.
        union
        {
            const quicr_publish_response_t*
              response; ///< Active on ACCEPT; required and consumed synchronously, but ownership remains with the
                        ///< caller, which must destroy it after the callback returns.
            quicr_publish_error_t error; ///< Active on REJECT.
        };
    } quicr_publish_reply_t;

    /**
     * @brief Subscribe reply.
     */
    typedef struct quicr_subscribe_reply
    {
        quicr_reply_result_t result; ///< Union discriminator.
        union
        {
            quicr_subscribe_response_t response; ///< Active on ACCEPT and copied before return.
            quicr_request_error_t error;         ///< Active on REJECT.
        };
    } quicr_subscribe_reply_t;

    /**
     * @brief Namespace-publish reply.
     */
    typedef struct quicr_publish_namespace_reply
    {
        quicr_reply_result_t result;           ///< ACCEPT or REJECT.
        quicr_publish_namespace_error_t error; ///< Used only on REJECT.
    } quicr_publish_namespace_reply_t;

    /**
     * @brief Fetch reply.
     */
    typedef struct quicr_fetch_reply
    {
        quicr_reply_result_t result; ///< Union discriminator.
        union
        {
            quicr_fetch_response_t response; ///< Active on ACCEPT and copied before return.
            quicr_fetch_error_t error;       ///< Active on REJECT.
        };
    } quicr_fetch_reply_t;

    /**
     * @brief Fetch-cancel reply.
     */
    typedef struct quicr_fetch_cancel_reply
    {
        quicr_reply_result_t result; ///< ACCEPT or REJECT.
        quicr_fetch_error_t error;   ///< Used only on REJECT.
    } quicr_fetch_cancel_reply_t;

    /**
     * @brief Track-status reply.
     */
    typedef struct quicr_track_status_reply
    {
        quicr_reply_result_t result; ///< Union discriminator.
        union
        {
            quicr_track_status_response_t response; ///< Active on ACCEPT; nested extensions are copied before return.
            quicr_request_error_t error;            ///< Active on REJECT.
        };
    } quicr_track_status_reply_t;

    /**
     * @brief Namespace-list reply.
     */
    typedef struct quicr_track_namespaces_reply
    {
        quicr_reply_result_t result; ///< Union discriminator.
        union
        {
            const quicr_track_namespace_list_t* response; ///< Active on ACCEPT; may be NULL for an empty result. All
                                                          ///< contents are borrowed and copied before callback return.
            quicr_request_error_t error;                  ///< Active on REJECT.
        };
    } quicr_track_namespaces_reply_t;

    /**
     * @brief Response-less request reply.
     */
    typedef struct quicr_request_reply
    {
        quicr_reply_result_t result; ///< ACCEPT or REJECT.
        quicr_request_error_t error; ///< Used only on REJECT.
    } quicr_request_reply_t;

    /**
     * @brief Generic response-less reply.
     */
    typedef struct quicr_error_reply
    {
        quicr_reply_result_t result; ///< ACCEPT or REJECT.
        quicr_error_t error;         ///< Used only on REJECT.
    } quicr_error_reply_t;

    /**
     * @name Session lifecycle statuses
     *
     * Typical client progression is NOT_CONNECTED/CONNECTING, PENDING_PEER_SETUP, READY, DISCONNECTING,
     * NOT_CONNECTED; errors may transition to an error or failure state.
     *
     * @{
     */

    /** @brief Session lifecycle status reported asynchronously. */
    typedef uint8_t quicr_session_status_t;
    enum
    {
        QUICR_SESSION_STATUS_READY = 0,          ///< Session is established and usable.
        QUICR_SESSION_STATUS_NOT_READY,          ///< Session is not currently usable.
        QUICR_SESSION_STATUS_INTERNAL_ERROR,     ///< Session failed internally.
        QUICR_SESSION_STATUS_INVALID_PARAMS,     ///< Configuration was invalid.
        QUICR_SESSION_STATUS_CONNECTING,         ///< Connection establishment is in progress.
        QUICR_SESSION_STATUS_DISCONNECTING,      ///< Shutdown is in progress.
        QUICR_SESSION_STATUS_NOT_CONNECTED,      ///< No connection is established.
        QUICR_SESSION_STATUS_FAILED_TO_CONNECT,  ///< Connection establishment failed.
        QUICR_SESSION_STATUS_PENDING_PEER_SETUP, ///< Transport is connected and awaiting peer setup.
    };
    /** @} */

    /**
     * @brief Client-session callbacks copied when the client is added.
     *
     * All callbacks are optional. They may run synchronously on an API caller thread, including while session locks
     * are held, or on a transport notification thread. Callbacks may be concurrent across objects or transports.
     * Return promptly. Callback arguments other than the session handle and user_data are borrowed only for the call.
     * Do not re-enter manager, session, or handler operations on the same objects unless a specific API explicitly
     * permits it. Never destroy a callback-owning object from its callback. The application must keep user_data valid
     * until session_removed or manager destruction. Reply callbacks must return a fully initialized ACCEPT or REJECT
     * reply; the library copies borrowed reply data.
     */
    typedef struct quicr_session_client_callbacks
    {
        void* user_data; ///< Opaque value passed unchanged to every callback.

        /**
         * @brief Peer SETUP; reject to disconnect.
         *
         * @param session Borrowed session handle.
         * @param attributes Borrowed peer setup attributes.
         * @param user_data Callback table's opaque user data.
         *
         * @return An initialized ACCEPT or REJECT reply.
         */
        quicr_error_reply_t (*setup_received)(quicr_session_t* session,
                                              const quicr_setup_attributes_t* attributes,
                                              void* user_data);

        /**
         * @brief QUIC stream closure notification.
         *
         * @param stream_id Closed QUIC stream identifier.
         * @param flag Reason the stream direction closed.
         * @param user_data Callback table's opaque user data.
         */
        void (*on_stream_closed)(uint64_t stream_id, quicr_stream_closed_flag_t flag, void* user_data);

        /**
         * @brief Session state transition.
         *
         * @param session Borrowed session handle.
         * @param status New session status.
         * @param user_data Callback table's opaque user data.
         */
        void (*status_changed)(quicr_session_t* session, quicr_session_status_t status, void* user_data);

        /**
         * @brief Inbound PUBLISH. handler may be NULL and, when non-NULL, is callback-scoped. Defaults to NOT_SUPPORTED
         * rejection.
         *
         * @param session Borrowed session handle.
         * @param request_id Inbound publish request identifier.
         * @param attributes Borrowed publish attributes.
         * @param handler Optional borrowed namespace-subscribe handler valid only for the callback.
         * @param user_data Callback table's opaque user data.
         *
         * @return An initialized ACCEPT or REJECT reply.
         */
        quicr_publish_reply_t (*publish_received)(quicr_session_t* session,
                                                  uint64_t request_id,
                                                  const quicr_publish_attributes_t* attributes,
                                                  const quicr_subscribe_namespace_handler_t* handler,
                                                  void* user_data);

        /**
         * @brief Inbound namespace publication.
         *
         * @param session Borrowed session handle.
         * @param track_namespace Borrowed published namespace.
         * @param attributes Borrowed namespace-publish attributes.
         * @param user_data Callback table's opaque user data.
         *
         * @return An initialized ACCEPT or REJECT reply.
         */
        quicr_publish_namespace_reply_t (*publish_namespace_received)(
          quicr_session_t* session,
          const quicr_track_namespace_t* track_namespace,
          const quicr_publish_namespace_attributes_t* attributes,
          void* user_data);

        /**
         * @brief Inbound standalone fetch; defaults to rejection.
         *
         * @param session Borrowed session handle.
         * @param request_id Inbound fetch request identifier.
         * @param full_track_name Borrowed requested full track name.
         * @param attributes Borrowed standalone-fetch attributes.
         * @param user_data Callback table's opaque user data.
         *
         * @return An initialized ACCEPT or REJECT reply.
         */
        quicr_fetch_reply_t (*standalone_fetch_received)(quicr_session_t* session,
                                                         uint64_t request_id,
                                                         const quicr_full_track_name_t* full_track_name,
                                                         const quicr_standalone_fetch_attributes_t* attributes,
                                                         void* user_data);

        /**
         * @brief Inbound joining fetch; defaults to rejection.
         *
         * @param session Borrowed session handle.
         * @param request_id Inbound fetch request identifier.
         * @param full_track_name Borrowed requested full track name.
         * @param attributes Borrowed joining-fetch attributes.
         * @param user_data Callback table's opaque user data.
         *
         * @return An initialized ACCEPT or REJECT reply.
         */
        quicr_fetch_reply_t (*joining_fetch_received)(quicr_session_t* session,
                                                      uint64_t request_id,
                                                      const quicr_full_track_name_t* full_track_name,
                                                      const quicr_joining_fetch_attributes_t* attributes,
                                                      void* user_data);

        /**
         * @brief Inbound fetch cancellation.
         *
         * @param session Borrowed session handle.
         * @param request_id Cancelled fetch request identifier.
         * @param user_data Callback table's opaque user data.
         *
         * @return An initialized ACCEPT or REJECT reply.
         */
        quicr_fetch_cancel_reply_t (*fetch_cancel_received)(quicr_session_t* session,
                                                            uint64_t request_id,
                                                            void* user_data);

        /**
         * @brief Inbound track-status query.
         *
         * @param session Borrowed session handle.
         * @param full_track_name Borrowed queried full track name.
         * @param user_data Callback table's opaque user data.
         *
         * @return An initialized ACCEPT or REJECT reply.
         */
        quicr_track_status_reply_t (*track_status_received)(quicr_session_t* session,
                                                            const quicr_full_track_name_t* full_track_name,
                                                            void* user_data);

        /**
         * @brief Subscribe unmatched by a publish handler; defaults to acceptance.
         *
         * @param session Borrowed session handle.
         * @param full_track_name Borrowed requested full track name.
         * @param attributes Borrowed subscribe attributes.
         * @param user_data Callback table's opaque user data.
         *
         * @return An initialized ACCEPT or REJECT reply.
         */
        quicr_error_reply_t (*unpublished_subscribe_received)(quicr_session_t* session,
                                                              const quicr_full_track_name_t* full_track_name,
                                                              const quicr_subscribe_attributes_t* attributes,
                                                              void* user_data);
    } quicr_session_client_callbacks_t;

    /**
     * @brief Server-session callbacks copied when a server is added.
     *
     * The concurrency, borrowing, reply, and user_data contracts are identical to
     * quicr_session_client_callbacks_t. The same callback table and user_data may be used concurrently by sessions
     * accepted on different transports; synchronize application state accordingly.
     */
    typedef struct quicr_session_server_callbacks
    {
        void* user_data; ///< Opaque value passed unchanged to every callback.

        /**
         * @brief Peer SETUP; reject to disconnect.
         *
         * @param session Borrowed session handle.
         * @param attributes Borrowed peer setup attributes.
         * @param user_data Callback table's opaque user data.
         *
         * @return An initialized ACCEPT or REJECT reply.
         */
        quicr_error_reply_t (*setup_received)(quicr_session_t* session,
                                              const quicr_setup_attributes_t* attributes,
                                              void* user_data);

        /**
         * @brief QUIC stream closure notification.
         *
         * @param stream_id Closed QUIC stream identifier.
         * @param flag Reason the stream direction closed.
         * @param user_data Callback table's opaque user data.
         */
        void (*on_stream_closed)(uint64_t stream_id, quicr_stream_closed_flag_t flag, void* user_data);

        /**
         * @brief Session state transition.
         *
         * @param session Borrowed session handle.
         * @param status New session status.
         * @param user_data Callback table's opaque user data.
         */
        void (*status_changed)(quicr_session_t* session, quicr_session_status_t status, void* user_data);

        /**
         * @brief Inbound PUBLISH; handler is nullable and callback-scoped.
         *
         * @param session Borrowed session handle.
         * @param request_id Inbound publish request identifier.
         * @param attributes Borrowed publish attributes.
         * @param handler Optional borrowed namespace-subscribe handler valid only for the callback.
         * @param user_data Callback table's opaque user data.
         *
         * @return An initialized ACCEPT or REJECT reply.
         */
        quicr_publish_reply_t (*publish_received)(quicr_session_t* session,
                                                  uint64_t request_id,
                                                  const quicr_publish_attributes_t* attributes,
                                                  const quicr_subscribe_namespace_handler_t* handler,
                                                  void* user_data);

        /**
         * @brief Inbound namespace publication.
         *
         * @param session Borrowed session handle.
         * @param track_namespace Borrowed published namespace.
         * @param attributes Borrowed namespace-publish attributes.
         * @param user_data Callback table's opaque user data.
         *
         * @return An initialized ACCEPT or REJECT reply.
         */
        quicr_publish_namespace_reply_t (*publish_namespace_received)(
          quicr_session_t* session,
          const quicr_track_namespace_t* track_namespace,
          const quicr_publish_namespace_attributes_t* attributes,
          void* user_data);

        /**
         * @brief Inbound standalone fetch.
         *
         * @param session Borrowed session handle.
         * @param request_id Inbound fetch request identifier.
         * @param full_track_name Borrowed requested full track name.
         * @param attributes Borrowed standalone-fetch attributes.
         * @param user_data Callback table's opaque user data.
         *
         * @return An initialized ACCEPT or REJECT reply.
         */
        quicr_fetch_reply_t (*standalone_fetch_received)(quicr_session_t* session,
                                                         uint64_t request_id,
                                                         const quicr_full_track_name_t* full_track_name,
                                                         const quicr_standalone_fetch_attributes_t* attributes,
                                                         void* user_data);

        /**
         * @brief Inbound joining fetch.
         *
         * @param session Borrowed session handle.
         * @param request_id Inbound fetch request identifier.
         * @param full_track_name Borrowed requested full track name.
         * @param attributes Borrowed joining-fetch attributes.
         * @param user_data Callback table's opaque user data.
         *
         * @return An initialized ACCEPT or REJECT reply.
         */
        quicr_fetch_reply_t (*joining_fetch_received)(quicr_session_t* session,
                                                      uint64_t request_id,
                                                      const quicr_full_track_name_t* full_track_name,
                                                      const quicr_joining_fetch_attributes_t* attributes,
                                                      void* user_data);

        /**
         * @brief Inbound fetch cancellation.
         *
         * @param session Borrowed session handle.
         * @param request_id Cancelled fetch request identifier.
         * @param user_data Callback table's opaque user data.
         *
         * @return An initialized ACCEPT or REJECT reply.
         */
        quicr_fetch_cancel_reply_t (*fetch_cancel_received)(quicr_session_t* session,
                                                            uint64_t request_id,
                                                            void* user_data);

        /**
         * @brief Inbound track-status query.
         *
         * @param session Borrowed session handle.
         * @param full_track_name Borrowed queried full track name.
         * @param user_data Callback table's opaque user data.
         *
         * @return An initialized ACCEPT or REJECT reply.
         */
        quicr_track_status_reply_t (*track_status_received)(quicr_session_t* session,
                                                            const quicr_full_track_name_t* full_track_name,
                                                            void* user_data);

        /**
         * @brief Namespace-prefix subscription; an accepted list is copied before return.
         *
         * @param session Borrowed session handle.
         * @param prefix_namespace Borrowed namespace prefix.
         * @param attributes Borrowed namespace-subscribe attributes.
         * @param user_data Callback table's opaque user data.
         *
         * @return An initialized ACCEPT or REJECT reply.
         */
        quicr_track_namespaces_reply_t (*subscribe_namespace_received)(
          quicr_session_t* session,
          const quicr_track_namespace_t* prefix_namespace,
          const quicr_subscribe_namespace_attributes_t* attributes,
          void* user_data);

        /**
         * @brief Track-prefix subscription; accepted list copied.
         *
         * @param session Borrowed session handle.
         * @param prefix_namespace Borrowed track-prefix namespace.
         * @param attributes Borrowed namespace-subscribe attributes.
         * @param user_data Callback table's opaque user data.
         *
         * @return An initialized ACCEPT or REJECT reply.
         */
        quicr_track_namespaces_reply_t (*subscribe_tracks_received)(
          quicr_session_t* session,
          const quicr_track_namespace_t* prefix_namespace,
          const quicr_subscribe_namespace_attributes_t* attributes,
          void* user_data);

        /**
         * @brief Inbound track subscription.
         *
         * @param session Borrowed session handle.
         * @param request_id Inbound subscribe request identifier.
         * @param full_track_name Borrowed requested full track name.
         * @param attributes Borrowed subscribe attributes.
         * @param user_data Callback table's opaque user data.
         *
         * @return An initialized ACCEPT or REJECT reply.
         */
        quicr_subscribe_reply_t (*subscribe_received)(quicr_session_t* session,
                                                      uint64_t request_id,
                                                      const quicr_full_track_name_t* full_track_name,
                                                      const quicr_subscribe_attributes_t* attributes,
                                                      void* user_data);

        /**
         * @brief Peer group request.
         *
         * @param full_track_name Borrowed full track name.
         * @param group_id Requested group identifier.
         * @param user_data Callback table's opaque user data.
         *
         * @return An initialized ACCEPT or REJECT reply.
         */
        quicr_error_reply_t (*new_group_requested)(const quicr_full_track_name_t* full_track_name,
                                                   uint64_t group_id,
                                                   void* user_data);
    } quicr_session_server_callbacks_t;

    /**
     * @brief Read the current session status.
     *
     * @param session Borrowed session handle.
     * @param status Receives the current session status.
     *
     * @return `OK` on success; `INVALID_ARGUMENT` for NULL; or `INTERNAL_ERROR` if the underlying session expired.
     */
    QUICR_C_API quicr_status_t quicr_session_get_status(const quicr_session_t* session, quicr_session_status_t* status);

    /**
     * @brief Begin asynchronous session disconnection.
     *
     * Status callbacks report subsequent transitions. The manager retains the handle until session_removed.
     *
     * @param session Borrowed session handle.
     *
     * @return `OK` when disconnection starts; `INVALID_ARGUMENT` for NULL; `NOT_FOUND` if the session is not live;
     * `OUT_OF_MEMORY` if allocation fails; otherwise `INTERNAL_ERROR`.
     */
    QUICR_C_API quicr_status_t quicr_session_disconnect(quicr_session_t* session);

    /**
     * @brief Send GOAWAY for a session.
     *
     * @param session Live borrowed session handle.
     * @param new_session_uri Optional borrowed URI copied during the call; NULL means an empty URI.
     * @param timeout_ms Drain timeout in milliseconds.
     * @param first_unprocessed_request_id Optional borrowed request-ID cutoff copied during the call.
     *
     * @return `OK` when GOAWAY is sent; `INVALID_ARGUMENT` for invalid inputs; `NOT_FOUND` if the session is not live;
     * `OUT_OF_MEMORY` if allocation fails; otherwise `INTERNAL_ERROR`.
     */
    QUICR_C_API quicr_status_t quicr_session_goaway(quicr_session_t* session,
                                                    const char* new_session_uri,
                                                    uint64_t timeout_ms,
                                                    const uint64_t* first_unprocessed_request_id);

    /*=======================================================================*/
    // Handlers
    /*=======================================================================*/

    /**
     * @name Track transport modes
     * @{
     */

    /** @brief Object transport mode for a track. */
    typedef uint8_t quicr_track_mode_t;
    enum
    {
        QUICR_TRACK_MODE_DATAGRAM = 0, ///< Send objects as unreliable datagrams.
        QUICR_TRACK_MODE_STREAM,       ///< Send objects on reliable streams.
    };
    /** @} */

    /**
     * @name Object status codes
     * @{
     */

    /** @brief Protocol status represented by an object header. */
    typedef uint8_t quicr_object_status_t;
    enum
    {
        QUICR_OBJECT_STATUS_AVAILABLE = 0x0,      ///< Payload is available.
        QUICR_OBJECT_STATUS_DOES_NOT_EXIST = 0x1, ///< Object does not exist.
        QUICR_OBJECT_STATUS_END_OF_GROUP = 0x3,   ///< Marks end of group.
        QUICR_OBJECT_STATUS_END_OF_TRACK = 0x4,   ///< Marks end of track.
        QUICR_OBJECT_STATUS_END_OF_SUBGROUP = 0x5 ///< Marks end of subgroup.
    };
    /** @} */

    /**
     * @brief One extension type with one or more borrowed values.
     */
    typedef struct quicr_extension_entry
    {
        uint64_t type;                   ///< Extension type.
        const quicr_byte_span_t* values; ///< Borrowed value-span array.
        size_t count;                    ///< Number of values.
    } quicr_extension_entry_t;

    /**
     * @brief Borrowed extension collection.
     */
    typedef struct quicr_extensions
    {
        const quicr_extension_entry_t* entries; ///< Borrowed entry array.
        size_t count;                           ///< Number of entries.
    } quicr_extensions_t;

    /**
     * @brief Object metadata for publishing and receiving.
     *
     * Publish calls copy metadata and extension bytes before returning. Callback instances and nested pointers are
     * valid only for the callback.
     */
    typedef struct quicr_object_headers
    {
        uint64_t group_id;            ///< Group ID.
        uint64_t object_id;           ///< Object ID.
        uint64_t subgroup_id;         ///< Subgroup ID.
        uint64_t payload_length;      ///< Declared complete payload length in bytes.
        quicr_object_status_t status; ///< Object availability or terminal status.

        bool has_priority; ///< Whether priority is present.
        uint8_t priority;  ///< Priority when present; lower values have higher priority.

        bool has_ttl; ///< Whether ttl is present.
        uint16_t ttl; ///< Time to live in milliseconds when present.

        bool has_track_mode;           ///< Whether track_mode is present.
        quicr_track_mode_t track_mode; ///< Transport mode when present.

        const quicr_extensions_t* extensions;           ///< Optional borrowed mutable-extension collection.
        const quicr_extensions_t* immutable_extensions; ///< Optional borrowed immutable-extension collection.
    } quicr_object_headers_t;

    /**
     * @brief Zero-initialize headers. NULL is ignored.
     *
     * @param headers Object headers to initialize, or NULL.
     */
    QUICR_C_API void quicr_object_headers_init(quicr_object_headers_t* headers);

    /**
     * @name Subgroup ID encoding flags
     * @{
     */

    /** @brief Stream-header subgroup-ID encoding mode. Values are flags, but select exactly one mode. */
    typedef uint8_t quicr_subgroup_id_type_t;
    enum
    {
        QUICR_SUBGROUP_ID_TYPE_IS_ZERO = 1 << 0u,               ///< Subgroup ID is implicitly zero.
        QUICR_SUBGROUP_ID_TYPE_SET_FROM_FIRST_OBJECT = 1 << 1u, ///< Derive it from the first object.
        QUICR_SUBGROUP_ID_TYPE_EXPLICIT = 1 << 2u,              ///< Encode it explicitly.
        QUICR_SUBGROUP_ID_TYPE_RESERVED = 1 << 3u,              ///< Reserved; do not use.
    };
    /** @} */

    /**
     * @brief Stream-header encoding properties.
     */
    typedef struct quicr_stream_header_properties
    {
        bool extensions;                           ///< Encode extensions.
        quicr_subgroup_id_type_t subgroup_id_mode; ///< Subgroup-ID mode.
        bool end_of_group;                         ///< Mark end of group.
        bool default_priority;                     ///< Use default priority.
        bool first_object;                         ///< Mark first object.
    } quicr_stream_header_properties_t;

    /**
     * @brief Zero-initialize properties and select IS_ZERO subgroup mode. NULL is ignored.
     *
     * @param properties Stream-header properties to initialize, or NULL.
     */
    QUICR_C_API void quicr_stream_header_properties_init(quicr_stream_header_properties_t* properties);

    /**
     * @brief Set group and object to zero. NULL is ignored.
     *
     * @param location Location to initialize, or NULL.
     */
    QUICR_C_API void quicr_location_init(quicr_location_t* location);

    /**
     * @brief Accumulator summary.
     */
    typedef struct quicr_min_max_avg
    {
        uint64_t min;         ///< Minimum sample.
        uint64_t max;         ///< Maximum sample.
        uint64_t avg;         ///< Arithmetic average.
        uint64_t value_sum;   ///< Sum of samples.
        uint64_t value_count; ///< Number of samples.
    } quicr_min_max_avg_t;

    /**
     * @brief Per-period subscriber metrics.
     */
    typedef struct quicr_subscribe_track_metrics
    {
        uint64_t last_sample_time; ///< Implementation clock timestamp.
        uint64_t bytes_received;   ///< Bytes received in the period.
        uint64_t objects_received; ///< Objects received in the period.
    } quicr_subscribe_track_metrics_t;

    /**
     * @brief Per-period QUIC publishing metrics.
     */
    typedef struct quicr_publish_track_quic_metrics
    {
        uint64_t tx_buffer_drops;                  ///< Buffer drops.
        uint64_t tx_queue_discards;                ///< Queue discards.
        uint64_t tx_queue_expired;                 ///< Queue expirations.
        uint64_t tx_delayed_callback;              ///< Delayed callbacks.
        quicr_min_max_avg_t tx_queue_size;         ///< Queue-size samples.
        quicr_min_max_avg_t tx_callback_ms;        ///< Callback duration in milliseconds.
        quicr_min_max_avg_t tx_object_duration_us; ///< Object duration in microseconds.
    } quicr_publish_track_quic_metrics_t;

    /**
     * @brief Per-period publisher metrics.
     */
    typedef struct quicr_publish_track_metrics
    {
        uint64_t last_sample_time;               ///< Implementation clock timestamp.
        uint64_t bytes_published;                ///< Bytes published.
        uint64_t objects_published;              ///< Objects published.
        uint64_t objects_dropped_not_ok;         ///< Objects dropped with non-OK status.
        quicr_publish_track_quic_metrics_t quic; ///< QUIC-specific metrics.
    } quicr_publish_track_metrics_t;

    /**
     * @brief Opaque owned publish-track handler.
     *
     * Create, add to one session, remove, then destroy. Destroying NULL succeeds. Destruction returns BUSY while
     * registered or while a callback is active; after removing or returning from the callback, retry destruction.
     */
    typedef struct quicr_publish_track_handler quicr_publish_track_handler_t;

    /**
     * @name Publish-track handler statuses
     * @{
     */

    /** @brief Publish-track handler lifecycle status. */
    typedef uint8_t quicr_publish_track_handler_status_t;
    enum
    {
        QUICR_PUBLISH_TRACK_HANDLER_STATUS_OK = 0,                    ///< Handler is active.
        QUICR_PUBLISH_TRACK_HANDLER_STATUS_NOT_CONNECTED,             ///< Session is not connected.
        QUICR_PUBLISH_TRACK_HANDLER_STATUS_NOT_ANNOUNCED,             ///< Track is not announced.
        QUICR_PUBLISH_TRACK_HANDLER_STATUS_PENDING_ANNOUNCE_RESPONSE, ///< Awaiting announce response.
        QUICR_PUBLISH_TRACK_HANDLER_STATUS_ANNOUNCE_NOT_AUTHORIZED,   ///< Announcement was unauthorized.
        QUICR_PUBLISH_TRACK_HANDLER_STATUS_NO_SUBSCRIBERS,            ///< No subscribers are active.
        QUICR_PUBLISH_TRACK_HANDLER_STATUS_UNSUBSCRIBED,              ///< Peer unsubscribed.
        QUICR_PUBLISH_TRACK_HANDLER_STATUS_DONE_BY_FIN,               ///< Completed by FIN.
        QUICR_PUBLISH_TRACK_HANDLER_STATUS_SENDING_UNANNOUNCE,        ///< Unannouncement is being sent.
        QUICR_PUBLISH_TRACK_HANDLER_STATUS_SUBSCRIPTION_UPDATED,      ///< Subscription changed.
        QUICR_PUBLISH_TRACK_HANDLER_STATUS_NEW_GROUP_REQUESTED,       ///< Peer requested a new group.
        QUICR_PUBLISH_TRACK_HANDLER_STATUS_PENDING_PUBLISH_OK,        ///< Awaiting publish acceptance.
        QUICR_PUBLISH_TRACK_HANDLER_STATUS_PAUSED,                    ///< Publishing is paused.
    };
    /** @} */

    /**
     * @brief Optional callbacks copied at publish-handler creation.
     *
     * Callbacks may run synchronously on an API caller thread, including while session locks are held, or on a
     * transport notification thread. They may be concurrent across objects or transports. Callback pointer arguments
     * are borrowed for the call. Do not re-enter manager, session, or handler operations on the same objects unless a
     * specific API explicitly permits it. Never destroy a callback-owning object from its callback. Keep user_data
     * valid from creation until successful destruction.
     */
    typedef struct quicr_publish_track_handler_callbacks
    {
        void* user_data; ///< Opaque callback value.

        /**
         * @brief Lifecycle transition.
         *
         * @param handler Borrowed publish handler receiving the transition.
         * @param status New handler status.
         * @param user_data Callback table's opaque user data.
         */
        void (*status_changed)(quicr_publish_track_handler_t* handler,
                               quicr_publish_track_handler_status_t status,
                               void* user_data);

        /**
         * @brief Borrowed metrics sample.
         *
         * @param handler Borrowed publish handler associated with the sample.
         * @param metrics Borrowed metrics valid only for the callback.
         * @param user_data Callback table's opaque user data.
         */
        void (*metrics_sampled)(quicr_publish_track_handler_t* handler,
                                const quicr_publish_track_metrics_t* metrics,
                                void* user_data);

        /**
         * @brief Stream closure; reset distinguishes reset from FIN.
         *
         * @param handler Borrowed publish handler whose stream closed.
         * @param stream_id Closed stream identifier.
         * @param reset True for reset, false for graceful FIN.
         * @param user_data Callback table's opaque user data.
         */
        void (*stream_closed)(quicr_publish_track_handler_t* handler, uint64_t stream_id, bool reset, void* user_data);
    } quicr_publish_track_handler_callbacks_t;

    /**
     * @brief Create a detached publish-track handler, copying ftn and callback table.
     *
     * @param callbacks Optional callbacks; NULL installs none.
     * @param ftn Full track name to copy.
     * @param track_mode Datagram or stream mode.
     * @param publisher_priority Publisher priority.
     * @param publish_object_priority Default object priority.
     * @param default_ttl Default TTL in milliseconds.
     * @param largest_location Optional largest previously published location; NULL uses zero.
     * @param handler Receives the owned handler, NULL on failure.
     *
     * @return `OK` on success; `INVALID_ARGUMENT` for invalid required inputs or values; `OUT_OF_MEMORY` if
     * allocation fails; otherwise `INTERNAL_ERROR`.
     */
    QUICR_C_API quicr_status_t
    quicr_publish_track_handler_create(const quicr_publish_track_handler_callbacks_t* callbacks,
                                       const quicr_full_track_name_t* ftn,
                                       quicr_track_mode_t track_mode,
                                       uint8_t publisher_priority,
                                       uint8_t publish_object_priority,
                                       uint32_t default_ttl,
                                       const quicr_location_t* largest_location,
                                       quicr_publish_track_handler_t** handler);

    /**
     * @brief Destroy a detached publish handler.
     *
     * @param handler Handler to destroy.
     *
     * @return `OK` when handler is NULL or destroyed; `BUSY` while registered, internally invalid, or in a callback.
     */
    QUICR_C_API quicr_status_t quicr_publish_track_handler_destroy(quicr_publish_track_handler_t* handler);

    /**
     * @brief Determine whether the handler can currently publish.
     *
     * @param handler Handler to inspect, or NULL.
     *
     * @return Whether the handler can publish; false for NULL.
     */
    QUICR_C_API bool quicr_publish_track_handler_can_publish(const quicr_publish_track_handler_t* handler);

    /**
     * @brief Read handler status.
     *
     * @param handler Handler to inspect.
     * @param status Receives the current handler status.
     *
     * @return `OK` on success; `INVALID_ARGUMENT` when handler or status is NULL; otherwise `INTERNAL_ERROR`.
     */
    QUICR_C_API quicr_status_t quicr_publish_track_handler_get_status(const quicr_publish_track_handler_t* handler,
                                                                      quicr_publish_track_handler_status_t* status);

    /**
     * @name Publish-object result statuses
     * @{
     */

    /** @brief Result of attempting to publish object data. */
    typedef uint8_t quicr_publish_object_status_t;
    enum
    {
        QUICR_PUBLISH_OBJECT_STATUS_OK = 0,                          ///< Object accepted.
        QUICR_PUBLISH_OBJECT_STATUS_INTERNAL_ERROR,                  ///< Internal failure.
        QUICR_PUBLISH_OBJECT_STATUS_NOT_AUTHORIZED,                  ///< Publication unauthorized.
        QUICR_PUBLISH_OBJECT_STATUS_NOT_ANNOUNCED,                   ///< Track not announced.
        QUICR_PUBLISH_OBJECT_STATUS_NO_SUBSCRIBERS,                  ///< No subscribers.
        QUICR_PUBLISH_OBJECT_STATUS_OBJECT_PAYLOAD_LENGTH_EXCEEDED,  ///< More than declared payload length.
        QUICR_PUBLISH_OBJECT_STATUS_PREVIOUS_OBJECT_TRUNCATED,       ///< Previous object was truncated.
        QUICR_PUBLISH_OBJECT_STATUS_NO_PREVIOUS_OBJECT,              ///< No object exists to continue.
        QUICR_PUBLISH_OBJECT_STATUS_OBJECT_DATA_COMPLETE,            ///< Declared object data is complete.
        QUICR_PUBLISH_OBJECT_STATUS_OBJECT_CONTINUATION_DATA_NEEDED, ///< More data is required.
        QUICR_PUBLISH_OBJECT_STATUS_OBJECT_DATA_TOO_LARGE,           ///< Supplied chunk is too large.
        QUICR_PUBLISH_OBJECT_STATUS_OBJECT_DATA_INCOMPLETE,          ///< Object remains incomplete.
        QUICR_PUBLISH_OBJECT_STATUS_PREVIOUS_OBJECT_NOT_COMPLETE_MUST_START_NEW_GROUP, ///< Start a new group.
        QUICR_PUBLISH_OBJECT_STATUS_PREVIOUS_OBJECT_NOT_COMPLETE_MUST_START_NEW_TRACK, ///< Start a new track.
        QUICR_PUBLISH_OBJECT_STATUS_PAUSED,                                            ///< Handler is paused.
        QUICR_PUBLISH_OBJECT_STATUS_PENDING_PUBLISH_OK, ///< Publication acceptance is pending.
        QUICR_PUBLISH_OBJECT_STATUS_SUBGROUP_CLOSED,    ///< Subgroup is closed.
        QUICR_PUBLISH_OBJECT_STATUS_FETCH_CLOSED,       ///< Fetch is closed.
    };
    /** @} */

    /**
     * @brief Publish an object or continuation chunk.
     *
     * headers and properties are borrowed; properties may be NULL. data is consumed or copied before return and may
     * be empty. status may be NULL. `OK` means the call was valid; inspect status for protocol/publish outcome.
     *
     * @param handler Publish handler.
     * @param headers Object metadata to copy.
     * @param data Object payload or continuation bytes.
     * @param properties Optional stream-header properties.
     * @param status Optional output receiving the publish outcome.
     *
     * @return `OK` when the call is valid; `INVALID_ARGUMENT` for invalid inputs; `OUT_OF_MEMORY` if allocation fails;
     * otherwise `INTERNAL_ERROR`. Inspect status for the publish outcome.
     */
    QUICR_C_API quicr_status_t
    quicr_publish_track_handler_publish_object(quicr_publish_track_handler_t* handler,
                                               const quicr_object_headers_t* headers,
                                               quicr_byte_span_t data,
                                               const quicr_stream_header_properties_t* properties,
                                               quicr_publish_object_status_t* status);

    /**
     * @brief End one subgroup; completed selects graceful completion versus incomplete/reset semantics.
     *
     * @param handler Publish handler.
     * @param group_id Group identifier.
     * @param subgroup_id Subgroup identifier.
     * @param completed Whether to complete gracefully.
     *
     * @return `OK` when the subgroup is ended; `INVALID_ARGUMENT` for invalid inputs; `OUT_OF_MEMORY` if allocation
     * fails; otherwise `INTERNAL_ERROR`.
     */
    QUICR_C_API quicr_status_t quicr_publish_track_handler_end_subgroup(quicr_publish_track_handler_t* handler,
                                                                        uint64_t group_id,
                                                                        uint64_t subgroup_id,
                                                                        bool completed);

    /**
     * @brief End all open subgroups; completed selects graceful completion.
     *
     * @param handler Publish handler.
     * @param completed Whether to complete gracefully.
     *
     * @return `OK` when the subgroups are ended; `INVALID_ARGUMENT` for invalid inputs; `OUT_OF_MEMORY` if allocation
     * fails; otherwise `INTERNAL_ERROR`.
     */
    QUICR_C_API quicr_status_t quicr_publish_track_handler_end_all_subgroups(quicr_publish_track_handler_t* handler,
                                                                             bool completed);

    /** @brief Opaque owned subscribe handler with the common create/add/remove/destroy contract. */
    typedef struct quicr_subscribe_track_handler quicr_subscribe_track_handler_t;

    /**
     * @name Subscribe and fetch handler statuses
     * @{
     */

    /** @brief Subscribe/fetch handler lifecycle status. */
    typedef uint8_t quicr_subscribe_track_handler_status_t;
    enum
    {
        QUICR_SUBSCRIBE_TRACK_HANDLER_STATUS_OK = 0,              ///< Handler is active.
        QUICR_SUBSCRIBE_TRACK_HANDLER_STATUS_NOT_CONNECTED,       ///< Session is disconnected.
        QUICR_SUBSCRIBE_TRACK_HANDLER_STATUS_ERROR,               ///< Request failed.
        QUICR_SUBSCRIBE_TRACK_HANDLER_STATUS_NOT_AUTHORIZED,      ///< Request unauthorized.
        QUICR_SUBSCRIBE_TRACK_HANDLER_STATUS_NOT_SUBSCRIBED,      ///< Not subscribed.
        QUICR_SUBSCRIBE_TRACK_HANDLER_STATUS_PENDING_RESPONSE,    ///< Awaiting response.
        QUICR_SUBSCRIBE_TRACK_HANDLER_STATUS_SENDING_UNSUBSCRIBE, ///< Unsubscribe is being sent.
        QUICR_SUBSCRIBE_TRACK_HANDLER_STATUS_PAUSED,              ///< Delivery is paused.
        QUICR_SUBSCRIBE_TRACK_HANDLER_STATUS_NEW_GROUP_REQUESTED, ///< New group was requested.
        QUICR_SUBSCRIBE_TRACK_HANDLER_STATUS_CANCELLED,           ///< Request was cancelled.
        QUICR_SUBSCRIBE_TRACK_HANDLER_STATUS_DONE_BY_FIN,         ///< Completed by FIN.
        QUICR_SUBSCRIBE_TRACK_HANDLER_STATUS_DONE_BY_RESET,       ///< Completed by reset.
    };
    /** @} */

    /**
     * @brief Optional callbacks copied at subscribe-handler creation.
     *
     * The callback threading, concurrency, user_data, re-entry, borrowing, and destruction contracts match publish
     * callbacks.
     */
    typedef struct quicr_subscribe_track_handler_callbacks
    {
        void* user_data; ///< Opaque callback value.

        /**
         * @brief Lifecycle transition.
         *
         * @param handler Borrowed subscribe handler receiving the transition.
         * @param status New handler status.
         * @param user_data Callback table's opaque user data.
         */
        void (*status_changed)(quicr_subscribe_track_handler_t* handler,
                               quicr_subscribe_track_handler_status_t status,
                               void* user_data);

        /**
         * @brief Borrowed metrics sample.
         *
         * @param handler Borrowed subscribe handler associated with the sample.
         * @param metrics Borrowed metrics valid only for the callback.
         * @param user_data Callback table's opaque user data.
         */
        void (*metrics_sampled)(quicr_subscribe_track_handler_t* handler,
                                const quicr_subscribe_track_metrics_t* metrics,
                                void* user_data);

        /**
         * @brief Borrowed headers, data, and properties for an available object.
         *
         * @param headers Borrowed object metadata valid only for the callback.
         * @param data Borrowed object payload valid only for the callback.
         * @param properties Optional borrowed stream-header properties valid only for the callback.
         * @param user_data Callback table's opaque user data.
         */
        void (*object_received)(const quicr_object_headers_t* headers,
                                quicr_byte_span_t data,
                                const quicr_stream_header_properties_t* properties,
                                void* user_data);

        /**
         * @brief Borrowed status-only object headers.
         *
         * @param headers Borrowed status-only object metadata valid only for the callback.
         * @param user_data Callback table's opaque user data.
         */
        void (*object_status_received)(const quicr_object_headers_t* headers, void* user_data);

        /**
         * @brief Subgroup completion; reset is true on reset.
         *
         * @param group_id Completed subgroup's group identifier.
         * @param subgroup_id Completed subgroup identifier.
         * @param reset True when the subgroup ended by reset.
         * @param user_data Callback table's opaque user data.
         */
        void (*subgroup_ended)(uint64_t group_id, uint64_t subgroup_id, bool reset, void* user_data);

        /**
         * @brief Borrowed raw datagram bytes.
         *
         * @param data Borrowed datagram bytes valid only for the callback.
         * @param user_data Callback table's opaque user data.
         */
        void (*datagram_received)(quicr_byte_span_t data, void* user_data);
    } quicr_subscribe_track_handler_callbacks_t;

    /**
     * @name Filter type constants
     * @{
     */

    /** @brief Discriminator for quicr_filter_t. */
    typedef uint64_t quicr_filter_type_t;
    enum
    {
        QUICR_FILTER_TYPE_NONE = 0,            ///< No filter; no union member is active.
        QUICR_FILTER_TYPE_SUBSCRIPTION_FILTER, ///< subscription is active.
        QUICR_FILTER_TYPE_TRACK_FILTER,        ///< track is active.
    };
    /** @} */

    /**
     * @name Subscription filter types
     * @{
     */

    /** @brief Subscription range selection. */
    typedef int32_t quicr_subscription_filter_type_t;
    enum
    {
        QUICR_SUBSCRIPTION_FILTER_TYPE_NEXT_GROUP_START = 0x1, ///< Start at the next group.
        QUICR_SUBSCRIPTION_FILTER_TYPE_LARGEST_OBJECT = 0x2,   ///< Start at the largest available object.
        QUICR_SUBSCRIPTION_FILTER_TYPE_ABSOLUTE_START = 0x3,   ///< Start at start_location.
        QUICR_SUBSCRIPTION_FILTER_TYPE_ABSOLUTE_RANGE =
          0x4, ///< Start at start_location and end after end_group_delta groups.
    };
    /** @} */

    /**
     * @brief Subscription filter parameters.
     */
    typedef struct quicr_subscription_filter
    {
        quicr_subscription_filter_type_t type; ///< Filter selection.
        quicr_location_t start_location;       ///< Start used by absolute filters.
        uint64_t end_group_delta;              ///< Inclusive range delta for ABSOLUTE_RANGE.
    } quicr_subscription_filter_t;

    /**
     * @brief Initialize to NEXT_GROUP_START with zero location/delta. NULL is ignored.
     *
     * @param filter Subscription filter to initialize, or NULL.
     */
    QUICR_C_API void quicr_subscription_filter_init(quicr_subscription_filter_t* filter);

    /**
     * @brief Track-selection filter.
     */
    typedef struct quicr_track_filter
    {
        uint64_t property_type;       ///< Property extension type to match.
        uint64_t max_tracks_selected; ///< Maximum selected tracks.
        uint64_t timeout;             ///< Selection timeout in protocol-defined units.
    } quicr_track_filter_t;

    /**
     * @brief Zero-initialize a track filter. NULL is ignored.
     *
     * @param filter Track filter to initialize, or NULL.
     */
    QUICR_C_API void quicr_track_filter_init(quicr_track_filter_t* filter);

    /**
     * @brief Tagged filter union.
     */
    typedef struct quicr_filter
    {
        quicr_filter_type_t type; ///< Active-member discriminator.

        union
        {
            quicr_subscription_filter_t subscription; ///< Active for SUBSCRIPTION_FILTER.
            quicr_track_filter_t track;               ///< Active for TRACK_FILTER.
        };
    } quicr_filter_t;

    /**
     * @brief Create a detached subscribe handler, copying callbacks, ftn, and optional filter.
     *
     * @param callbacks Optional callback table.
     * @param ftn Full track name to copy.
     * @param priority Subscriber priority.
     * @param group_order Requested order; UNSPECIFIED omits the preference.
     * @param filter Optional filter; NULL selects no filter.
     * @param handler Receives owned handler and is set NULL on failure.
     *
     * @return `OK` on success; `INVALID_ARGUMENT` for invalid required inputs or values; `OUT_OF_MEMORY` if
     * allocation fails; otherwise `INTERNAL_ERROR`.
     */
    QUICR_C_API quicr_status_t
    quicr_subscribe_track_handler_create(const quicr_subscribe_track_handler_callbacks_t* callbacks,
                                         const quicr_full_track_name_t* ftn,
                                         uint8_t priority,
                                         quicr_group_order_t group_order,
                                         const quicr_filter_t* filter,
                                         quicr_subscribe_track_handler_t** handler);

    /**
     * @brief Subscribe callbacks for forwarding encoded stream data.
     */
    typedef struct quicr_forwarding_subscribe_track_handler_callbacks
    {
        quicr_subscribe_track_handler_callbacks_t track_callbacks; ///< Common callbacks and user_data.

        /**
         * @brief Stream subgroup start; properties are borrowed for the callback.
         *
         * @param group_id Started subgroup's group identifier.
         * @param subgroup_id Started subgroup identifier.
         * @param has_priority Whether priority is present.
         * @param priority Subgroup priority when has_priority is true.
         * @param properties Optional borrowed stream-header properties valid only for the callback.
         * @param user_data Callback table's opaque user data.
         */
        void (*subgroup_started)(uint64_t group_id,
                                 uint64_t subgroup_id,
                                 bool has_priority,
                                 uint8_t priority,
                                 const quicr_stream_header_properties_t* properties,
                                 void* user_data);

        /**
         * @brief Borrowed encoded bytes.
         *
         * @param group_id Forwarded subgroup's group identifier.
         * @param subgroup_id Forwarded subgroup identifier.
         * @param data Borrowed encoded bytes valid only for the callback.
         * @param user_data Callback table's opaque user data.
         */
        void (*stream_bytes_forwarded)(uint64_t group_id,
                                       uint64_t subgroup_id,
                                       quicr_byte_span_t data,
                                       void* user_data);
    } quicr_forwarding_subscribe_track_handler_callbacks_t;

    /**
     * @brief Create a detached forwarding subscribe handler.
     *
     * @param callbacks Optional callback table.
     * @param ftn Full track name to copy.
     * @param priority Subscriber priority.
     * @param group_order Requested order; UNSPECIFIED omits the preference.
     * @param filter Optional filter; NULL selects no filter.
     * @param handler Receives owned handler and is set NULL on failure.
     *
     * @return `OK` on success; `INVALID_ARGUMENT` for invalid required inputs or values; `OUT_OF_MEMORY` if
     * allocation fails; otherwise `INTERNAL_ERROR`.
     */
    QUICR_C_API quicr_status_t quicr_forwarding_subscribe_track_handler_create(
      const quicr_forwarding_subscribe_track_handler_callbacks_t* callbacks,
      const quicr_full_track_name_t* ftn,
      uint8_t priority,
      quicr_group_order_t group_order,
      const quicr_filter_t* filter,
      quicr_subscribe_track_handler_t** handler);

    /**
     * @brief Destroy a detached subscribe handler.
     *
     * @param handler Handler to destroy.
     *
     * @return `OK` when handler is NULL or destroyed; `BUSY` while registered, internally invalid, or in a callback.
     */
    QUICR_C_API quicr_status_t quicr_subscribe_track_handler_destroy(quicr_subscribe_track_handler_t* handler);

    /**
     * @brief Read handler status.
     *
     * @param handler Handler to inspect.
     * @param status Receives the current handler status.
     *
     * @return `OK` on success; `INVALID_ARGUMENT` when handler or status is NULL; otherwise `INTERNAL_ERROR`.
     */
    QUICR_C_API quicr_status_t quicr_subscribe_track_handler_get_status(const quicr_subscribe_track_handler_t* handler,
                                                                        quicr_subscribe_track_handler_status_t* status);

    /**
     * @brief Pause delivery asynchronously; status_changed reports PAUSED.
     *
     * @param handler Subscribe handler.
     *
     * @return `OK` when pausing starts; `INVALID_ARGUMENT` for an invalid handler or state; `OUT_OF_MEMORY` if
     * allocation fails; otherwise `INTERNAL_ERROR`.
     */
    QUICR_C_API quicr_status_t quicr_subscribe_track_handler_pause(quicr_subscribe_track_handler_t* handler);

    /**
     * @brief Resume a paused subscription.
     *
     * @param handler Subscribe handler.
     *
     * @return `OK` when resumed; `INVALID_ARGUMENT` for an invalid handler or state; `OUT_OF_MEMORY` if allocation
     * fails; otherwise `INTERNAL_ERROR`.
     */
    QUICR_C_API quicr_status_t quicr_subscribe_track_handler_resume(quicr_subscribe_track_handler_t* handler);

    /**
     * @brief Request group_id from a peer that advertised support.
     *
     * @param handler Subscribe handler.
     * @param group_id Requested group identifier.
     *
     * @return `OK` when requested; `INVALID_ARGUMENT` for an invalid handler, state, or group; `OUT_OF_MEMORY` if
     * allocation fails; otherwise `INTERNAL_ERROR`.
     */
    QUICR_C_API quicr_status_t quicr_subscribe_track_handler_request_new_group(quicr_subscribe_track_handler_t* handler,
                                                                               uint64_t group_id);

    /**
     * @brief Advertise or withdraw support for peer new-group requests.
     *
     * @param handler Subscribe handler.
     * @param supported Whether new-group requests are supported.
     *
     * @return `OK` when the setting is applied; `INVALID_ARGUMENT` for an invalid handler or state; `OUT_OF_MEMORY` if
     * allocation fails; otherwise `INTERNAL_ERROR`.
     */
    QUICR_C_API quicr_status_t
    quicr_subscribe_track_handler_support_new_group_request(quicr_subscribe_track_handler_t* handler, bool supported);

    /**
     * @brief Send an update for a registered subscribe handler on session.
     *
     * @param session Borrowed session handle.
     * @param handler Registered subscribe handler.
     *
     * @return `OK` when the update is sent; `INVALID_ARGUMENT` for invalid pointers, registration, or state;
     * `NOT_FOUND` if the session is not live; `OUT_OF_MEMORY` if allocation fails; otherwise `INTERNAL_ERROR`.
     */
    QUICR_C_API quicr_status_t quicr_session_update_subscribe_track_handler(quicr_session_t* session,
                                                                            quicr_subscribe_track_handler_t* handler);

    /** @brief Opaque owned fetch handler with the common create/add/remove/destroy contract. */
    typedef struct quicr_fetch_track_handler quicr_fetch_track_handler_t;

    /** @brief Fetch status; uses all quicr_subscribe_track_handler_status_t values. */
    typedef quicr_subscribe_track_handler_status_t quicr_fetch_track_handler_status_t;

    /**
     * @brief Optional callbacks copied at fetch-handler creation.
     *
     * Field meanings and lifetime/concurrency contracts match quicr_subscribe_track_handler_callbacks_t, with fetch
     * handler pointers in status and metrics callbacks.
     */
    typedef struct quicr_fetch_track_handler_callbacks
    {
        void* user_data; ///< Opaque callback value.

        /**
         * @brief Lifecycle transition.
         *
         * @param handler Borrowed fetch handler receiving the transition.
         * @param status New handler status.
         * @param user_data Callback table's opaque user data.
         */
        void (*status_changed)(quicr_fetch_track_handler_t* handler,
                               quicr_fetch_track_handler_status_t status,
                               void* user_data);

        /**
         * @brief Borrowed metrics sample.
         *
         * @param handler Borrowed fetch handler associated with the sample.
         * @param metrics Borrowed metrics valid only for the callback.
         * @param user_data Callback table's opaque user data.
         */
        void (*metrics_sampled)(quicr_fetch_track_handler_t* handler,
                                const quicr_subscribe_track_metrics_t* metrics,
                                void* user_data);

        /**
         * @brief Borrowed headers, data, and properties for an available object.
         *
         * @param headers Borrowed object metadata valid only for the callback.
         * @param data Borrowed object payload valid only for the callback.
         * @param properties Optional borrowed stream-header properties valid only for the callback.
         * @param user_data Callback table's opaque user data.
         */
        void (*object_received)(const quicr_object_headers_t* headers,
                                quicr_byte_span_t data,
                                const quicr_stream_header_properties_t* properties,
                                void* user_data);

        /**
         * @brief Borrowed status-only object headers.
         *
         * @param headers Borrowed status-only object metadata valid only for the callback.
         * @param user_data Callback table's opaque user data.
         */
        void (*object_status_received)(const quicr_object_headers_t* headers, void* user_data);

        /**
         * @brief Subgroup completion; reset is true on reset.
         *
         * @param group_id Completed subgroup's group identifier.
         * @param subgroup_id Completed subgroup identifier.
         * @param reset True when the subgroup ended by reset.
         * @param user_data Callback table's opaque user data.
         */
        void (*subgroup_ended)(uint64_t group_id, uint64_t subgroup_id, bool reset, void* user_data);

        /**
         * @brief Borrowed raw datagram bytes.
         *
         * @param data Borrowed datagram bytes valid only for the callback.
         * @param user_data Callback table's opaque user data.
         */
        void (*datagram_received)(quicr_byte_span_t data, void* user_data);
    } quicr_fetch_track_handler_callbacks_t;

    /**
     * @brief Create a detached fetch handler, copying callback table, name, and range.
     *
     * @param callbacks Optional callback table.
     * @param ftn Full track name to copy.
     * @param priority Fetch priority.
     * @param start_location Required inclusive start.
     * @param end_location Required inclusive end.
     * @param group_order Required ascending or descending order.
     * @param handler Receives owned handler and is set NULL on failure.
     *
     * @return `OK` on success; `INVALID_ARGUMENT` for invalid required inputs, range, or order; `OUT_OF_MEMORY` if
     * allocation fails; otherwise `INTERNAL_ERROR`.
     */
    QUICR_C_API quicr_status_t quicr_fetch_track_handler_create(const quicr_fetch_track_handler_callbacks_t* callbacks,
                                                                const quicr_full_track_name_t* ftn,
                                                                uint8_t priority,
                                                                const quicr_location_t* start_location,
                                                                const quicr_fetch_end_location_t* end_location,
                                                                quicr_group_order_t group_order,
                                                                quicr_fetch_track_handler_t** handler);

    /**
     * @brief Destroy a detached fetch handler.
     *
     * @param handler Handler to destroy.
     *
     * @return `OK` when handler is NULL or destroyed; `BUSY` while registered, internally invalid, or in a callback.
     */
    QUICR_C_API quicr_status_t quicr_fetch_track_handler_destroy(quicr_fetch_track_handler_t* handler);

    /**
     * @brief Read handler status.
     *
     * @param handler Handler to inspect.
     * @param status Receives the current handler status.
     *
     * @return `OK` on success; `INVALID_ARGUMENT` when handler or status is NULL; otherwise `INTERNAL_ERROR`.
     */
    QUICR_C_API quicr_status_t quicr_fetch_track_handler_get_status(const quicr_fetch_track_handler_t* handler,
                                                                    quicr_fetch_track_handler_status_t* status);

    /**
     * @brief Pause fetch delivery asynchronously.
     *
     * @param handler Fetch handler.
     *
     * @return `OK` when pausing starts; `INVALID_ARGUMENT` for an invalid handler or state; `OUT_OF_MEMORY` if
     * allocation fails; otherwise `INTERNAL_ERROR`.
     */
    QUICR_C_API quicr_status_t quicr_fetch_track_handler_pause(quicr_fetch_track_handler_t* handler);

    /**
     * @brief Resume paused fetch delivery.
     *
     * @param handler Fetch handler.
     *
     * @return `OK` when resumed; `INVALID_ARGUMENT` for an invalid handler or state; `OUT_OF_MEMORY` if allocation
     * fails; otherwise `INTERNAL_ERROR`.
     */
    QUICR_C_API quicr_status_t quicr_fetch_track_handler_resume(quicr_fetch_track_handler_t* handler);

    /**
     * @brief Request group_id from a peer that advertised support.
     *
     * @param handler Fetch handler.
     * @param group_id Requested group identifier.
     *
     * @return `OK` when requested; `INVALID_ARGUMENT` for an invalid handler, state, or group; `OUT_OF_MEMORY` if
     * allocation fails; otherwise `INTERNAL_ERROR`.
     */
    QUICR_C_API quicr_status_t quicr_fetch_track_handler_request_new_group(quicr_fetch_track_handler_t* handler,
                                                                           uint64_t group_id);

    /**
     * @brief Advertise or withdraw support for peer new-group requests.
     *
     * @param handler Fetch handler.
     * @param supported Whether new-group requests are supported.
     *
     * @return `OK` when the setting is applied; `INVALID_ARGUMENT` for an invalid handler or state; `OUT_OF_MEMORY` if
     * allocation fails; otherwise `INTERNAL_ERROR`.
     */
    QUICR_C_API quicr_status_t quicr_fetch_track_handler_support_new_group_request(quicr_fetch_track_handler_t* handler,
                                                                                   bool supported);

    /** @brief Opaque owned namespace-publish handler with the common lifecycle contract. */
    typedef struct quicr_publish_namespace_handler quicr_publish_namespace_handler_t;

    /**
     * @name Namespace-publish handler statuses
     * @{
     */

    /** @brief Namespace-publish handler lifecycle status. */
    typedef uint8_t quicr_publish_namespace_handler_status_t;
    enum
    {
        QUICR_PUBLISH_NAMESPACE_HANDLER_STATUS_OK = 0,           ///< Namespace is published.
        QUICR_PUBLISH_NAMESPACE_HANDLER_STATUS_NOT_CONNECTED,    ///< Session disconnected.
        QUICR_PUBLISH_NAMESPACE_HANDLER_STATUS_NOT_PUBLISHED,    ///< Namespace is not published.
        QUICR_PUBLISH_NAMESPACE_HANDLER_STATUS_PENDING_RESPONSE, ///< Awaiting response.
        QUICR_PUBLISH_NAMESPACE_HANDLER_STATUS_NOT_AUTHORIZED,   ///< Publication unauthorized.
        QUICR_PUBLISH_NAMESPACE_HANDLER_STATUS_SENDING_DONE,     ///< Completion is being sent.
        QUICR_PUBLISH_NAMESPACE_HANDLER_STATUS_ERROR,            ///< Request failed.
    };
    /** @} */

    /**
     * @brief Optional callbacks copied at namespace-publish handler creation.
     *
     * The callback threading, concurrency, user_data, re-entry, borrowing, and destruction contracts match publish
     * callbacks.
     */
    typedef struct quicr_publish_namespace_handler_callbacks
    {
        void* user_data; ///< Kept valid until successful destruction.

        /**
         * @brief Lifecycle callback; see the handler callback threading contract.
         *
         * @param handler Borrowed namespace-publish handler receiving the transition.
         * @param status New handler status.
         * @param user_data Callback table's opaque user data.
         */
        void (*status_changed)(quicr_publish_namespace_handler_t* handler,
                               quicr_publish_namespace_handler_status_t status,
                               void* user_data);
    } quicr_publish_namespace_handler_callbacks_t;

    /**
     * @brief Create a detached namespace-publish handler.
     *
     * @param callbacks Optional callback table.
     * @param prefix Namespace prefix to copy.
     * @param handler Receives the owned handler and is set NULL on failure.
     *
     * @return `OK` on success; `INVALID_ARGUMENT` for invalid required inputs; `OUT_OF_MEMORY` if allocation fails;
     * otherwise `INTERNAL_ERROR`.
     */
    QUICR_C_API quicr_status_t
    quicr_publish_namespace_handler_create(const quicr_publish_namespace_handler_callbacks_t* callbacks,
                                           const quicr_track_namespace_t* prefix,
                                           quicr_publish_namespace_handler_t** handler);

    /**
     * @brief Destroy a detached namespace-publish handler.
     *
     * @param handler Handler to destroy.
     *
     * @return `OK` when handler is NULL or destroyed; `BUSY` while registered, internally invalid, or in a callback.
     */
    QUICR_C_API quicr_status_t quicr_publish_namespace_handler_destroy(quicr_publish_namespace_handler_t* handler);

    /**
     * @brief Read handler status.
     *
     * @param handler Handler to inspect.
     * @param status Receives the current handler status.
     *
     * @return `OK` on success; `INVALID_ARGUMENT` when handler or status is NULL; otherwise `INTERNAL_ERROR`.
     */
    QUICR_C_API quicr_status_t
    quicr_publish_namespace_handler_get_status(const quicr_publish_namespace_handler_t* handler,
                                               quicr_publish_namespace_handler_status_t* status);

    /**
     * @brief Copy the handler's latest protocol error.
     *
     * @param handler Handler whose latest error is copied.
     * @param code Required output error code.
     * @param reason Optional output buffer; may be NULL to query size.
     * @param reason_size Required only when reason is non-NULL; input capacity and output required size including NUL.
     *
     * @return `OK` when copied; `BUFFER_TOO_SMALL` when reason lacks capacity, with the required size reported;
     * `INVALID_ARGUMENT` for invalid pointers; `NOT_FOUND` when no protocol error is stored; otherwise
     * `INTERNAL_ERROR`.
     */
    QUICR_C_API quicr_status_t
    quicr_publish_namespace_handler_get_error(const quicr_publish_namespace_handler_t* handler,
                                              quicr_error_code_t* code,
                                              char* reason,
                                              size_t* reason_size);

    /**
     * @brief Associate a publish-track handler with this namespace handler; neither object is transferred.
     *
     * @param handler Namespace-publish handler.
     * @param track_handler Publish-track handler to associate.
     *
     * @return `OK` when associated; `INVALID_ARGUMENT` for invalid handlers or state; `OUT_OF_MEMORY` if allocation
     * fails; otherwise `INTERNAL_ERROR`.
     */
    QUICR_C_API quicr_status_t
    quicr_publish_namespace_handler_publish_track(quicr_publish_namespace_handler_t* handler,
                                                  const quicr_publish_track_handler_t* track_handler);

    /**
     * @brief Remove a prior publish-track association before destroying either handler.
     *
     * @param handler Namespace-publish handler.
     * @param track_handler Associated publish-track handler.
     *
     * @return `OK` when removed; `INVALID_ARGUMENT` for invalid handlers, association, or state; `OUT_OF_MEMORY` if
     * allocation fails; otherwise `INTERNAL_ERROR`.
     */
    QUICR_C_API quicr_status_t
    quicr_publish_namespace_handler_unpublish_track(quicr_publish_namespace_handler_t* handler,
                                                    const quicr_publish_track_handler_t* track_handler);

    /** @brief Opaque owned namespace-subscribe handler with the common lifecycle contract. */
    typedef struct quicr_subscribe_namespace_handler quicr_subscribe_namespace_handler_t;

    /**
     * @name Namespace subscription modes
     * @{
     */

    /** @brief Namespace subscription target mode. */
    typedef int32_t quicr_subscribe_namespace_mode_t;
    enum
    {
        QUICR_SUBSCRIBE_NAMESPACE_MODE_NAMESPACES = 0, ///< Receive matching namespace announcements.
        QUICR_SUBSCRIBE_NAMESPACE_MODE_TRACKS = 1,     ///< Receive matching tracks.
    };
    /** @} */

    /**
     * @name Namespace-subscribe handler statuses
     * @{
     */

    /** @brief Namespace-subscribe handler lifecycle status. */
    typedef uint8_t quicr_subscribe_namespace_handler_status_t;
    enum
    {
        QUICR_SUBSCRIBE_NAMESPACE_HANDLER_STATUS_OK = 0,         ///< Subscription is active.
        QUICR_SUBSCRIBE_NAMESPACE_HANDLER_STATUS_NOT_SUBSCRIBED, ///< Subscription is inactive.
        QUICR_SUBSCRIBE_NAMESPACE_HANDLER_STATUS_ERROR,          ///< Request failed.
    };
    /** @} */

    /**
     * @brief Optional callbacks copied at namespace-subscribe handler creation.
     *
     * The callback threading, concurrency, user_data, re-entry, and destruction contracts match publish callbacks.
     * Callback namespaces are borrowed only for the call.
     */
    typedef struct quicr_subscribe_namespace_handler_callbacks
    {
        void* user_data; ///< Opaque callback value.

        /**
         * @brief Lifecycle transition.
         *
         * @param handler Borrowed namespace-subscribe handler receiving the transition.
         * @param status New handler status.
         * @param user_data Callback table's opaque user data.
         */
        void (*status_changed)(quicr_subscribe_namespace_handler_t* handler,
                               quicr_subscribe_namespace_handler_status_t status,
                               void* user_data);

        /**
         * @brief Matching namespace became available.
         *
         * @param handler Borrowed namespace-subscribe handler receiving the namespace.
         * @param track_namespace Borrowed matching namespace valid only for the callback.
         * @param user_data Callback table's opaque user data.
         */
        void (*namespace_received)(quicr_subscribe_namespace_handler_t* handler,
                                   const quicr_track_namespace_t* track_namespace,
                                   void* user_data);

        /**
         * @brief Matching namespace ended.
         *
         * @param handler Borrowed namespace-subscribe handler receiving the notification.
         * @param track_namespace Borrowed ended namespace valid only for the callback.
         * @param user_data Callback table's opaque user data.
         */
        void (*namespace_done_received)(quicr_subscribe_namespace_handler_t* handler,
                                        const quicr_track_namespace_t* track_namespace,
                                        void* user_data);
    } quicr_subscribe_namespace_handler_callbacks_t;

    /**
     * @brief Create a detached namespace-subscribe handler.
     *
     * @param callbacks Optional callback table.
     * @param prefix Namespace prefix to copy.
     * @param mode Namespace subscription mode.
     * @param filter Optional filter; NULL selects no filter.
     * @param handler Receives the owned handler and is set NULL on failure.
     *
     * @return `OK` on success; `INVALID_ARGUMENT` for invalid required inputs, mode, or filter; `OUT_OF_MEMORY` if
     * allocation fails; otherwise `INTERNAL_ERROR`.
     */
    QUICR_C_API quicr_status_t
    quicr_subscribe_namespace_handler_create(const quicr_subscribe_namespace_handler_callbacks_t* callbacks,
                                             const quicr_track_namespace_t* prefix,
                                             quicr_subscribe_namespace_mode_t mode,
                                             const quicr_filter_t* filter,
                                             quicr_subscribe_namespace_handler_t** handler);

    /**
     * @brief Destroy a detached namespace-subscribe handler.
     *
     * @param handler Handler to destroy.
     *
     * @return `OK` when handler is NULL or destroyed; `BUSY` while registered, internally invalid, or in a callback.
     */
    QUICR_C_API quicr_status_t quicr_subscribe_namespace_handler_destroy(quicr_subscribe_namespace_handler_t* handler);

    /**
     * @brief Read handler status.
     *
     * @param handler Handler to inspect.
     * @param status Receives the current handler status.
     *
     * @return `OK` on success; `INVALID_ARGUMENT` when handler or status is NULL; otherwise `INTERNAL_ERROR`.
     */
    QUICR_C_API quicr_status_t
    quicr_subscribe_namespace_handler_get_status(const quicr_subscribe_namespace_handler_t* handler,
                                                 quicr_subscribe_namespace_handler_status_t* status);

    /**
     * @brief Copy the handler's latest protocol error.
     *
     * @param handler Handler whose latest error is copied.
     * @param code Required output error code.
     * @param reason Optional output buffer; may be NULL to query size.
     * @param reason_size Required only when reason is non-NULL; input capacity and output required size including NUL.
     *
     * @return `OK` when copied; `BUFFER_TOO_SMALL` when reason lacks capacity, with the required size reported;
     * `INVALID_ARGUMENT` for invalid pointers; `NOT_FOUND` when no protocol error is stored; otherwise
     * `INTERNAL_ERROR`.
     */
    QUICR_C_API quicr_status_t
    quicr_subscribe_namespace_handler_get_error(const quicr_subscribe_namespace_handler_t* handler,
                                                quicr_error_code_t* code,
                                                char* reason,
                                                size_t* reason_size);

    /** @brief Opaque owned track-status query handler with the common lifecycle contract. */
    typedef struct quicr_track_status_track_handler quicr_track_status_track_handler_t;

    /**
     * @name Track-status handler statuses
     * @{
     */

    /** @brief Track-status query lifecycle status. */
    typedef uint8_t quicr_track_status_handler_status_t;
    enum
    {
        QUICR_TRACK_STATUS_HANDLER_STATUS_NOT_REQUESTED = 0, ///< Query not sent.
        QUICR_TRACK_STATUS_HANDLER_STATUS_PENDING_RESPONSE,  ///< Awaiting response.
        QUICR_TRACK_STATUS_HANDLER_STATUS_OK,                ///< Response available.
        QUICR_TRACK_STATUS_HANDLER_STATUS_ERROR,             ///< Query failed.
        QUICR_TRACK_STATUS_HANDLER_STATUS_DONE_BY_FIN,       ///< Completed by FIN.
        QUICR_TRACK_STATUS_HANDLER_STATUS_DONE_BY_RESET,     ///< Completed by reset.
        QUICR_TRACK_STATUS_HANDLER_STATUS_NOT_CONNECTED,     ///< Session disconnected.
    };
    /** @} */

    /**
     * @brief Optional callbacks copied at track-status handler creation.
     *
     * The callback threading, concurrency, user_data, re-entry, borrowing, and destruction contracts match publish
     * callbacks.
     */
    typedef struct quicr_track_status_handler_callbacks
    {
        void* user_data; ///< Kept valid until successful destruction.

        /**
         * @brief Lifecycle callback; see the handler callback threading contract.
         *
         * @param handler Borrowed track-status handler receiving the transition.
         * @param status New handler status.
         * @param user_data Callback table's opaque user data.
         */
        void (*status_changed)(quicr_track_status_track_handler_t* handler,
                               quicr_track_status_handler_status_t status,
                               void* user_data);
    } quicr_track_status_handler_callbacks_t;

    /**
     * @brief Create a detached track-status handler.
     *
     * @param callbacks Optional callback table.
     * @param ftn Full track name to copy.
     * @param handler Receives the owned handler and is set NULL on failure.
     *
     * @return `OK` on success; `INVALID_ARGUMENT` for invalid required inputs; `OUT_OF_MEMORY` if allocation fails;
     * otherwise `INTERNAL_ERROR`.
     */
    QUICR_C_API quicr_status_t
    quicr_track_status_handler_create(const quicr_track_status_handler_callbacks_t* callbacks,
                                      const quicr_full_track_name_t* ftn,
                                      quicr_track_status_track_handler_t** handler);

    /**
     * @brief Destroy a detached track-status handler.
     *
     * @param handler Handler to destroy.
     *
     * @return `OK` when handler is NULL or destroyed; `BUSY` while registered, internally invalid, or in a callback.
     */
    QUICR_C_API quicr_status_t quicr_track_status_handler_destroy(quicr_track_status_track_handler_t* handler);

    /**
     * @brief Read handler status.
     *
     * @param handler Handler to inspect.
     * @param status Receives the current handler status.
     *
     * @return `OK` on success; `INVALID_ARGUMENT` when handler or status is NULL; otherwise `INTERNAL_ERROR`.
     */
    QUICR_C_API quicr_status_t quicr_track_status_handler_get_status(const quicr_track_status_track_handler_t* handler,
                                                                     quicr_track_status_handler_status_t* status);

    /**
     * @brief Copy the current response.
     *
     * @param handler Handler whose current response is copied.
     * @param has_response Receives whether a response exists.
     * @param response Receives response fields and is zeroed first.
     * @param track_extensions Optional caller-owned storage to receive copied extensions. When NULL, response's
     * track_extensions remains NULL.
     *
     * @return `OK` after reporting whether a response exists; `INVALID_ARGUMENT` for invalid pointers;
     * `OUT_OF_MEMORY` if copying extensions fails; otherwise `INTERNAL_ERROR`.
     */
    QUICR_C_API quicr_status_t
    quicr_track_status_handler_get_response(const quicr_track_status_track_handler_t* handler,
                                            bool* has_response,
                                            quicr_track_status_response_t* response,
                                            quicr_track_extensions_t* track_extensions);

    /**
     * @brief Copy the handler's latest protocol error.
     *
     * @param handler Handler whose latest error is copied.
     * @param code Required output error code.
     * @param reason Optional output buffer; may be NULL to query size.
     * @param reason_size Required only when reason is non-NULL; input capacity and output required size including NUL.
     *
     * @return `OK` when copied; `BUFFER_TOO_SMALL` when reason lacks capacity, with the required size reported;
     * `INVALID_ARGUMENT` for invalid pointers; `NOT_FOUND` when no protocol error is stored; otherwise
     * `INTERNAL_ERROR`.
     */
    QUICR_C_API quicr_status_t quicr_track_status_handler_get_error(const quicr_track_status_track_handler_t* handler,
                                                                    quicr_error_code_t* code,
                                                                    char* reason,
                                                                    size_t* reason_size);

#if !defined(__cplusplus) && defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201112L

/**
 * @brief Type-generic handler destruction for C11 and later C translation units.
 *
 * Supported mutable pointer types are publish, subscribe, fetch, publish-namespace, subscribe-namespace, and
 * track-status handlers. C++ and pre-C11 builds do not define this macro. Arguments are evaluated once.
 *
 * @param handler Handler to destroy.
 *
 * @return The selected handler-specific destroy status.
 */
#define quicr_track_handler_destroy(handler)                                                                           \
    _Generic((handler),                                                                                                \
      quicr_publish_track_handler_t*: quicr_publish_track_handler_destroy,                                             \
      quicr_subscribe_track_handler_t*: quicr_subscribe_track_handler_destroy,                                         \
      quicr_fetch_track_handler_t*: quicr_fetch_track_handler_destroy,                                                 \
      quicr_publish_namespace_handler_t*: quicr_publish_namespace_handler_destroy,                                     \
      quicr_subscribe_namespace_handler_t*: quicr_subscribe_namespace_handler_destroy,                                 \
      quicr_track_status_track_handler_t*: quicr_track_status_handler_destroy)(handler)

/**
 * @brief Type-generic status getter for C11 and later C translation units.
 *
 * Supports mutable and const pointers to every handler type supported by quicr_track_handler_destroy(). The status
 * pointer must have that handler's corresponding status type. Arguments are evaluated once.
 *
 * @param handler Handler to inspect.
 * @param status Receives the selected handler's current status.
 *
 * @return The selected handler-specific status-getter result.
 */
#define quicr_track_handler_get_status(handler, status)                                                                \
    _Generic((handler),                                                                                                \
      quicr_publish_track_handler_t*: quicr_publish_track_handler_get_status,                                          \
      const quicr_publish_track_handler_t*: quicr_publish_track_handler_get_status,                                    \
      quicr_subscribe_track_handler_t*: quicr_subscribe_track_handler_get_status,                                      \
      const quicr_subscribe_track_handler_t*: quicr_subscribe_track_handler_get_status,                                \
      quicr_fetch_track_handler_t*: quicr_fetch_track_handler_get_status,                                              \
      const quicr_fetch_track_handler_t*: quicr_fetch_track_handler_get_status,                                        \
      quicr_publish_namespace_handler_t*: quicr_publish_namespace_handler_get_status,                                  \
      const quicr_publish_namespace_handler_t*: quicr_publish_namespace_handler_get_status,                            \
      quicr_subscribe_namespace_handler_t*: quicr_subscribe_namespace_handler_get_status,                              \
      const quicr_subscribe_namespace_handler_t*: quicr_subscribe_namespace_handler_get_status,                        \
      quicr_track_status_track_handler_t*: quicr_track_status_handler_get_status,                                      \
      const quicr_track_status_track_handler_t*: quicr_track_status_handler_get_status)(handler, status)
#endif

    /*=======================================================================*/
    // Attributes
    /*=======================================================================*/

    /**
     * @brief Peer SETUP attributes, valid only during setup_received.
     */
    struct quicr_setup_attributes
    {
        quicr_byte_span_t endpoint_id; ///< Borrowed peer endpoint identifier.
    };

    /**
     * @brief Inbound namespace-publish attributes, callback-scoped.
     */
    struct quicr_publish_namespace_attributes
    {
        uint64_t request_id; ///< Peer request ID.
    };

    /**
     * @brief Inbound standalone-fetch attributes, callback-scoped.
     */
    struct quicr_standalone_fetch_attributes
    {
        uint8_t priority;                                  ///< Fetch priority.
        quicr_group_order_t group_order;                   ///< Requested order.
        quicr_group_order_t publisher_default_group_order; ///< Publisher's default order.
        quicr_location_t start_location;                   ///< Inclusive start.
        quicr_fetch_end_location_t end_location;           ///< Inclusive end.
    };

    /**
     * @brief Inbound joining-fetch attributes, callback-scoped.
     */
    struct quicr_joining_fetch_attributes
    {
        uint8_t priority;                                  ///< Fetch priority.
        quicr_group_order_t group_order;                   ///< Requested order.
        quicr_group_order_t publisher_default_group_order; ///< Publisher's default order.
        uint64_t joining_request_id;                       ///< Subscription/fetch request to join.
        bool relative;                                     ///< Whether joining_start is relative.
        uint64_t joining_start;                            ///< Relative offset or absolute start, per relative.
    };

    /**
     * @name Authorization token alias operations
     * @{
     */

    /** @brief Authorization-token alias operation. */
    typedef uint64_t quicr_token_alias_type_t;
    enum
    {
        QUICR_TOKEN_ALIAS_TYPE_DELETE = 0,    ///< Delete an alias.
        QUICR_TOKEN_ALIAS_TYPE_REGISTER = 1,  ///< Register value under an alias.
        QUICR_TOKEN_ALIAS_TYPE_USE_ALIAS = 2, ///< Refer to a registered alias.
        QUICR_TOKEN_ALIAS_TYPE_USE_VALUE = 3, ///< Carry a token value directly.
    };
    /** @} */

    /**
     * @brief Parsed authorization token.
     */
    typedef struct quicr_token
    {
        quicr_token_alias_type_t alias_type; ///< Alias operation.
        bool has_token_alias;                ///< Whether token_alias is present.
        uint64_t token_alias;                ///< Alias value when present.
        bool has_token_type;                 ///< Whether token_type is present.
        uint64_t token_type;                 ///< Token type when present.
        quicr_byte_span_t token_value;       ///< Borrowed callback-scoped token bytes.
    } quicr_token_t;

    /**
     * @brief Create an empty owned extension collection.
     *
     * @param extensions Receives a new owned object on success and is otherwise unchanged.
     *
     * @return `OK` on success; `INVALID_ARGUMENT` when extensions is NULL; `OUT_OF_MEMORY` if allocation fails;
     * otherwise `INTERNAL_ERROR`.
     */
    QUICR_C_API quicr_status_t quicr_track_extensions_create(quicr_track_extensions_t** extensions);

    /**
     * @brief Destroy an owned extension collection. NULL is allowed.
     *
     * @param extensions Owned extension collection to destroy, or NULL.
     */
    QUICR_C_API void quicr_track_extensions_destroy(quicr_track_extensions_t* extensions);

    /**
     * @brief Add or replace a mutable extension by copying value.
     *
     * @param extensions Extension collection to modify.
     * @param type Extension type.
     * @param value Extension value to copy.
     *
     * @return `OK` when copied; `INVALID_ARGUMENT` for an invalid collection or value; `OUT_OF_MEMORY` if allocation
     * fails; otherwise `INTERNAL_ERROR`.
     */
    QUICR_C_API quicr_status_t quicr_track_extensions_add(quicr_track_extensions_t* extensions,
                                                          uint64_t type,
                                                          quicr_byte_span_t value);

    /**
     * @brief Add or replace an immutable extension by copying value.
     *
     * @param extensions Extension collection to modify.
     * @param type Extension type.
     * @param value Extension value to copy.
     *
     * @return `OK` when copied; `INVALID_ARGUMENT` for an invalid collection or value; `OUT_OF_MEMORY` if allocation
     * fails; otherwise `INTERNAL_ERROR`.
     */
    QUICR_C_API quicr_status_t quicr_track_extensions_add_immutable(quicr_track_extensions_t* extensions,
                                                                    uint64_t type,
                                                                    quicr_byte_span_t value);

    /**
     * @brief Read the mutable-extension count.
     *
     * @param extensions Extension collection to inspect, or NULL.
     *
     * @return The mutable-extension count, or zero for NULL.
     */
    QUICR_C_API size_t quicr_track_extensions_count(const quicr_track_extensions_t* extensions);

    /**
     * @brief Read the immutable-extension count.
     *
     * @param extensions Extension collection to inspect, or NULL.
     *
     * @return The immutable-extension count, or zero for NULL.
     */
    QUICR_C_API size_t quicr_track_extensions_immutable_count(const quicr_track_extensions_t* extensions);

    /**
     * @brief Borrow a mutable extension entry.
     *
     * @param extensions Extension collection to inspect.
     * @param index Zero-based mutable-extension index.
     * @param type Receives the extension type.
     * @param value Receives a span valid until mutation or destruction.
     *
     * @return `OK` on success; `INVALID_ARGUMENT` for NULL or an out-of-range index; otherwise `INTERNAL_ERROR`.
     */
    QUICR_C_API quicr_status_t quicr_track_extensions_entry(const quicr_track_extensions_t* extensions,
                                                            size_t index,
                                                            uint64_t* type,
                                                            quicr_byte_span_t* value);

    /**
     * @brief Borrow an immutable extension entry.
     *
     * @param extensions Extension collection to inspect.
     * @param index Zero-based immutable-extension index.
     * @param type Receives the extension type.
     * @param value Receives a span valid until mutation or destruction.
     *
     * @return `OK` on success; `INVALID_ARGUMENT` for NULL or an out-of-range index; otherwise `INTERNAL_ERROR`.
     */
    QUICR_C_API quicr_status_t quicr_track_extensions_immutable_entry(const quicr_track_extensions_t* extensions,
                                                                      size_t index,
                                                                      uint64_t* type,
                                                                      quicr_byte_span_t* value);

    /**
     * @brief Borrow the full track name for the attributes callback duration.
     *
     * @param attributes Publish attributes to inspect, or NULL.
     *
     * @return The borrowed full track name, or NULL when attributes is NULL.
     */
    QUICR_C_API const quicr_full_track_name_t* quicr_publish_attributes_full_track_name(
      const quicr_publish_attributes_t* attributes);

    /**
     * @brief Read the track alias.
     *
     * @param attributes Publish attributes to inspect, or NULL.
     *
     * @return The track alias, or zero when attributes is NULL.
     */
    QUICR_C_API uint64_t quicr_publish_attributes_track_alias(const quicr_publish_attributes_t* attributes);

    /**
     * @brief Read the authorization-token count.
     *
     * @param attributes Publish attributes to inspect, or NULL.
     *
     * @return The token count, or zero when attributes is NULL.
     */
    QUICR_C_API size_t quicr_publish_attributes_auth_token_count(const quicr_publish_attributes_t* attributes);

    /**
     * @brief Read an authorization token; token_value is borrowed for the attributes callback duration.
     *
     * @param attributes Publish attributes to inspect.
     * @param index Zero-based token index.
     * @param token Receives the token.
     *
     * @return `OK` on success; `INVALID_ARGUMENT` for NULL or an out-of-range index; otherwise `INTERNAL_ERROR`.
     */
    QUICR_C_API quicr_status_t quicr_publish_attributes_auth_token(const quicr_publish_attributes_t* attributes,
                                                                   size_t index,
                                                                   quicr_token_t* token);

    /**
     * @brief Read optional publication expiration in protocol-defined milliseconds.
     *
     * @param attributes Publish attributes to inspect.
     * @param has_expires Receives whether expiration is present.
     * @param expires Receives expiration, or zero when absent.
     *
     * @return `OK` on success; `INVALID_ARGUMENT` for NULL; otherwise `INTERNAL_ERROR`.
     */
    QUICR_C_API quicr_status_t quicr_publish_attributes_expires(const quicr_publish_attributes_t* attributes,
                                                                bool* has_expires,
                                                                uint64_t* expires);

    /**
     * @brief Read the optional largest object.
     *
     * @param attributes Publish attributes to inspect.
     * @param has_largest_object Receives whether a location is present.
     * @param largest_object Receives the location, or a zero location when absent.
     *
     * @return `OK` on success; `INVALID_ARGUMENT` for NULL; otherwise `INTERNAL_ERROR`.
     */
    QUICR_C_API quicr_status_t quicr_publish_attributes_largest_object(const quicr_publish_attributes_t* attributes,
                                                                       bool* has_largest_object,
                                                                       quicr_location_t* largest_object);

    /**
     * @brief Read the forward flag.
     *
     * @param attributes Publish attributes to inspect, or NULL.
     *
     * @return The forward flag, or false when attributes is NULL.
     */
    QUICR_C_API bool quicr_publish_attributes_forward(const quicr_publish_attributes_t* attributes);

    /**
     * @brief Read the publisher default group order.
     *
     * @param attributes Publish attributes to inspect.
     * @param group_order Receives the default group order.
     *
     * @return `OK` on success; `INVALID_ARGUMENT` for NULL; otherwise `INTERNAL_ERROR`.
     */
    QUICR_C_API quicr_status_t quicr_publish_attributes_group_order(const quicr_publish_attributes_t* attributes,
                                                                    quicr_group_order_t* group_order);

    /**
     * @brief Read the dynamic-groups flag.
     *
     * @param attributes Publish attributes to inspect, or NULL.
     *
     * @return The dynamic-groups flag, or false when attributes is NULL.
     */
    QUICR_C_API bool quicr_publish_attributes_dynamic_groups(const quicr_publish_attributes_t* attributes);

    /**
     * @brief Read the publisher priority.
     *
     * @param attributes Publish attributes to inspect, or NULL.
     *
     * @return The publisher priority, or zero when attributes is NULL.
     */
    QUICR_C_API uint8_t quicr_publish_attributes_publisher_priority(const quicr_publish_attributes_t* attributes);

    /**
     * @brief Borrow track extensions for the callback duration.
     *
     * @param attributes Publish attributes to inspect, or NULL.
     *
     * @return The borrowed track extensions, or NULL when attributes is NULL.
     */
    QUICR_C_API const quicr_track_extensions_t* quicr_publish_attributes_track_extensions(
      const quicr_publish_attributes_t* attributes);

    /**
     * @brief Read the optional maximum cache duration in milliseconds.
     *
     * @param attributes Publish attributes to inspect.
     * @param has_max_cache_duration Receives whether the duration is present.
     * @param max_cache_duration Receives the duration, or zero when absent.
     *
     * @return `OK` on success; `INVALID_ARGUMENT` for NULL; otherwise `INTERNAL_ERROR`.
     */
    QUICR_C_API quicr_status_t quicr_publish_attributes_max_cache_duration(const quicr_publish_attributes_t* attributes,
                                                                           bool* has_max_cache_duration,
                                                                           uint64_t* max_cache_duration);

    /**
     * @brief Read the optional delivery timeout in milliseconds.
     *
     * @param attributes Publish attributes to inspect.
     * @param has_delivery_timeout Receives whether the timeout is present.
     * @param delivery_timeout Receives the timeout, or zero when absent.
     *
     * @return `OK` on success; `INVALID_ARGUMENT` for NULL; otherwise `INTERNAL_ERROR`.
     */
    QUICR_C_API quicr_status_t quicr_publish_attributes_delivery_timeout(const quicr_publish_attributes_t* attributes,
                                                                         bool* has_delivery_timeout,
                                                                         uint64_t* delivery_timeout);

    /**
     * @brief Read the subscriber priority.
     *
     * @param attributes Subscribe attributes to inspect, or NULL.
     *
     * @return The subscriber priority, or zero when attributes is NULL.
     */
    QUICR_C_API uint8_t quicr_subscribe_attributes_priority(const quicr_subscribe_attributes_t* attributes);

    /**
     * @brief Read the optional group order.
     *
     * @param attributes Subscribe attributes to inspect.
     * @param has_group_order Receives whether a group order is present.
     * @param group_order Receives the group order, or ASCENDING when absent.
     *
     * @return `OK` on success; `INVALID_ARGUMENT` for NULL; otherwise `INTERNAL_ERROR`.
     */
    QUICR_C_API quicr_status_t quicr_subscribe_attributes_group_order(const quicr_subscribe_attributes_t* attributes,
                                                                      bool* has_group_order,
                                                                      quicr_group_order_t* group_order);

    /**
     * @brief Read the publisher default group order.
     *
     * @param attributes Subscribe attributes to inspect.
     * @param group_order Receives the publisher default group order.
     *
     * @return `OK` on success; `INVALID_ARGUMENT` for NULL; otherwise `INTERNAL_ERROR`.
     */
    QUICR_C_API quicr_status_t
    quicr_subscribe_attributes_publisher_group_order(const quicr_subscribe_attributes_t* attributes,
                                                     quicr_group_order_t* group_order);

    /**
     * @brief Read the delivery timeout in milliseconds.
     *
     * @param attributes Subscribe attributes to inspect, or NULL.
     *
     * @return The delivery timeout, or zero when attributes is NULL.
     */
    QUICR_C_API uint64_t quicr_subscribe_attributes_delivery_timeout(const quicr_subscribe_attributes_t* attributes);

    /**
     * @brief Read the expiration interval in milliseconds.
     *
     * @param attributes Subscribe attributes to inspect, or NULL.
     *
     * @return The expiration interval, or zero when attributes is NULL.
     */
    QUICR_C_API uint64_t quicr_subscribe_attributes_expires(const quicr_subscribe_attributes_t* attributes);

    /**
     * @brief Read the forward flag.
     *
     * @param attributes Subscribe attributes to inspect, or NULL.
     *
     * @return The forward flag, or false when attributes is NULL.
     */
    QUICR_C_API bool quicr_subscribe_attributes_forward(const quicr_subscribe_attributes_t* attributes);

    /**
     * @brief Read the optional new-group request ID.
     *
     * @param attributes Subscribe attributes to inspect.
     * @param has_new_group_request_id Receives whether an ID is present.
     * @param new_group_request_id Receives the ID, or zero when absent.
     *
     * @return `OK` on success; `INVALID_ARGUMENT` for NULL; otherwise `INTERNAL_ERROR`.
     */
    QUICR_C_API quicr_status_t
    quicr_subscribe_attributes_new_group_request_id(const quicr_subscribe_attributes_t* attributes,
                                                    bool* has_new_group_request_id,
                                                    uint64_t* new_group_request_id);

    /**
     * @brief Determine whether the publisher initiated this subscription.
     *
     * @param attributes Subscribe attributes to inspect, or NULL.
     *
     * @return Whether the publisher initiated the subscription, or false when attributes is NULL.
     */
    QUICR_C_API bool quicr_subscribe_attributes_is_publisher_initiated(const quicr_subscribe_attributes_t* attributes);

    /**
     * @brief Read the subscription start location.
     *
     * @param attributes Subscribe attributes to inspect, or NULL.
     *
     * @return The start location, or a zero location when attributes is NULL.
     */
    QUICR_C_API quicr_location_t
    quicr_subscribe_attributes_start_location(const quicr_subscribe_attributes_t* attributes);

    /**
     * @brief Read the rendezvous timeout in milliseconds.
     *
     * @param attributes Subscribe attributes to inspect, or NULL.
     *
     * @return The rendezvous timeout, or zero when attributes is NULL.
     */
    QUICR_C_API uint64_t quicr_subscribe_attributes_rendezvous_timeout(const quicr_subscribe_attributes_t* attributes);

    /**
     * @brief Read the namespace request ID.
     *
     * @param attributes Namespace-subscribe attributes to inspect, or NULL.
     *
     * @return The request ID, or zero when attributes is NULL.
     */
    QUICR_C_API uint64_t
    quicr_subscribe_namespace_attributes_request_id(const quicr_subscribe_namespace_attributes_t* attributes);

    /*=======================================================================*/
    // Responses
    /*=======================================================================*/

    /**
     * @brief Create a publish-acceptance response retaining handler's implementation.
     *
     * handler must remain a valid C handle through this call, but may subsequently be destroyed once otherwise
     * eligible. Return the response from publish_received, then destroy the response after that callback returns.
     *
     * @param handler Subscribe handler whose implementation the response retains.
     * @param response Receives the owned response and is set NULL on failure.
     *
     * @return `OK` on success; `INVALID_ARGUMENT` if handler, its implementation, or response is invalid;
     * `OUT_OF_MEMORY` if allocation fails; otherwise `INTERNAL_ERROR`.
     */
    QUICR_C_API quicr_status_t quicr_publish_response_create(quicr_subscribe_track_handler_t* handler,
                                                             quicr_publish_response_t** response);

    /**
     * @brief Destroy an owned publish response. NULL is allowed.
     *
     * @param response Owned response to destroy, or NULL.
     */
    QUICR_C_API void quicr_publish_response_destroy(quicr_publish_response_t* response);

    /**
     * @brief Set subscriber priority.
     *
     * @param response Publish response to modify.
     * @param priority Subscriber priority.
     *
     * @return `OK` on success or `INVALID_ARGUMENT` when response is NULL.
     */
    QUICR_C_API quicr_status_t quicr_publish_response_set_subscriber_priority(quicr_publish_response_t* response,
                                                                              uint8_t priority);

    /**
     * @brief Set group order; UNSPECIFIED clears the optional preference.
     *
     * @param response Publish response to modify.
     * @param group_order Group order or UNSPECIFIED.
     *
     * @return `OK` on success; `INVALID_ARGUMENT` when response or group_order is invalid; otherwise `INTERNAL_ERROR`.
     */
    QUICR_C_API quicr_status_t quicr_publish_response_set_group_order(quicr_publish_response_t* response,
                                                                      quicr_group_order_t group_order);

    /**
     * @brief Set a filter by copying it.
     *
     * @param response Publish response to modify.
     * @param filter Required filter to copy.
     *
     * @return `OK` on success; `INVALID_ARGUMENT` when response or filter is invalid; otherwise `INTERNAL_ERROR`.
     */
    QUICR_C_API quicr_status_t quicr_publish_response_set_filter(quicr_publish_response_t* response,
                                                                 const quicr_filter_t* filter);

    /**
     * @brief Set whether received objects should be forwarded.
     *
     * @param response Publish response to modify.
     * @param forward Whether to forward received objects.
     *
     * @return `OK` on success or `INVALID_ARGUMENT` when response is NULL.
     */
    QUICR_C_API quicr_status_t quicr_publish_response_set_forward(quicr_publish_response_t* response, bool forward);

    /**
     * @brief Set subgroup delivery timeout in milliseconds.
     *
     * @param response Publish response to modify.
     * @param timeout_ms Subgroup delivery timeout.
     *
     * @return `OK` on success or `INVALID_ARGUMENT` when response is NULL or timeout_ms is out of range.
     */
    QUICR_C_API quicr_status_t quicr_publish_response_set_subgroup_delivery_timeout(quicr_publish_response_t* response,
                                                                                    uint64_t timeout_ms);

    /**
     * @brief Set object delivery timeout in milliseconds.
     *
     * @param response Publish response to modify.
     * @param timeout_ms Object delivery timeout.
     *
     * @return `OK` on success or `INVALID_ARGUMENT` when response is NULL or timeout_ms is out of range.
     */
    QUICR_C_API quicr_status_t quicr_publish_response_set_object_delivery_timeout(quicr_publish_response_t* response,
                                                                                  uint64_t timeout_ms);

    /**
     * @brief Set the new-group request ID.
     *
     * @param response Publish response to modify.
     * @param request_id New-group request identifier.
     *
     * @return `OK` on success or `INVALID_ARGUMENT` when response is NULL.
     */
    QUICR_C_API quicr_status_t quicr_publish_response_set_new_group_request_id(quicr_publish_response_t* response,
                                                                               uint64_t request_id);

    /*=======================================================================*/
    // Session Manager
    /*=======================================================================*/

    /**
     * @brief Opaque owner of transports, sessions, session handles, and handler registrations.
     *
     * Destroying the manager shuts down transports and invalidates all session handles. Before destruction, remove
     * every registered handler; after removal, destroy handlers, then destroy the manager. Manager destruction does
     * not destroy application-owned handler C objects.
     */
    typedef struct quicr_session_manager quicr_session_manager_t;

    /**
     * @brief Optional callbacks copied at manager creation.
     *
     * Callbacks may run synchronously on an API caller thread, including while session locks are held, or on a
     * transport notification thread. They may be concurrent across objects or transports. Return promptly. Do not
     * re-enter manager, session, or handler operations on the same objects unless a specific API explicitly permits
     * it. Never destroy a callback-owning object from its callback. Keep user_data valid until manager destruction
     * returns.
     */
    typedef struct quicr_session_manager_callbacks
    {
        void* user_data; ///< Opaque callback value.

        /**
         * @brief Reports a manager-owned server session handle.
         *
         * @param manager Borrowed session manager.
         * @param session Borrowed manager-owned server session handle.
         * @param user_data Callback table's opaque user data.
         */
        void (*new_server_session)(quicr_session_manager_t* manager, quicr_session_t* session, void* user_data);

        /**
         * @brief Final notification for a handle; the handle is valid only during this callback and is invalid on
         * return.
         *
         * @param manager Borrowed session manager.
         * @param session Borrowed session handle valid only for the callback.
         * @param user_data Callback table's opaque user data.
         */
        void (*session_removed)(quicr_session_manager_t* manager, quicr_session_t* session, void* user_data);
    } quicr_session_manager_callbacks_t;

    /**
     * @brief Create a manager, copying optional callbacks.
     *
     * @param callbacks Optional callback table.
     * @param manager Receives the owned manager and is set NULL on failure.
     *
     * @return `OK` on success; `INVALID_ARGUMENT` when manager is NULL; `OUT_OF_MEMORY` if allocation fails;
     * otherwise `INTERNAL_ERROR`.
     */
    QUICR_C_API quicr_status_t quicr_session_manager_create(const quicr_session_manager_callbacks_t* callbacks,
                                                            quicr_session_manager_t** manager);

    /**
     * @brief Shut down transports and destroy a manager. NULL is allowed; no callbacks may use it afterward.
     *
     * @param manager Owned manager to destroy, or NULL.
     */
    QUICR_C_API void quicr_session_manager_destroy(quicr_session_manager_t* manager);

    /**
     * @brief Add a client transport and wait for a session.
     *
     * Config strings are copied/consumed during creation. callbacks are copied. On success, session receives a
     * borrowed manager-owned handle valid until session_removed or manager destruction.
     *
     * @param manager Session manager.
     * @param config Client configuration to copy.
     * @param callbacks Optional callback table to copy.
     * @param session Receives a borrowed manager-owned session handle.
     *
     * @return `OK` on success; `INVALID_ARGUMENT` for invalid pointers or configuration; `OUT_OF_MEMORY` if
     * allocation fails; otherwise `INTERNAL_ERROR`.
     */
    QUICR_C_API quicr_status_t quicr_session_manager_add_client(quicr_session_manager_t* manager,
                                                                const quicr_client_config_t* config,
                                                                const quicr_session_client_callbacks_t* callbacks,
                                                                quicr_session_t** session);

    /**
     * @brief Start a server transport.
     *
     * Config and callback table are copied during the call. Accepted sessions arrive asynchronously through
     * new_server_session.
     *
     * @param manager Session manager.
     * @param config Server configuration to copy.
     * @param callbacks Optional callback table to copy.
     *
     * @return `OK` on success; `INVALID_ARGUMENT` for invalid pointers or configuration; `OUT_OF_MEMORY` if
     * allocation fails; otherwise `INTERNAL_ERROR`.
     */
    QUICR_C_API quicr_status_t quicr_session_manager_add_server(quicr_session_manager_t* manager,
                                                                const quicr_server_config_t* config,
                                                                const quicr_session_server_callbacks_t* callbacks);

    /**
     * @brief Register a detached publish handler with session; ownership remains with the caller.
     *
     * @param manager Session manager.
     * @param session Borrowed target session handle.
     * @param handler Detached publish handler to register.
     *
     * @return `OK` on success; `INVALID_ARGUMENT` for a null or expired object, wrong registration, or invalid state;
     * `OUT_OF_MEMORY` if allocation fails; or `INTERNAL_ERROR` for another exception.
     */
    QUICR_C_API quicr_status_t
    quicr_session_manager_add_publish_track_handler(quicr_session_manager_t* manager,
                                                    quicr_session_t* session,
                                                    const quicr_publish_track_handler_t* handler);

    /**
     * @brief Unregister a publish handler; after return, destroy it, retrying if destruction reports BUSY.
     *
     * @param manager Session manager.
     * @param session Borrowed session handle.
     * @param handler Registered publish handler to remove.
     *
     * @return `OK` on success; `INVALID_ARGUMENT` for a null or expired object, wrong registration, or invalid state;
     * `OUT_OF_MEMORY` if allocation fails; or `INTERNAL_ERROR` for another exception.
     */
    QUICR_C_API quicr_status_t
    quicr_session_manager_remove_publish_track_handler(quicr_session_manager_t* manager,
                                                       quicr_session_t* session,
                                                       const quicr_publish_track_handler_t* handler);

    /**
     * @brief Register a detached subscribe handler with session; ownership remains with the caller.
     *
     * @param manager Session manager.
     * @param session Borrowed target session handle.
     * @param handler Detached subscribe handler to register.
     *
     * @return `OK` on success; `INVALID_ARGUMENT` for a null or expired object, wrong registration, or invalid state;
     * `OUT_OF_MEMORY` if allocation fails; or `INTERNAL_ERROR` for another exception.
     */
    QUICR_C_API quicr_status_t
    quicr_session_manager_add_subscribe_track_handler(quicr_session_manager_t* manager,
                                                      quicr_session_t* session,
                                                      const quicr_subscribe_track_handler_t* handler);

    /**
     * @brief Unregister a subscribe handler before destroying it.
     *
     * @param manager Session manager.
     * @param session Borrowed session handle.
     * @param handler Registered subscribe handler to remove.
     *
     * @return `OK` on success; `INVALID_ARGUMENT` for a null or expired object, wrong registration, or invalid state;
     * `OUT_OF_MEMORY` if allocation fails; or `INTERNAL_ERROR` for another exception.
     */
    QUICR_C_API quicr_status_t
    quicr_session_manager_remove_subscribe_track_handler(quicr_session_manager_t* manager,
                                                         quicr_session_t* session,
                                                         const quicr_subscribe_track_handler_t* handler);

    /**
     * @brief Register a detached fetch handler with session; ownership remains with the caller.
     *
     * @param manager Session manager.
     * @param session Borrowed target session handle.
     * @param handler Detached fetch handler to register.
     *
     * @return `OK` on success; `INVALID_ARGUMENT` for a null or expired object, wrong registration, or invalid state;
     * `OUT_OF_MEMORY` if allocation fails; or `INTERNAL_ERROR` for another exception.
     */
    QUICR_C_API quicr_status_t
    quicr_session_manager_add_fetch_track_handler(quicr_session_manager_t* manager,
                                                  quicr_session_t* session,
                                                  const quicr_fetch_track_handler_t* handler);

    /**
     * @brief Unregister a fetch handler before destroying it.
     *
     * @param manager Session manager.
     * @param session Borrowed session handle.
     * @param handler Registered fetch handler to remove.
     *
     * @return `OK` on success; `INVALID_ARGUMENT` for a null or expired object, wrong registration, or invalid state;
     * `OUT_OF_MEMORY` if allocation fails; or `INTERNAL_ERROR` for another exception.
     */
    QUICR_C_API quicr_status_t
    quicr_session_manager_remove_fetch_track_handler(quicr_session_manager_t* manager,
                                                     quicr_session_t* session,
                                                     const quicr_fetch_track_handler_t* handler);

    /**
     * @brief Register a detached namespace-publish handler with session.
     *
     * @param manager Session manager.
     * @param session Borrowed target session handle.
     * @param handler Detached namespace-publish handler to register.
     *
     * @return `OK` on success; `INVALID_ARGUMENT` for a null or expired object, wrong registration, or invalid state;
     * `OUT_OF_MEMORY` if allocation fails; or `INTERNAL_ERROR` for another exception.
     */
    QUICR_C_API quicr_status_t
    quicr_session_manager_add_publish_namespace_handler(quicr_session_manager_t* manager,
                                                        quicr_session_t* session,
                                                        const quicr_publish_namespace_handler_t* handler);

    /**
     * @brief Unregister a namespace-publish handler before destroying it.
     *
     * @param manager Session manager.
     * @param session Borrowed session handle.
     * @param handler Registered namespace-publish handler to remove.
     *
     * @return `OK` on success; `INVALID_ARGUMENT` for a null or expired object, wrong registration, or invalid state;
     * `OUT_OF_MEMORY` if allocation fails; or `INTERNAL_ERROR` for another exception.
     */
    QUICR_C_API quicr_status_t
    quicr_session_manager_remove_publish_namespace_handler(quicr_session_manager_t* manager,
                                                           quicr_session_t* session,
                                                           const quicr_publish_namespace_handler_t* handler);

    /**
     * @brief Register a detached namespace-subscribe handler with session.
     *
     * @param manager Session manager.
     * @param session Borrowed target session handle.
     * @param handler Detached namespace-subscribe handler to register.
     *
     * @return `OK` on success; `INVALID_ARGUMENT` for a null or expired object, wrong registration, or invalid state;
     * `OUT_OF_MEMORY` if allocation fails; or `INTERNAL_ERROR` for another exception.
     */
    QUICR_C_API quicr_status_t
    quicr_session_manager_add_subscribe_namespace_handler(quicr_session_manager_t* manager,
                                                          quicr_session_t* session,
                                                          const quicr_subscribe_namespace_handler_t* handler);

    /**
     * @brief Unregister a namespace-subscribe handler before destroying it.
     *
     * @param manager Session manager.
     * @param session Borrowed session handle.
     * @param handler Registered namespace-subscribe handler to remove.
     *
     * @return `OK` on success; `INVALID_ARGUMENT` for a null or expired object, wrong registration, or invalid state;
     * `OUT_OF_MEMORY` if allocation fails; or `INTERNAL_ERROR` for another exception.
     */
    QUICR_C_API quicr_status_t
    quicr_session_manager_remove_subscribe_namespace_handler(quicr_session_manager_t* manager,
                                                             quicr_session_t* session,
                                                             const quicr_subscribe_namespace_handler_t* handler);

    /**
     * @brief Register a detached track-status handler with session.
     *
     * @param manager Session manager.
     * @param session Borrowed target session handle.
     * @param handler Detached track-status handler to register.
     *
     * @return `OK` on success; `INVALID_ARGUMENT` for a null or expired object, wrong registration, or invalid state;
     * `OUT_OF_MEMORY` if allocation fails; or `INTERNAL_ERROR` for another exception.
     */
    QUICR_C_API quicr_status_t
    quicr_session_manager_add_track_status_track_handler(quicr_session_manager_t* manager,
                                                         quicr_session_t* session,
                                                         const quicr_track_status_track_handler_t* handler);

    /**
     * @brief Unregister a track-status handler before destroying it.
     *
     * @param manager Session manager.
     * @param session Borrowed session handle.
     * @param handler Registered track-status handler to remove.
     *
     * @return `OK` on success; `INVALID_ARGUMENT` for a null or expired object, wrong registration, or invalid state;
     * `OUT_OF_MEMORY` if allocation fails; or `INTERNAL_ERROR` for another exception.
     */
    QUICR_C_API quicr_status_t
    quicr_session_manager_remove_track_status_track_handler(quicr_session_manager_t* manager,
                                                            quicr_session_t* session,
                                                            const quicr_track_status_track_handler_t* handler);

#if !defined(__cplusplus) && defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201112L

/**
 * @brief Type-generic handler registration for C11 and later C translation units.
 *
 * Supports mutable and const pointers to publish, subscribe, fetch, publish-namespace, subscribe-namespace, and
 * track-status handlers. C++ and pre-C11 builds do not define it. Arguments are evaluated once.
 *
 * @param manager Session manager.
 * @param session Borrowed target session handle.
 * @param handler Detached handler to register.
 *
 * @return The selected handler-specific registration status.
 */
#define quicr_session_manager_add_handler(manager, session, handler)                                                   \
    _Generic((handler),                                                                                                \
      quicr_publish_track_handler_t*: quicr_session_manager_add_publish_track_handler,                                 \
      const quicr_publish_track_handler_t*: quicr_session_manager_add_publish_track_handler,                           \
      quicr_subscribe_track_handler_t*: quicr_session_manager_add_subscribe_track_handler,                             \
      const quicr_subscribe_track_handler_t*: quicr_session_manager_add_subscribe_track_handler,                       \
      quicr_fetch_track_handler_t*: quicr_session_manager_add_fetch_track_handler,                                     \
      const quicr_fetch_track_handler_t*: quicr_session_manager_add_fetch_track_handler,                               \
      quicr_publish_namespace_handler_t*: quicr_session_manager_add_publish_namespace_handler,                         \
      const quicr_publish_namespace_handler_t*: quicr_session_manager_add_publish_namespace_handler,                   \
      quicr_subscribe_namespace_handler_t*: quicr_session_manager_add_subscribe_namespace_handler,                     \
      const quicr_subscribe_namespace_handler_t*: quicr_session_manager_add_subscribe_namespace_handler,               \
      quicr_track_status_track_handler_t*: quicr_session_manager_add_track_status_track_handler,                       \
      const quicr_track_status_track_handler_t*: quicr_session_manager_add_track_status_track_handler)(                \
      manager, session, handler)

/**
 * @brief Type-generic handler removal for C11 and later C translation units.
 *
 * Supports the same mutable and const handler pointer types as quicr_session_manager_add_handler(). Arguments are
 * evaluated once.
 *
 * @param manager Session manager.
 * @param session Borrowed session handle.
 * @param handler Registered handler to remove.
 *
 * @return The selected handler-specific removal status.
 */
#define quicr_session_manager_remove_handler(manager, session, handler)                                                \
    _Generic((handler),                                                                                                \
      quicr_publish_track_handler_t*: quicr_session_manager_remove_publish_track_handler,                              \
      const quicr_publish_track_handler_t*: quicr_session_manager_remove_publish_track_handler,                        \
      quicr_subscribe_track_handler_t*: quicr_session_manager_remove_subscribe_track_handler,                          \
      const quicr_subscribe_track_handler_t*: quicr_session_manager_remove_subscribe_track_handler,                    \
      quicr_fetch_track_handler_t*: quicr_session_manager_remove_fetch_track_handler,                                  \
      const quicr_fetch_track_handler_t*: quicr_session_manager_remove_fetch_track_handler,                            \
      quicr_publish_namespace_handler_t*: quicr_session_manager_remove_publish_namespace_handler,                      \
      const quicr_publish_namespace_handler_t*: quicr_session_manager_remove_publish_namespace_handler,                \
      quicr_subscribe_namespace_handler_t*: quicr_session_manager_remove_subscribe_namespace_handler,                  \
      const quicr_subscribe_namespace_handler_t*: quicr_session_manager_remove_subscribe_namespace_handler,            \
      quicr_track_status_track_handler_t*: quicr_session_manager_remove_track_status_track_handler,                    \
      const quicr_track_status_track_handler_t*: quicr_session_manager_remove_track_status_track_handler)(             \
      manager, session, handler)
#endif

    /**
     * @brief Send GOAWAY with the first-unprocessed cutoff derived from a publish handler.
     *
     * new_session_uri is optional and copied; timeout_ms is milliseconds. The handler remains caller-owned.
     *
     * @param session Borrowed session handle.
     * @param new_session_uri Optional URI copied during the call; NULL means an empty URI.
     * @param timeout_ms Drain timeout in milliseconds.
     * @param handler Publish handler that determines the request cutoff.
     *
     * @return `OK` on success; `INVALID_ARGUMENT` for invalid pointers, registration, or state; `NOT_FOUND` when the
     * session is no longer live; `OUT_OF_MEMORY` if allocation fails; otherwise `INTERNAL_ERROR`.
     */
    QUICR_C_API quicr_status_t quicr_session_goaway_publish_track_handler(quicr_session_t* session,
                                                                          const char* new_session_uri,
                                                                          uint64_t timeout_ms,
                                                                          const quicr_publish_track_handler_t* handler);

    /**
     * @brief Send handler-scoped GOAWAY for a subscribe handler.
     *
     * @param session Borrowed session handle.
     * @param new_session_uri Optional URI copied during the call; NULL means an empty URI.
     * @param timeout_ms Drain timeout in milliseconds.
     * @param handler Subscribe handler that determines the request cutoff.
     *
     * @return `OK` on success; `INVALID_ARGUMENT` for invalid pointers, registration, or state; `NOT_FOUND` when the
     * session is no longer live; `OUT_OF_MEMORY` if allocation fails; otherwise `INTERNAL_ERROR`.
     */
    QUICR_C_API quicr_status_t
    quicr_session_goaway_subscribe_track_handler(quicr_session_t* session,
                                                 const char* new_session_uri,
                                                 uint64_t timeout_ms,
                                                 const quicr_subscribe_track_handler_t* handler);

    /**
     * @brief Send handler-scoped GOAWAY for a fetch handler.
     *
     * @param session Borrowed session handle.
     * @param new_session_uri Optional URI copied during the call; NULL means an empty URI.
     * @param timeout_ms Drain timeout in milliseconds.
     * @param handler Fetch handler that determines the request cutoff.
     *
     * @return `OK` on success; `INVALID_ARGUMENT` for invalid pointers, registration, or state; `NOT_FOUND` when the
     * session is no longer live; `OUT_OF_MEMORY` if allocation fails; otherwise `INTERNAL_ERROR`.
     */
    QUICR_C_API quicr_status_t quicr_session_goaway_fetch_track_handler(quicr_session_t* session,
                                                                        const char* new_session_uri,
                                                                        uint64_t timeout_ms,
                                                                        const quicr_fetch_track_handler_t* handler);

    /**
     * @brief Send handler-scoped GOAWAY for a namespace-publish handler.
     *
     * @param session Borrowed session handle.
     * @param new_session_uri Optional URI copied during the call; NULL means an empty URI.
     * @param timeout_ms Drain timeout in milliseconds.
     * @param handler Namespace-publish handler that determines the request cutoff.
     *
     * @return `OK` on success; `INVALID_ARGUMENT` for invalid pointers, registration, or state; `NOT_FOUND` when the
     * session is no longer live; `OUT_OF_MEMORY` if allocation fails; otherwise `INTERNAL_ERROR`.
     */
    QUICR_C_API quicr_status_t
    quicr_session_goaway_publish_namespace_handler(quicr_session_t* session,
                                                   const char* new_session_uri,
                                                   uint64_t timeout_ms,
                                                   const quicr_publish_namespace_handler_t* handler);

    /**
     * @brief Send handler-scoped GOAWAY for a namespace-subscribe handler.
     *
     * @param session Borrowed session handle.
     * @param new_session_uri Optional URI copied during the call; NULL means an empty URI.
     * @param timeout_ms Drain timeout in milliseconds.
     * @param handler Namespace-subscribe handler that determines the request cutoff.
     *
     * @return `OK` on success; `INVALID_ARGUMENT` for invalid pointers, registration, or state; `NOT_FOUND` when the
     * session is no longer live; `OUT_OF_MEMORY` if allocation fails; otherwise `INTERNAL_ERROR`.
     */
    QUICR_C_API quicr_status_t
    quicr_session_goaway_subscribe_namespace_handler(quicr_session_t* session,
                                                     const char* new_session_uri,
                                                     uint64_t timeout_ms,
                                                     const quicr_subscribe_namespace_handler_t* handler);

    /**
     * @brief Send handler-scoped GOAWAY for a track-status handler.
     *
     * @param session Borrowed session handle.
     * @param new_session_uri Optional URI copied during the call; NULL means an empty URI.
     * @param timeout_ms Drain timeout in milliseconds.
     * @param handler Track-status handler that determines the request cutoff.
     *
     * @return `OK` on success; `INVALID_ARGUMENT` for invalid pointers, registration, or state; `NOT_FOUND` when the
     * session is no longer live; `OUT_OF_MEMORY` if allocation fails; otherwise `INTERNAL_ERROR`.
     */
    QUICR_C_API quicr_status_t
    quicr_session_goaway_track_status_handler(quicr_session_t* session,
                                              const char* new_session_uri,
                                              uint64_t timeout_ms,
                                              const quicr_track_status_track_handler_t* handler);

#if !defined(__cplusplus) && defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201112L

/**
 * @brief Type-generic handler-scoped GOAWAY for C11 and later C translation units.
 *
 * Supports mutable and const pointers to every handler accepted by the manager's generic add/remove macros.
 * C++ and pre-C11 builds do not define it. Arguments are evaluated once.
 *
 * @param session Borrowed session handle.
 * @param new_session_uri Optional URI copied during the call; NULL means an empty URI.
 * @param timeout_ms Drain timeout in milliseconds.
 * @param handler Handler that determines the request cutoff.
 *
 * @return The selected handler-specific GOAWAY status.
 */
#define quicr_session_goaway_handler(session, new_session_uri, timeout_ms, handler)                                    \
    _Generic((handler),                                                                                                \
      quicr_publish_track_handler_t*: quicr_session_goaway_publish_track_handler,                                      \
      const quicr_publish_track_handler_t*: quicr_session_goaway_publish_track_handler,                                \
      quicr_subscribe_track_handler_t*: quicr_session_goaway_subscribe_track_handler,                                  \
      const quicr_subscribe_track_handler_t*: quicr_session_goaway_subscribe_track_handler,                            \
      quicr_fetch_track_handler_t*: quicr_session_goaway_fetch_track_handler,                                          \
      const quicr_fetch_track_handler_t*: quicr_session_goaway_fetch_track_handler,                                    \
      quicr_publish_namespace_handler_t*: quicr_session_goaway_publish_namespace_handler,                              \
      const quicr_publish_namespace_handler_t*: quicr_session_goaway_publish_namespace_handler,                        \
      quicr_subscribe_namespace_handler_t*: quicr_session_goaway_subscribe_namespace_handler,                          \
      const quicr_subscribe_namespace_handler_t*: quicr_session_goaway_subscribe_namespace_handler,                    \
      quicr_track_status_track_handler_t*: quicr_session_goaway_track_status_handler,                                  \
      const quicr_track_status_track_handler_t*: quicr_session_goaway_track_status_handler)(                           \
      session, new_session_uri, timeout_ms, handler)

#endif

#ifdef __cplusplus
}
#endif

#endif
