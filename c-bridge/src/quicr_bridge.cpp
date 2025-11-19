// SPDX-FileCopyrightText: Copyright (c) 2024 Cisco Systems
// SPDX-License-Identifier: BSD-2-Clause

/**
 * @file quicr_bridge.cpp
 * @brief Implementation of C API bridge for libquicr
 */

#include "quicr/quicr_bridge.h"

#include <quicr/client.h>
#include <quicr/common.h>
#include <quicr/config.h>
#include <quicr/fetch_track_handler.h>
#include <quicr/object.h>
#include <quicr/publish_track_handler.h>
#include <quicr/server.h>
#include <quicr/subscribe_namespace_handler.h>
#include <quicr/subscribe_track_handler.h>
#include <quicr/track_name.h>

#include <cstring>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace {

    /**
     * @brief Convert C namespace structure to C++ TrackNamespace
     *
     * Takes the array of tuples from the C structure and converts each
     * to a vector<uint8_t> for the C++ constructor.
     *
     */
    quicr::TrackNamespace cpp_namespace_from_c(const qbridge_namespace_t* c_namespace)
    {
        if (!c_namespace || c_namespace->num_tuples == 0 || c_namespace->num_tuples > QBRIDGE_MAX_NAMESPACE_ENTRIES) {
            return quicr::TrackNamespace{};
        }

        std::vector<std::vector<uint8_t>> entries;
        entries.reserve(c_namespace->num_tuples);

        for (size_t i = 0; i < c_namespace->num_tuples; ++i) {
            const char* const tuple = c_namespace->tuples[i];
            const size_t len = std::strlen(tuple);

            // Direct construction from char* cast to uint8_t* - avoids string copy
            entries.emplace_back(reinterpret_cast<const uint8_t*>(tuple),
                                 reinterpret_cast<const uint8_t*>(tuple + len));
        }

        return quicr::TrackNamespace(std::move(entries));
    }

    /**
     * @brief Convert C full track name to C++ FullTrackName
     *
     * Optimizations:
     * - Validate tuple_len bounds to prevent buffer overrun
     * - Use std::move to avoid copying namespace
     * - Direct construction from char* cast to uint8_t* (safe, same representation)
     * - Reserve exact size for track_name vector
     */
    quicr::FullTrackName cpp_track_name_from_c(const qbridge_full_track_name_t* c_track_name)
    {
        if (!c_track_name || c_track_name->track_name.tuple_len == 0 ||
            c_track_name->track_name.tuple_len > QBRIDGE_MAX_TRACK_NAME_LEN) {
            return quicr::FullTrackName{};
        }

        const auto ns = cpp_namespace_from_c(&c_track_name->ns);

        const uint8_t* const tuple_start = reinterpret_cast<const uint8_t*>(c_track_name->track_name.tuple);
        std::vector<uint8_t> track_name(tuple_start, tuple_start + c_track_name->track_name.tuple_len);

        return quicr::FullTrackName{ std::move(ns), std::move(track_name) };
    }

    /**
     * @brief Convert C++ TrackNamespace to C namespace structure
     *
     * Converts each entry in the C++ namespace back to string tuples
     * in the C structure.
     */
    void c_namespace_from_cpp(qbridge_namespace_t* c_namespace, const quicr::TrackNamespace& cpp_namespace)
    {
        if (!c_namespace)
            return;

        const auto& entries = cpp_namespace.GetEntries();
        c_namespace->num_tuples = std::min(entries.size(), static_cast<size_t>(QBRIDGE_MAX_NAMESPACE_ENTRIES));

        for (size_t i = 0; i < c_namespace->num_tuples; ++i) {
            const auto& entry = entries[i];
            const size_t len = std::min(entry.size(), static_cast<size_t>(QBRIDGE_MAX_TUPLE_LEN - 1));
            std::memcpy(c_namespace->tuples[i], entry.data(), len);
            c_namespace->tuples[i][len] = '\0';
        }
    }

    /**
     * @brief Convert C++ FullTrackName to C full track name structure
     */
    [[maybe_unused]] void c_track_name_from_cpp(qbridge_full_track_name_t* c_track_name,
                                                const quicr::FullTrackName& cpp_track_name)
    {
        if (!c_track_name)
            return;

        c_namespace_from_cpp(&c_track_name->ns, cpp_track_name.name_space);

        const auto& track_str = cpp_track_name.name;
        const size_t len = std::min(track_str.size(), static_cast<size_t>(QBRIDGE_MAX_TRACK_NAME_LEN - 1));
        std::memcpy(c_track_name->track_name.tuple, track_str.data(), len);
        c_track_name->track_name.tuple[len] = '\0';
        c_track_name->track_name.tuple_len = len;
    }

    /**
     * @brief Convert C client config to C++ ClientConfig
     */
    quicr::ClientConfig cpp_client_config_from_c(const qbridge_client_config_t* c_config)
    {
        quicr::ClientConfig config;

        if (c_config) {
            config.connect_uri =
              std::string("moq://") + c_config->server_hostname + ":" + std::to_string(c_config->server_port);
            config.endpoint_id = "qbridge_client";
            config.tick_service_sleep_delay_us = c_config->tick_service_sleep_delay_us;

            // Configure transport
            if (strlen(c_config->tls_cert_filename) > 0) {
                config.transport_config.tls_cert_filename = c_config->tls_cert_filename;
            }
            if (strlen(c_config->tls_key_filename) > 0) {
                config.transport_config.tls_key_filename = c_config->tls_key_filename;
            }
            config.transport_config.debug = c_config->debug_logs;
            config.transport_config.use_reset_wait_strategy = c_config->use_reset_wait_strategy;
            config.transport_config.time_queue_max_duration = 50000; // Set to 50 seconds to handle TTLs up to 50000ms
        }

        return config;
    }

    /**
     * @brief Convert C++ transport status to C connection status
     */
    qbridge_connection_status_t status_from_cpp(const quicr::Transport::Status cpp_status)
    {
        switch (cpp_status) {
            case quicr::Transport::Status::kNotConnected:
                return QBRIDGE_STATUS_NOT_CONNECTED;
            case quicr::Transport::Status::kConnecting:
                return QBRIDGE_STATUS_CONNECTING;
            case quicr::Transport::Status::kNotReady:
                return QBRIDGE_STATUS_CONNECTED;
            case quicr::Transport::Status::kDisconnecting:
                return QBRIDGE_STATUS_DISCONNECTING;
            case quicr::Transport::Status::kReady:
                return QBRIDGE_STATUS_READY;
            default:
                return QBRIDGE_STATUS_ERROR;
        }
    }

} // anonymous namespace

