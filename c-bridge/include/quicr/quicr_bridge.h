// SPDX-FileCopyrightText: Copyright (c) 2024 Cisco Systems
// SPDX-License-Identifier: BSD-2-Clause

/**
 * @file quicr_bridge.h
 * @brief C API bridge for libquicr
 *
 * This header provides a C language interface to the libquicr C++ library,
 * enabling C applications to use QuicR functionality for real-time media
 * transport over QUIC.
 */

#ifndef QUICR_BRIDGE_H
#define QUICR_BRIDGE_H

#ifdef __cplusplus
extern "C"
{
#endif

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/**
 * @defgroup constants Constants and Limits
 * @{
 */

/** Maximum number of namespace tuple entries */
#define QBRIDGE_MAX_NAMESPACE_ENTRIES 32

/** Maximum length of each namespace tuple entry */
#define QBRIDGE_MAX_TUPLE_LEN 1024

/** Maximum length of track name tuple */
#define QBRIDGE_MAX_TRACK_NAME_LEN 1024

/** Maximum hostname length */
#define QBRIDGE_MAX_HOSTNAME_LEN 256

/** Maximum filename length for TLS certificates */
#define QBRIDGE_MAX_FILENAME_LEN 512

    /** @} */

    /**
     * @defgroup opaque_handles Opaque Handle Types
     * @brief Opaque handles for C++ objects
     * @{
     */

    typedef struct qbridge_client qbridge_client_t;
    typedef struct qbridge_publish_track_handler qbridge_publish_track_handler_t;
    typedef struct qbridge_subscribe_track_handler qbridge_subscribe_track_handler_t;
    typedef struct qbridge_fetch_track_handler qbridge_fetch_track_handler_t;
    typedef struct qbridge_subscribe_namespace_track_handler qbridge_subscribe_namespace_track_handler_t;

    /** @} */

    /**
     * @defgroup simple_types Simple Types
     * @{
     */

    typedef uint64_t qbridge_connection_handle_t;
    typedef uint64_t qbridge_track_alias_t;
    typedef uint64_t qbridge_request_id_t;
    typedef uint64_t qbridge_group_id_t;
    typedef uint64_t qbridge_object_id_t;

    /** @} */

    /**
     * @defgroup result_codes Result and Status Codes
     * @{
     */

    /**
     * @brief Result codes for API operations
     */
    typedef enum
    {
        QBRIDGE_OK = 0,                /**< Operation successful */
        QBRIDGE_ERROR_INVALID_PARAM,   /**< Invalid parameter provided */
        QBRIDGE_ERROR_NOT_CONNECTED,   /**< Not connected to server */
        QBRIDGE_ERROR_NOT_AUTHORIZED,  /**< Operation not authorized */
        QBRIDGE_ERROR_NO_MEMORY,       /**< Memory allocation failed */
        QBRIDGE_ERROR_TIMEOUT,         /**< Operation timed out */
        QBRIDGE_ERROR_TRACK_NOT_FOUND, /**< Track not found */
        QBRIDGE_ERROR_ALREADY_EXISTS,  /**< Resource already exists */
        QBRIDGE_ERROR_INTERNAL,        /**< Internal error */
        QBRIDGE_ERROR_INVALID_STATE    /**< Invalid state for operation */
    } qbridge_result_t;

    /**
     * @brief Connection status states
     */
    typedef enum
    {
        QBRIDGE_STATUS_NOT_CONNECTED = 0, /**< Not connected */
        QBRIDGE_STATUS_CONNECTING,        /**< Connection in progress */
        QBRIDGE_STATUS_CONNECTED,         /**< Connected but not ready */
        QBRIDGE_STATUS_DISCONNECTING,     /**< Disconnection in progress */
        QBRIDGE_STATUS_READY,             /**< Connected and ready for operations */
        QBRIDGE_STATUS_ERROR              /**< Connection error */
    } qbridge_connection_status_t;

    /**
     * @brief Track delivery mode
     */
    typedef enum
    {
        QBRIDGE_DELIVERY_MODE_DATAGRAM = 0, /**< Datagram delivery mode */
        QBRIDGE_DELIVERY_MODE_STREAM        /**< Stream delivery mode */
    } qbridge_delivery_mode_t;

    /**
     * @brief Priority levels for objects and tracks
     */
    typedef enum
    {
        QBRIDGE_PRIORITY_VERY_LOW = 0, /**< Very low priority */
        QBRIDGE_PRIORITY_LOW = 1,      /**< Low priority */
        QBRIDGE_PRIORITY_NORMAL = 2,   /**< Normal priority */
        QBRIDGE_PRIORITY_HIGH = 3,     /**< High priority */
        QBRIDGE_PRIORITY_VERY_HIGH = 4 /**< Very high priority */
    } qbridge_priority_t;

    /**
     * @brief Publish status codes
     */
    typedef enum
    {
        QBRIDGE_PUBLISH_STATUS_OK = 0,                    /**< Ready to publish */
        QBRIDGE_PUBLISH_STATUS_NOT_CONNECTED,             /**< Not connected */
        QBRIDGE_PUBLISH_STATUS_NOT_ANNOUNCED,             /**< Not announced */
        QBRIDGE_PUBLISH_STATUS_PENDING_ANNOUNCE_RESPONSE, /**< Waiting for announce response */
        QBRIDGE_PUBLISH_STATUS_ANNOUNCE_NOT_AUTHORIZED,   /**< Announce not authorized */
        QBRIDGE_PUBLISH_STATUS_NO_SUBSCRIBERS,            /**< No subscribers */
        QBRIDGE_PUBLISH_STATUS_SUBSCRIPTION_UPDATED,      /**< Subscription updated */
        QBRIDGE_PUBLISH_STATUS_NEW_GROUP_REQUESTED,       /**< New group requested */
        QBRIDGE_PUBLISH_STATUS_PAUSED                     /**< Publishing paused */
    } qbridge_publish_status_t;

    /** @} */

    /**
     * @defgroup data_structures Data Structures
     * @{
     */

    /**
     * @brief Data buffer structure for holding binary data
     */
    typedef struct
    {
        uint8_t* data;   /**< Pointer to data */
        size_t length;   /**< Current length of data */
        size_t capacity; /**< Allocated capacity */
    } qbridge_data_buffer_t;

    /**
     * @brief Namespace structure with array of tuple entries
     *
     * A namespace consists of multiple tuple entries separated by '/' when
     * represented as a string. Each entry maps to one element in the C++
     * TrackNamespace constructor.
     */
    typedef struct
    {
        /** Array of namespace tuple entries */
        char tuples[QBRIDGE_MAX_NAMESPACE_ENTRIES][QBRIDGE_MAX_TUPLE_LEN];
        /** Number of valid tuple entries */
        size_t num_tuples;
    } qbridge_namespace_t;

    /**
     * @brief Track name structure with single tuple entry
     */
    typedef struct
    {
        /** Track name tuple */
        char tuple[QBRIDGE_MAX_TRACK_NAME_LEN];
        /** Length of track name tuple */
        size_t tuple_len;
    } qbridge_track_name_t;

    /**
     * @brief Full track name combining namespace and track name
     */
    typedef struct
    {
        qbridge_namespace_t ns;          /**< Namespace */
        qbridge_track_name_t track_name; /**< Track name */
    } qbridge_full_track_name_t;

    /**
     * @brief Object headers containing metadata
     */
    typedef struct
    {
        qbridge_group_id_t group_id;   /**< Group ID */
        uint64_t subgroup_id;          /**< Subgroup ID */
        qbridge_object_id_t object_id; /**< Object ID */
        qbridge_priority_t priority;   /**< Priority level */
        uint32_t ttl_ms;               /**< Time-to-live in milliseconds */
        bool cacheable;                /**< Whether object is cacheable */
    } qbridge_object_headers_t;

    /**
     * @brief Complete object with headers and payload
     */
    typedef struct
    {
        qbridge_object_headers_t headers; /**< Object headers */
        qbridge_data_buffer_t payload;    /**< Object payload */
    } qbridge_object_t;

    /** @} */

    /**
     * @defgroup config_structures Configuration Structures
     * @{
     */

    /**
     * @brief Client configuration
     *
     * Use qbridge_client_config_init() to initialize with default values.
     */
    typedef struct
    {
        /** Server hostname (default: "localhost") */
        char server_hostname[QBRIDGE_MAX_HOSTNAME_LEN];
        /** Server port (default: 4433) */
        uint16_t server_port;
        /** TLS certificate filename (optional) */
        char tls_cert_filename[QBRIDGE_MAX_FILENAME_LEN];
        /** TLS key filename (optional) */
        char tls_key_filename[QBRIDGE_MAX_FILENAME_LEN];
        /** Connection timeout in milliseconds (default: 5000) */
        uint32_t connect_timeout_ms;
        /** Idle timeout in milliseconds (default: 30000) */
        uint32_t idle_timeout_ms;
        /** Tick service sleep delay in microseconds (default: 333) */
        uint32_t tick_service_sleep_delay_us;
        /** Enable debug logging (default: false) */
        bool debug_logs;
        /** Use reset and wait strategy (default: false) */
        bool use_reset_wait_strategy;
    } qbridge_client_config_t;

    /**
     * @brief Publish track configuration
     */
    typedef struct
    {
        qbridge_full_track_name_t full_track_name; /**< Full track name */
        qbridge_delivery_mode_t delivery_mode;     /**< Delivery mode */
        qbridge_priority_t default_priority;       /**< Default priority */
        uint32_t default_ttl_ms;                   /**< Default TTL in ms */
        bool default_cacheable;                    /**< Default cacheable flag */
        uint64_t track_alias;                      /**< Track alias */
        bool use_announce;                         /**< Use announce flow */
    } qbridge_publish_track_config_t;

    /**
     * @brief Subscribe track configuration
     */
    typedef struct
    {
        qbridge_full_track_name_t full_track_name; /**< Full track name */
        qbridge_priority_t priority;               /**< Priority */
        uint64_t group_order;                      /**< Group order */
        uint64_t track_alias;                      /**< Track alias */
        uint64_t start_group_id;                   /**< Start group ID */
        uint64_t start_object_id;                  /**< Start object ID */
        uint64_t end_group_id;                     /**< End group ID */
        uint64_t end_object_id;                    /**< End object ID */
    } qbridge_subscribe_track_config_t;

    /**
     * @brief Fetch track configuration
     */
    typedef struct
    {
        qbridge_full_track_name_t full_track_name; /**< Full track name */
        qbridge_priority_t priority;               /**< Priority */
        uint64_t group_order;                      /**< Group order */
        uint64_t start_group_id;                   /**< Start group ID */
        uint64_t start_object_id;                  /**< Start object ID */
        uint64_t end_group_id;                     /**< End group ID */
        uint64_t end_object_id;                    /**< End object ID */
    } qbridge_fetch_track_config_t;

    /** @} */

    /**
     * @defgroup callback_types Callback Function Types
     * @{
     */

    /**
     * @brief Connection status change callback
     * @param status New connection status
     * @param user_data User-provided data pointer
     */
    typedef void (*qbridge_connection_status_callback_t)(qbridge_connection_status_t status, void* user_data);

    /**
     * @brief Namespace callback
     * @param ns Namespace
     * @param user_data User-provided data pointer
     */
    typedef void (*qbridge_namespace_callback_t)(const qbridge_namespace_t* ns, void* user_data);

    /**
     * @brief Object received callback
     * @param object Received object
     * @param user_data User-provided data pointer
     */
    typedef void (*qbridge_object_received_callback_t)(const qbridge_object_t* object, void* user_data);

    /**
     * @brief Object published callback
     * @param group_id Group ID of published object
     * @param object_id Object ID of published object
     * @param result Result of publish operation
     * @param user_data User-provided data pointer
     */
    typedef void (*qbridge_object_published_callback_t)(qbridge_group_id_t group_id,
                                                        qbridge_object_id_t object_id,
                                                        qbridge_result_t result,
                                                        void* user_data);

    /**
     * @brief Publish status callback
     * @param status Publish status
     * @param can_publish Whether publishing is currently allowed
     * @param user_data User-provided data pointer
     */
    typedef void (*qbridge_publish_status_callback_t)(qbridge_publish_status_t status,
                                                      bool can_publish,
                                                      void* user_data);

    /** @} */

    /**
     * @defgroup client_api Client API
     * @{
     */

    /**
     * @defgroup client_lifecycle Client Lifecycle
     * @{
     */

    /**
     * @brief Create a new client instance
     * @param config Client configuration
     * @return Client handle or NULL on failure
     */
    qbridge_client_t* qbridge_client_create(const qbridge_client_config_t* config);

    /**
     * @brief Destroy a client instance
     * @param client Client handle
     */
    void qbridge_client_destroy(qbridge_client_t* client);

    /**
     * @brief Connect client to server
     * @param client Client handle
     * @return Result code
     */
    qbridge_result_t qbridge_client_connect(qbridge_client_t* client);

    /**
     * @brief Disconnect client from server
     * @param client Client handle
     * @return Result code
     */
    qbridge_result_t qbridge_client_disconnect(qbridge_client_t* client);

    /**
     * @brief Get current connection status
     * @param client Client handle
     * @return Connection status
     */
    qbridge_connection_status_t qbridge_client_get_status(const qbridge_client_t* client);

    /** @} */

    /**
     * @defgroup client_callbacks Client Callback Registration
     * @{
     */

    /**
     * @brief Set connection status callback
     * @param client Client handle
     * @param callback Callback function
     * @param user_data User data to pass to callback
     */
    void qbridge_client_set_status_callback(qbridge_client_t* client,
                                            qbridge_connection_status_callback_t callback,
                                            void* user_data);

    /**
     * @brief Set namespace callback
     * @param client Client handle
     * @param callback Callback function
     * @param user_data User data to pass to callback
     */
    void qbridge_client_set_namespace_callback(qbridge_client_t* client,
                                               qbridge_namespace_callback_t callback,
                                               void* user_data);

    /** @} */

    /**
     * @defgroup namespace_ops Namespace Operations
     * @{
     */

    /**
     * @brief Publish a namespace
     * @param client Client handle
     * @param ns Namespace to publish
     * @return Result code
     */
    qbridge_result_t qbridge_client_publish_namespace(qbridge_client_t* client, const qbridge_namespace_t* ns);

    /**
     * @brief Unpublish a namespace
     * @param client Client handle
     * @param ns Namespace to unpublish
     * @return Result code
     */
    qbridge_result_t qbridge_client_unpublish_namespace(qbridge_client_t* client, const qbridge_namespace_t* ns);

    /**
     * @brief Subscribe to a namespace
     * @param client Client handle
     * @param handler Track handler to subscribe to
     * @return Result code
     */
    qbridge_result_t qbridge_client_subscribe_namespace(qbridge_client_t* client,
                                                        const qbridge_subscribe_namespace_track_handler_t* handler);

    /**
     * @brief Unsubscribe from a namespace
     * @param client Client handle
     * @param handler Track handler to unsubscribe from
     * @return Result code
     */
    qbridge_result_t qbridge_client_unsubscribe_namespace(qbridge_client_t* client,
                                                          const qbridge_subscribe_namespace_track_handler_t* handler);

    /** @} */

    /**
     * @defgroup track_handlers Track Handler Creation
     * @{
     */

    /**
     * @brief Create a publish track handler
     * @param config Track configuration
     * @param published_callback Callback for published objects
     * @param status_callback Callback for status changes
     * @param user_data User data to pass to callbacks
     * @return Track handler or NULL on failure
     */
    qbridge_publish_track_handler_t* qbridge_create_publish_track_handler(
      const qbridge_publish_track_config_t* config,
      qbridge_object_published_callback_t published_callback,
      qbridge_publish_status_callback_t status_callback,
      void* user_data);

    /**
     * @brief Destroy a publish track handler
     * @param handler Track handler
     */
    void qbridge_destroy_publish_track_handler(qbridge_publish_track_handler_t* handler);

    /**
     * @brief Create a subscribe track handler
     * @param config Track configuration
     * @param received_callback Callback for received objects
     * @param user_data User data to pass to callback
     * @return Track handler or NULL on failure
     */
    qbridge_subscribe_track_handler_t* qbridge_create_subscribe_track_handler(
      const qbridge_subscribe_track_config_t* config,
      qbridge_object_received_callback_t received_callback,
      void* user_data);

    /**
     * @brief Destroy a subscribe track handler
     * @param handler Track handler
     */
    void qbridge_destroy_subscribe_track_handler(qbridge_subscribe_track_handler_t* handler);

    /**
     * @brief Create a fetch track handler
     * @param config Track configuration
     * @param received_callback Callback for received objects
     * @param user_data User data to pass to callback
     * @return Track handler or NULL on failure
     */
    qbridge_fetch_track_handler_t* qbridge_create_fetch_track_handler(
      const qbridge_fetch_track_config_t* config,
      qbridge_object_received_callback_t received_callback,
      void* user_data);

    /**
     * @brief Destroy a fetch track handler
     * @param handler Track handler
     */
    void qbridge_destroy_fetch_track_handler(qbridge_fetch_track_handler_t* handler);

    /** @} */

    /**
     * @defgroup track_ops Track Operations
     * @{
     */

    /**
     * @brief Publish a track
     * @param client Client handle
     * @param handler Publish track handler
     * @return Result code
     */
    qbridge_result_t qbridge_client_publish_track(qbridge_client_t* client, qbridge_publish_track_handler_t* handler);

    /**
     * @brief Unpublish a track
     * @param client Client handle
     * @param handler Publish track handler
     * @return Result code
     */
    qbridge_result_t qbridge_client_unpublish_track(qbridge_client_t* client, qbridge_publish_track_handler_t* handler);

    /**
     * @brief Subscribe to a track
     * @param client Client handle
     * @param handler Subscribe track handler
     * @return Result code
     */
    qbridge_result_t qbridge_client_subscribe_track(qbridge_client_t* client,
                                                    qbridge_subscribe_track_handler_t* handler);

    /**
     * @brief Unsubscribe from a track
     * @param client Client handle
     * @param handler Subscribe track handler
     * @return Result code
     */
    qbridge_result_t qbridge_client_unsubscribe_track(qbridge_client_t* client,
                                                      qbridge_subscribe_track_handler_t* handler);

    /**
     * @brief Fetch a track
     * @param client Client handle
     * @param handler Fetch track handler
     * @return Result code
     */
    qbridge_result_t qbridge_client_fetch_track(qbridge_client_t* client, qbridge_fetch_track_handler_t* handler);

    /**
     * @brief Cancel a fetch track operation
     * @param client Client handle
     * @param handler Fetch track handler
     * @return Result code
     */
    qbridge_result_t qbridge_client_cancel_fetch_track(qbridge_client_t* client,
                                                       qbridge_fetch_track_handler_t* handler);

    /** @} */

    /**
     * @defgroup publish_ops Publishing Operations
     * @{
     */

    /**
     * @brief Check if track can currently publish
     * @param handler Publish track handler
     * @return true if publishing is allowed, false otherwise
     */
    bool qbridge_publish_track_can_publish(const qbridge_publish_track_handler_t* handler);

    /**
     * @brief Publish an object with separate headers and data
     * @param handler Publish track handler
     * @param headers Object headers
     * @param data Object data
     * @param data_len Data length
     * @return Result code
     */
    qbridge_result_t qbridge_publish_object_with_headers(qbridge_publish_track_handler_t* handler,
                                                         const qbridge_object_headers_t* headers,
                                                         const uint8_t* data,
                                                         size_t data_len);

    /**
     * @brief End a subgroup
     * @param handler Publish track handler
     * @param group_id Group ID containing the subgroup
     * @param subgroup_id Subgroup ID to end
     * @param completed True to close gracefully (FIN), false to reset
     * @return Result code
     */
    qbridge_result_t qbridge_end_subgroup(qbridge_publish_track_handler_t* handler,
                                          uint64_t group_id,
                                          uint64_t subgroup_id,
                                          bool completed);

    /** @} */
    /** @} */

    /**
     * @defgroup utility_functions Utility Functions
     * @{
     */

    /**
     * @defgroup buffer_mgmt Buffer Management
     * @{
     */

    /** @} */

    /**
     * @defgroup namespace_utils Namespace and Track Name Utilities
     * @{
     */

    /**
     * @brief Create namespace from string with '/' separated tuples
     * @param ns Namespace structure to fill
     * @param namespace_str String containing '/' separated namespace tuples
     * @return Result code
     *
     * Example: "example/publisher/video" will create a namespace with 3 tuples:
     *          ["example", "publisher", "video"]
     */
    qbridge_result_t qbridge_namespace_from_string(qbridge_namespace_t* ns, const char* namespace_str);

    /**
     * @brief Create full track name from namespace and track name strings
     * @param track_name Full track name structure to fill
     * @param namespace_str String containing '/' separated namespace tuples
     * @param track_name_str Track name string
     * @return Result code
     */
    qbridge_result_t qbridge_full_track_name_from_strings(qbridge_full_track_name_t* track_name,
                                                          const char* namespace_str,
                                                          const char* track_name_str);

    /**
     * @brief Compute track alias using hashing logic
     * @param track_name Full track name
     * @return Track alias hash value or 0 on failure
     */
    qbridge_track_alias_t qbridge_compute_track_alias(const qbridge_full_track_name_t* track_name);

    /** @} */

    /**
     * @defgroup config_helpers Configuration Helpers
     * @{
     */

    /**
     * @brief Initialize client configuration with default values
     * @param config Configuration structure to initialize
     *
     * Default values:
     * - server_hostname: "localhost"
     * - server_port: 4433
     * - connect_timeout_ms: 5000
     * - idle_timeout_ms: 30000
     * - tick_service_sleep_delay_us: 333
     * - debug_logs: false
     * - use_reset_wait_strategy: false
     */
    void qbridge_client_config_init(qbridge_client_config_t* config);

    /**
     * @brief Initialize publish track configuration with default values
     * @param config Configuration structure to initialize
     *
     * Default values:
     * - default_priority: QBRIDGE_PRIORITY_NORMAL
     * - default_ttl_ms: 5000
     * - default_cacheable: true
     * - delivery_mode: QBRIDGE_DELIVERY_MODE_DATAGRAM
     * - use_announce: false
     */
    void qbridge_publish_track_config_init(qbridge_publish_track_config_t* config);

    /**
     * @brief Initialize subscribe track configuration with default values
     * @param config Configuration structure to initialize
     *
     * Default values:
     * - priority: QBRIDGE_PRIORITY_NORMAL
     */
    void qbridge_subscribe_track_config_init(qbridge_subscribe_track_config_t* config);

    /**
     * @brief Initialize fetch track configuration with default values
     * @param config Configuration structure to initialize
     *
     * Default values:
     * - priority: QBRIDGE_PRIORITY_NORMAL
     */
    void qbridge_fetch_track_config_init(qbridge_fetch_track_config_t* config);

    /** @} */

    /**
     * @defgroup error_handling Error Handling
     * @{
     */

    /**
     * @brief Convert result code to string
     * @param result Result code
     * @return String representation of result code
     */
    const char* qbridge_result_to_string(qbridge_result_t result);

    /**
     * @brief Convert status code to string
     * @param status Status code
     * @return String representation of status code
     */
    const char* qbridge_status_to_string(qbridge_connection_status_t status);

    /** @} */
    /** @} */

#ifdef __cplusplus
}
#endif

#endif // QUICR_BRIDGE_H