// === CLIENT IMPLEMENTATION ===

/**
 * @brief Bridge client class extending quicr::Client
 */
class BridgeClient : public quicr::Client
{
  public:
    static std::shared_ptr<BridgeClient> Create(const quicr::ClientConfig& cfg)
    {
        return std::shared_ptr<BridgeClient>(new BridgeClient(cfg));
    }

    qbridge_connection_status_callback_t status_callback = nullptr;
    void* status_callback_user_data = nullptr;
    qbridge_namespace_callback_t namespace_callback = nullptr;
    void* namespace_callback_user_data = nullptr;
    std::mutex callback_mutex;

  protected:
    BridgeClient(const quicr::ClientConfig& config)
      : quicr::Client(config)
    {
    }

  public:
    void StatusChanged(const quicr::Transport::Status status) override
    {
        std::lock_guard<std::mutex> lock(callback_mutex);
        if (status_callback) {
            status_callback(status_from_cpp(status), status_callback_user_data);
        }
    }
};

/**
 * @brief C client structure wrapping C++ client
 */
struct qbridge_client
{
    std::shared_ptr<BridgeClient> cpp_client;

    qbridge_client(const qbridge_client_config_t* config)
    {
        auto cpp_config = cpp_client_config_from_c(config);
        cpp_client = BridgeClient::Create(cpp_config);
    }
};

/**
 * @brief Bridge publish track handler extending quicr::PublishTrackHandler
 */
class BridgePublishTrackHandler : public quicr::PublishTrackHandler
{
  public:
    static std::shared_ptr<BridgePublishTrackHandler> Create(const quicr::FullTrackName& full_track_name,
                                                             const quicr::TrackMode track_mode,
                                                             const uint8_t default_priority,
                                                             const uint32_t default_ttl,
                                                             qbridge_object_published_callback_t callback,
                                                             qbridge_publish_status_callback_t status_cb,
                                                             void* user_data)
    {
        return std::shared_ptr<BridgePublishTrackHandler>(new BridgePublishTrackHandler(
          full_track_name, track_mode, default_priority, default_ttl, callback, status_cb, user_data));
    }

    qbridge_object_published_callback_t published_callback = nullptr;
    qbridge_publish_status_callback_t status_callback = nullptr;
    void* user_data = nullptr;
    std::mutex callback_mutex;

  protected:
    BridgePublishTrackHandler(const quicr::FullTrackName& full_track_name,
                              const quicr::TrackMode track_mode,
                              const uint8_t default_priority,
                              const uint32_t default_ttl,
                              qbridge_object_published_callback_t callback,
                              qbridge_publish_status_callback_t status_cb,
                              void* data)
      : quicr::PublishTrackHandler(full_track_name, track_mode, default_priority, default_ttl)
      , published_callback(callback)
      , status_callback(status_cb)
      , user_data(data)
    {
    }

  public:
    /**
     * @brief Handle status changes and notify C callback
     */
    void StatusChanged(const Status status) override
    {
        std::lock_guard<std::mutex> lock(callback_mutex);
        if (status_callback) {
            qbridge_publish_status_t c_status;
            switch (status) {
                case Status::kOk:
                    c_status = QBRIDGE_PUBLISH_STATUS_OK;
                    break;
                case Status::kNotConnected:
                    c_status = QBRIDGE_PUBLISH_STATUS_NOT_CONNECTED;
                    break;
                case Status::kNotAnnounced:
                    c_status = QBRIDGE_PUBLISH_STATUS_NOT_ANNOUNCED;
                    break;
                case Status::kPendingAnnounceResponse:
                    c_status = QBRIDGE_PUBLISH_STATUS_PENDING_ANNOUNCE_RESPONSE;
                    break;
                case Status::kAnnounceNotAuthorized:
                    c_status = QBRIDGE_PUBLISH_STATUS_ANNOUNCE_NOT_AUTHORIZED;
                    break;
                case Status::kNoSubscribers:
                    c_status = QBRIDGE_PUBLISH_STATUS_NO_SUBSCRIBERS;
                    break;
                case Status::kSubscriptionUpdated:
                    c_status = QBRIDGE_PUBLISH_STATUS_SUBSCRIPTION_UPDATED;
                    break;
                case Status::kNewGroupRequested:
                    c_status = QBRIDGE_PUBLISH_STATUS_NEW_GROUP_REQUESTED;
                    break;
                case Status::kPaused:
                    c_status = QBRIDGE_PUBLISH_STATUS_PAUSED;
                    break;
                default:
                    c_status = QBRIDGE_PUBLISH_STATUS_NOT_CONNECTED;
                    break;
            }

            const bool can_publish = CanPublish();
            status_callback(c_status, can_publish, user_data);
        }
        quicr::PublishTrackHandler::StatusChanged(status);
    }
};

/**
 * @brief C publish track handler structure
 */
struct qbridge_publish_track_handler
{
    std::shared_ptr<BridgePublishTrackHandler> cpp_handler;

    qbridge_publish_track_handler(const qbridge_publish_track_config_t* config,
                                  qbridge_object_published_callback_t callback,
                                  qbridge_publish_status_callback_t status_cb,
                                  void* data)
    {
        if (config) {
            const auto full_track_name = cpp_track_name_from_c(&config->full_track_name);

            // Map delivery mode to track mode
            const quicr::TrackMode track_mode = (config->delivery_mode == QBRIDGE_DELIVERY_MODE_STREAM)
                                                  ? quicr::TrackMode::kStream
                                                  : quicr::TrackMode::kDatagram;

            cpp_handler = BridgePublishTrackHandler::Create(full_track_name,
                                                            track_mode,
                                                            static_cast<uint8_t>(config->default_priority),
                                                            config->default_ttl_ms,
                                                            callback,
                                                            status_cb,
                                                            data);

            // Set use_announce flag if configured
            if (config->use_announce) {
                cpp_handler->SetUseAnnounce(true);
            }

            // Set track alias if provided
            if (config->track_alias > 0) {
                cpp_handler->SetTrackAlias(config->track_alias);
            }
        }
    }
};

/**
 * @brief Bridge subscribe track handler extending quicr::SubscribeTrackHandler
 */
class BridgeSubscribeTrackHandler : public quicr::SubscribeTrackHandler
{
  public:
    static std::shared_ptr<BridgeSubscribeTrackHandler> Create(const quicr::FullTrackName& full_track_name,
                                                               const quicr::messages::SubscriberPriority priority,
                                                               const quicr::messages::GroupOrder group_order,
                                                               const quicr::messages::FilterType filter_type,
                                                               qbridge_object_received_callback_t callback,
                                                               void* user_data)
    {
        return std::shared_ptr<BridgeSubscribeTrackHandler>(new BridgeSubscribeTrackHandler(
          full_track_name, priority, group_order, filter_type, std::nullopt, callback, user_data));
    }

    static std::shared_ptr<BridgeSubscribeTrackHandler> Create(
      const quicr::FullTrackName& full_track_name,
      const quicr::messages::SubscriberPriority priority,
      const quicr::messages::GroupOrder group_order,
      const quicr::messages::FilterType filter_type,
      const std::optional<quicr::SubscribeTrackHandler::JoiningFetch>& joining_fetch,
      qbridge_object_received_callback_t callback,
      void* user_data)
    {
        return std::shared_ptr<BridgeSubscribeTrackHandler>(new BridgeSubscribeTrackHandler(
          full_track_name, priority, group_order, filter_type, joining_fetch, callback, user_data));
    }

    qbridge_object_received_callback_t received_callback = nullptr;
    void* user_data = nullptr;

  protected:
    BridgeSubscribeTrackHandler(const quicr::FullTrackName& full_track_name,
                                const quicr::messages::SubscriberPriority priority,
                                const quicr::messages::GroupOrder group_order,
                                const quicr::messages::FilterType filter_type,
                                const std::optional<quicr::SubscribeTrackHandler::JoiningFetch>& joining_fetch,
                                qbridge_object_received_callback_t callback,
                                void* data)
      : quicr::SubscribeTrackHandler(full_track_name, priority, group_order, filter_type, joining_fetch)
      , received_callback(callback)
      , user_data(data)
    {
    }

  public:
    /**
     * @brief Handle received objects and notify C callback
     */
    void ObjectReceived(const quicr::ObjectHeaders& object_headers, quicr::BytesSpan data) override
    {
        if (received_callback) {
            // Convert C++ object to C object
            qbridge_object_t c_obj{};
            c_obj.headers.group_id = object_headers.group_id;
            c_obj.headers.subgroup_id = object_headers.subgroup_id;
            c_obj.headers.object_id = object_headers.object_id;
            c_obj.headers.priority = static_cast<qbridge_priority_t>(object_headers.priority.value_or(0));
            c_obj.headers.ttl_ms = object_headers.ttl.value_or(0);
            c_obj.headers.cacheable = false; // This field doesn't exist in new API

            // Copy payload data
            c_obj.payload.data = const_cast<uint8_t*>(data.data());
            c_obj.payload.length = data.size();
            c_obj.payload.capacity = data.size();

            received_callback(&c_obj, user_data);
        }
    }
};

/**
 * @brief C subscribe track handler structure
 */
struct qbridge_subscribe_track_handler
{
    std::shared_ptr<BridgeSubscribeTrackHandler> cpp_handler;

    qbridge_subscribe_track_handler(const qbridge_subscribe_track_config_t* config,
                                    qbridge_object_received_callback_t callback,
                                    void* data)
    {
        if (config) {
            const auto full_track_name = cpp_track_name_from_c(&config->full_track_name);

            // Determine the appropriate filter type based on config
            quicr::messages::FilterType filter_type;

            // Check if we have start and end group specified (for absolute range)
            const bool has_end_group = (config->end_group_id > 0);

            if (config->start_group_id > 0 && config->end_group_id > 0) {
                // Both start and end specified -> kAbsoluteRange
                filter_type = quicr::messages::FilterType::kAbsoluteRange;
            } else if (config->start_group_id > 0 && !has_end_group) {
                // Only start group specified -> kAbsoluteStart
                filter_type = quicr::messages::FilterType::kAbsoluteStart;
            } else if (config->start_group_id == 0 && !has_end_group) {
                filter_type = quicr::messages::FilterType::kNextGroupStart;
            } else {
                // No start/end specified -> kLargestObject (default)
                filter_type = quicr::messages::FilterType::kLargestObject;
            }

            // Create without joining fetch for normal subscribe (latest object)
            cpp_handler =
              BridgeSubscribeTrackHandler::Create(full_track_name,
                                                  static_cast<quicr::messages::SubscriberPriority>(config->priority),
                                                  static_cast<quicr::messages::GroupOrder>(config->group_order),
                                                  filter_type,
                                                  callback,
                                                  data);
        }
    }
};

/**
 * @brief Bridge fetch track handler extending quicr::FetchTrackHandler
 */
class BridgeFetchTrackHandler : public quicr::FetchTrackHandler
{
  public:
    static std::shared_ptr<BridgeFetchTrackHandler> Create(const quicr::FullTrackName& full_track_name,
                                                           const quicr::messages::SubscriberPriority priority,
                                                           const quicr::messages::GroupOrder group_order,
                                                           const quicr::messages::GroupId start_group,
                                                           const quicr::messages::GroupId end_group,
                                                           const quicr::messages::GroupId start_object,
                                                           const quicr::messages::GroupId end_object,
                                                           qbridge_object_received_callback_t callback,
                                                           void* user_data)
    {
        return std::shared_ptr<BridgeFetchTrackHandler>(new BridgeFetchTrackHandler(full_track_name,
                                                                                    priority,
                                                                                    group_order,
                                                                                    start_group,
                                                                                    end_group,
                                                                                    start_object,
                                                                                    end_object,
                                                                                    callback,
                                                                                    user_data));
    }

    qbridge_object_received_callback_t received_callback = nullptr;
    void* user_data = nullptr;

  protected:
    BridgeFetchTrackHandler(const quicr::FullTrackName& full_track_name,
                            const quicr::messages::SubscriberPriority priority,
                            const quicr::messages::GroupOrder group_order,
                            const quicr::messages::GroupId start_group,
                            const quicr::messages::GroupId end_group,
                            const quicr::messages::GroupId start_object,
                            const quicr::messages::GroupId end_object,
                            qbridge_object_received_callback_t callback,
                            void* data)
      : quicr::FetchTrackHandler(full_track_name,
                                 priority,
                                 group_order,
                                 start_group,
                                 end_group,
                                 start_object,
                                 end_object)
      , received_callback(callback)
      , user_data(data)
    {
    }

  public:
    /**
     * @brief Handle received objects and notify C callback
     */
    void ObjectReceived(const quicr::ObjectHeaders& object_headers, quicr::BytesSpan data) override
    {
        if (received_callback) {
            // Convert C++ object to C object
            qbridge_object_t c_obj{};
            c_obj.headers.group_id = object_headers.group_id;
            c_obj.headers.subgroup_id = object_headers.subgroup_id;
            c_obj.headers.object_id = object_headers.object_id;
            c_obj.headers.priority = static_cast<qbridge_priority_t>(object_headers.priority.value_or(0));
            c_obj.headers.ttl_ms = object_headers.ttl.value_or(0);
            c_obj.headers.cacheable = false; // This field doesn't exist in new API

            // Copy payload data
            c_obj.payload.data = const_cast<uint8_t*>(data.data());
            c_obj.payload.length = data.size();
            c_obj.payload.capacity = data.size();

            received_callback(&c_obj, user_data);
        }
    }
};

/**
 * @brief C fetch track handler structure
 */
struct qbridge_fetch_track_handler
{
    std::shared_ptr<BridgeFetchTrackHandler> cpp_handler;

    qbridge_fetch_track_handler(const qbridge_fetch_track_config_t* config,
                                qbridge_object_received_callback_t callback,
                                void* data)
    {
        if (config) {
            const auto full_track_name = cpp_track_name_from_c(&config->full_track_name);

            cpp_handler =
              BridgeFetchTrackHandler::Create(full_track_name,
                                              static_cast<quicr::messages::SubscriberPriority>(config->priority),
                                              static_cast<quicr::messages::GroupOrder>(config->group_order),
                                              config->start_group_id,
                                              config->end_group_id,
                                              config->start_object_id,
                                              config->end_object_id,
                                              callback,
                                              data);
        }
    }
};

/**
 * @brief Bridge fetch track handler extending quicr::FetchTrackHandler
 */
class BridgeSubscribeNamespaceHandler : public quicr::SubscribeNamespaceHandler
{
  public:
    static std::shared_ptr<BridgeSubscribeNamespaceHandler> Create(const quicr::TrackNamespace& ns)
    {
        return std::shared_ptr<BridgeSubscribeNamespaceHandler>(new BridgeSubscribeNamespaceHandler(ns));
    }

    qbridge_object_received_callback_t received_callback = nullptr;
    void* user_data = nullptr;

  protected:
    BridgeSubscribeNamespaceHandler(const quicr::TrackNamespace& ns)
      : quicr::SubscribeNamespaceHandler(ns)
    {
    }

  public:
    /**
     * @brief Handle received objects and notify C callback
     */
    void StatusChanged(Status) override {}
};

/**
 * @brief C subscribe namespace track handler structure
 */
struct qbridge_subscribe_namespace_track_handler
{
    std::shared_ptr<BridgeSubscribeNamespaceHandler> cpp_handler;

    qbridge_subscribe_namespace_track_handler(const qbridge_namespace_t* ns)
    {
        if (ns) {
            cpp_handler = BridgeSubscribeNamespaceHandler::Create(cpp_namespace_from_c(ns));
        }
    }
};

// === C API IMPLEMENTATION ===

extern "C"
{

    // Client lifecycle
    qbridge_client_t* qbridge_client_create(const qbridge_client_config_t* config)
    {
        if (!config) {
            return nullptr;
        }

        try {
            return new qbridge_client(config);
        } catch (...) {
            return nullptr;
        }
    }

    void qbridge_client_destroy(qbridge_client_t* client)
    {
        delete client;
    }

    qbridge_result_t qbridge_client_connect(qbridge_client_t* client)
    {
        if (!client || !client->cpp_client) {
            return QBRIDGE_ERROR_INVALID_PARAM;
        }

        const auto status = client->cpp_client->Connect();
        return (status == quicr::Transport::Status::kConnecting) ? QBRIDGE_OK : QBRIDGE_ERROR_INTERNAL;
    }

    qbridge_result_t qbridge_client_disconnect(qbridge_client_t* client)
    {
        if (!client || !client->cpp_client) {
            return QBRIDGE_ERROR_INVALID_PARAM;
        }

        const auto status = client->cpp_client->Disconnect();
        return (status == quicr::Transport::Status::kDisconnecting) ? QBRIDGE_OK : QBRIDGE_ERROR_INTERNAL;
    }

    qbridge_connection_status_t qbridge_client_get_status(const qbridge_client_t* client)
    {
        if (!client || !client->cpp_client) {
            return QBRIDGE_STATUS_ERROR;
        }

        return status_from_cpp(client->cpp_client->GetStatus());
    }

    // Client callback registration
    void qbridge_client_set_status_callback(qbridge_client_t* client,
                                            qbridge_connection_status_callback_t callback,
                                            void* user_data)
    {
        if (!client)
            return;

        std::lock_guard<std::mutex> lock(client->cpp_client->callback_mutex);
        client->cpp_client->status_callback = callback;
        client->cpp_client->status_callback_user_data = user_data;
    }

    void qbridge_client_set_namespace_callback(qbridge_client_t* client,
                                               qbridge_namespace_callback_t callback,
                                               void* user_data)
    {
        if (!client)
            return;

        std::lock_guard<std::mutex> lock(client->cpp_client->callback_mutex);
        client->cpp_client->namespace_callback = callback;
        client->cpp_client->namespace_callback_user_data = user_data;
    }

    // Namespace operations
    qbridge_result_t qbridge_client_publish_namespace(qbridge_client_t* client, const qbridge_namespace_t* ns)
    {
        if (!client || !client->cpp_client || !ns) {
            return QBRIDGE_ERROR_INVALID_PARAM;
        }

        const auto cpp_namespace = cpp_namespace_from_c(ns);
        client->cpp_client->PublishNamespace(cpp_namespace);
        return QBRIDGE_OK;
    }

    qbridge_result_t qbridge_client_unpublish_namespace(qbridge_client_t* client, const qbridge_namespace_t* ns)
    {
        if (!client || !client->cpp_client || !ns) {
            return QBRIDGE_ERROR_INVALID_PARAM;
        }

        const auto cpp_namespace = cpp_namespace_from_c(ns);
        client->cpp_client->PublishNamespaceDone(cpp_namespace);
        return QBRIDGE_OK;
    }

    qbridge_result_t qbridge_client_subscribe_namespace(qbridge_client_t* client,
                                                        const qbridge_subscribe_namespace_track_handler_t* handler)
    {
        if (!client || !client->cpp_client || !handler || !handler->cpp_handler) {
            return QBRIDGE_ERROR_INVALID_PARAM;
        }

        client->cpp_client->SubscribeNamespace(handler->cpp_handler);
        return QBRIDGE_OK;
    }

    qbridge_result_t qbridge_client_unsubscribe_namespace(qbridge_client_t* client,
                                                          const qbridge_subscribe_namespace_track_handler_t* handler)
    {
        if (!client || !client->cpp_client || !handler || !handler->cpp_handler) {
            return QBRIDGE_ERROR_INVALID_PARAM;
        }

        client->cpp_client->UnsubscribeNamespace(handler->cpp_handler);
        return QBRIDGE_OK;
    }

    // Track handler creation
    qbridge_publish_track_handler_t* qbridge_create_publish_track_handler(
      const qbridge_publish_track_config_t* config,
      qbridge_object_published_callback_t published_callback,
      qbridge_publish_status_callback_t status_callback,
      void* user_data)
    {

        if (!config) {
            return nullptr;
        }

        try {
            return new qbridge_publish_track_handler(config, published_callback, status_callback, user_data);
        } catch (...) {
            return nullptr;
        }
    }

    void qbridge_destroy_publish_track_handler(qbridge_publish_track_handler_t* handler)
    {
        delete handler;
    }

    qbridge_subscribe_track_handler_t* qbridge_create_subscribe_track_handler(
      const qbridge_subscribe_track_config_t* config,
      qbridge_object_received_callback_t received_callback,
      void* user_data)
    {

        if (!config) {
            return nullptr;
        }

        try {
            return new qbridge_subscribe_track_handler(config, received_callback, user_data);
        } catch (...) {
            return nullptr;
        }
    }

    void qbridge_destroy_subscribe_track_handler(qbridge_subscribe_track_handler_t* handler)
    {
        delete handler;
    }

    qbridge_fetch_track_handler_t* qbridge_create_fetch_track_handler(
      const qbridge_fetch_track_config_t* config,
      qbridge_object_received_callback_t received_callback,
      void* user_data)
    {

        if (!config) {
            return nullptr;
        }

        try {
            return new qbridge_fetch_track_handler(config, received_callback, user_data);
        } catch (...) {
            return nullptr;
        }
    }

    void qbridge_destroy_fetch_track_handler(qbridge_fetch_track_handler_t* handler)
    {
        delete handler;
    }

    // Track operations
    qbridge_result_t qbridge_client_publish_track(qbridge_client_t* client, qbridge_publish_track_handler_t* handler)
    {
        if (!client || !client->cpp_client || !handler || !handler->cpp_handler) {
            return QBRIDGE_ERROR_INVALID_PARAM;
        }

        client->cpp_client->PublishTrack(handler->cpp_handler);
        return QBRIDGE_OK;
    }

    qbridge_result_t qbridge_client_unpublish_track(qbridge_client_t* client, qbridge_publish_track_handler_t* handler)
    {
        if (!client || !client->cpp_client || !handler || !handler->cpp_handler) {
            return QBRIDGE_ERROR_INVALID_PARAM;
        }

        client->cpp_client->UnpublishTrack(handler->cpp_handler);
        return QBRIDGE_OK;
    }

    qbridge_result_t qbridge_client_subscribe_track(qbridge_client_t* client,
                                                    qbridge_subscribe_track_handler_t* handler)
    {
        if (!client || !client->cpp_client || !handler || !handler->cpp_handler) {
            return QBRIDGE_ERROR_INVALID_PARAM;
        }

        client->cpp_client->SubscribeTrack(handler->cpp_handler);
        return QBRIDGE_OK;
    }

    qbridge_result_t qbridge_client_unsubscribe_track(qbridge_client_t* client,
                                                      qbridge_subscribe_track_handler_t* handler)
    {
        if (!client || !client->cpp_client || !handler || !handler->cpp_handler) {
            return QBRIDGE_ERROR_INVALID_PARAM;
        }

        client->cpp_client->UnsubscribeTrack(handler->cpp_handler);
        return QBRIDGE_OK;
    }

    qbridge_result_t qbridge_client_fetch_track(qbridge_client_t* client, qbridge_fetch_track_handler_t* handler)
    {
        if (!client || !client->cpp_client || !handler || !handler->cpp_handler) {
            return QBRIDGE_ERROR_INVALID_PARAM;
        }

        client->cpp_client->FetchTrack(handler->cpp_handler);
        return QBRIDGE_OK;
    }

    qbridge_result_t qbridge_client_cancel_fetch_track(qbridge_client_t* client, qbridge_fetch_track_handler_t* handler)
    {
        if (!client || !client->cpp_client || !handler || !handler->cpp_handler) {
            return QBRIDGE_ERROR_INVALID_PARAM;
        }

        client->cpp_client->CancelFetchTrack(handler->cpp_handler);
        return QBRIDGE_OK;
    }

    // Publishing objects
    bool qbridge_publish_track_can_publish(const qbridge_publish_track_handler_t* handler)
    {
        if (!handler || !handler->cpp_handler) {
            return false;
        }

        return handler->cpp_handler->CanPublish();
    }

    qbridge_result_t qbridge_publish_object(qbridge_publish_track_handler_t* handler, const qbridge_object_t* object)
    {
        if (!handler || !handler->cpp_handler || !object) {
            return QBRIDGE_ERROR_INVALID_PARAM;
        }

        // Create C++ object headers
        quicr::ObjectHeaders cpp_headers;
        cpp_headers.group_id = object->headers.group_id;
        cpp_headers.subgroup_id = object->headers.subgroup_id;
        cpp_headers.object_id = object->headers.object_id;
        cpp_headers.priority = static_cast<uint8_t>(object->headers.priority);
        cpp_headers.ttl = object->headers.ttl_ms;
        cpp_headers.payload_length = object->payload.length;
        cpp_headers.status = quicr::ObjectStatus::kAvailable;

        // Create data span
        quicr::BytesSpan data_span;
        if (object->payload.data && object->payload.length > 0) {
            data_span = quicr::BytesSpan(object->payload.data, object->payload.length);
        }

        const auto status = handler->cpp_handler->PublishObject(cpp_headers, data_span);
        return (status == quicr::PublishTrackHandler::PublishObjectStatus::kOk) ? QBRIDGE_OK : QBRIDGE_ERROR_INTERNAL;
    }

    qbridge_result_t qbridge_publish_object_with_headers(qbridge_publish_track_handler_t* handler,
                                                         const qbridge_object_headers_t* headers,
                                                         const uint8_t* data,
                                                         size_t data_len)
    {
        if (!handler || !handler->cpp_handler || !headers) {
            return QBRIDGE_ERROR_INVALID_PARAM;
        }

        // Create C++ object headers
        quicr::ObjectHeaders cpp_headers;
        cpp_headers.group_id = headers->group_id;
        cpp_headers.subgroup_id = headers->subgroup_id;
        cpp_headers.object_id = headers->object_id;
        cpp_headers.priority = static_cast<uint8_t>(headers->priority);
        cpp_headers.ttl = headers->ttl_ms;
        cpp_headers.payload_length = data_len;
        cpp_headers.status = quicr::ObjectStatus::kAvailable;

        // Create data span
        quicr::BytesSpan data_span;
        if (data && data_len > 0) {
            data_span = quicr::BytesSpan(data, data_len);
        }

        const auto status = handler->cpp_handler->PublishObject(cpp_headers, data_span);
        return (status == quicr::PublishTrackHandler::PublishObjectStatus::kOk) ? QBRIDGE_OK : QBRIDGE_ERROR_INTERNAL;
    }

    // Utility functions
    qbridge_result_t qbridge_namespace_from_string(qbridge_namespace_t* ns, const char* namespace_str)
    {
        if (!ns || !namespace_str) {
            return QBRIDGE_ERROR_INVALID_PARAM;
        }

        // Parse namespace string directly without allocations
        ns->num_tuples = 0;
        const char* start = namespace_str;
        const char* end;

        while (*start && ns->num_tuples < QBRIDGE_MAX_NAMESPACE_ENTRIES) {
            // Find next delimiter or end of string
            end = start;
            while (*end && *end != '/') {
                end++;
            }

            const size_t tuple_len = end - start;
            if (tuple_len >= QBRIDGE_MAX_TUPLE_LEN) {
                return QBRIDGE_ERROR_INVALID_PARAM;
            }

            // Copy tuple
            std::memcpy(ns->tuples[ns->num_tuples], start, tuple_len);
            ns->tuples[ns->num_tuples][tuple_len] = '\0';
            ns->num_tuples++;

            // Move to next tuple (skip delimiter if present)
            start = (*end == '/') ? end + 1 : end;
        }

        return QBRIDGE_OK;
    }

    qbridge_result_t qbridge_full_track_name_from_strings(qbridge_full_track_name_t* track_name,
                                                          const char* namespace_str,
                                                          const char* track_name_str)
    {
        if (!track_name || !namespace_str || !track_name_str) {
            return QBRIDGE_ERROR_INVALID_PARAM;
        }

        const auto result = qbridge_namespace_from_string(&track_name->ns, namespace_str);
        if (result != QBRIDGE_OK) {
            return result;
        }

        const size_t len = std::strlen(track_name_str);
        if (len >= QBRIDGE_MAX_TRACK_NAME_LEN) {
            return QBRIDGE_ERROR_INVALID_PARAM;
        }

        std::strcpy(track_name->track_name.tuple, track_name_str);
        track_name->track_name.tuple_len = len;
        return QBRIDGE_OK;
    }

    qbridge_track_alias_t qbridge_compute_track_alias(const qbridge_full_track_name_t* track_name)
    {
        if (!track_name) {
            return 0;
        }

        try {
            auto cpp_track = cpp_track_name_from_c(track_name);
            auto track_hash = quicr::TrackHash(cpp_track);
            return track_hash.track_fullname_hash;
        } catch (...) {
            return 0;
        }
    }

    // Configuration helpers
    void qbridge_client_config_init(qbridge_client_config_t* config)
    {
        if (!config)
            return;

        std::memset(config, 0, sizeof(*config));
        config->server_port = 4433;
        config->connect_timeout_ms = 5000;
        config->idle_timeout_ms = 30000;
        config->tick_service_sleep_delay_us = 333;
        config->debug_logs = false;
        config->use_reset_wait_strategy = false;
        std::strcpy(config->server_hostname, "localhost");
    }

    // Error handling
    const char* qbridge_result_to_string(qbridge_result_t result)
    {
        switch (result) {
            case QBRIDGE_OK:
                return "Success";
            case QBRIDGE_ERROR_INVALID_PARAM:
                return "Invalid parameter";
            case QBRIDGE_ERROR_NOT_CONNECTED:
                return "Not connected";
            case QBRIDGE_ERROR_NOT_AUTHORIZED:
                return "Not authorized";
            case QBRIDGE_ERROR_NO_MEMORY:
                return "Out of memory";
            case QBRIDGE_ERROR_TIMEOUT:
                return "Timeout";
            case QBRIDGE_ERROR_TRACK_NOT_FOUND:
                return "Track not found";
            case QBRIDGE_ERROR_ALREADY_EXISTS:
                return "Already exists";
            case QBRIDGE_ERROR_INTERNAL:
                return "Internal error";
            case QBRIDGE_ERROR_INVALID_STATE:
                return "Invalid state";
            default:
                return "Unknown error";
        }
    }

    const char* qbridge_status_to_string(qbridge_connection_status_t status)
    {
        switch (status) {
            case QBRIDGE_STATUS_NOT_CONNECTED:
                return "Not connected";
            case QBRIDGE_STATUS_CONNECTING:
                return "Connecting";
            case QBRIDGE_STATUS_CONNECTED:
                return "Connected";
            case QBRIDGE_STATUS_DISCONNECTING:
                return "Disconnecting";
            case QBRIDGE_STATUS_READY:
                return "Ready";
            case QBRIDGE_STATUS_ERROR:
                return "Error";
            default:
                return "Unknown status";
        }
    }

    // Placeholder implementations for remaining functions
    void qbridge_publish_track_config_init(qbridge_publish_track_config_t* config)
    {
        if (!config)
            return;
        std::memset(config, 0, sizeof(*config));
        config->default_priority = QBRIDGE_PRIORITY_NORMAL;
        config->default_ttl_ms = 5000;
        config->default_cacheable = true;
        config->delivery_mode = QBRIDGE_DELIVERY_MODE_DATAGRAM;
        config->use_announce = false; // Use publish flow by default
    }

    void qbridge_subscribe_track_config_init(qbridge_subscribe_track_config_t* config)
    {
        if (!config)
            return;
        std::memset(config, 0, sizeof(*config));
        config->priority = QBRIDGE_PRIORITY_NORMAL;
    }

    void qbridge_fetch_track_config_init(qbridge_fetch_track_config_t* config)
    {
        if (!config)
            return;
        std::memset(config, 0, sizeof(*config));
        config->priority = QBRIDGE_PRIORITY_NORMAL;
    }

} // extern "C"
