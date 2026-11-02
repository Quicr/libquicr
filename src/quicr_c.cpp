// SPDX-FileCopyrightText: Copyright (c) 2026 Cisco Systems
// SPDX-License-Identifier: BSD-2-Clause

#include "quicr/quicr_c.h"

#include <quicr/session.h>
#include <quicr/session_callbacks.h>
#include <quicr/session_manager.h>

#include <mutex>
#include <stdexcept>

using namespace quicr;

namespace {
    template<typename C, typename Cpp>
    C to_c_type(const Cpp&);

    template<>
    quicr_stream_closed_flag_t to_c_type<quicr_stream_closed_flag_t, StreamClosedFlag>(const StreamClosedFlag& flag)
    {
        switch (flag) {
            case StreamClosedFlag::kFin:
                return QUICR_STREAM_CLOSED_FLAG_FIN;
            case StreamClosedFlag::kReset:
                return QUICR_STREAM_CLOSED_FLAG_RESET;
            case StreamClosedFlag::kStopSending:
                return QUICR_STREAM_CLOSED_FLAG_STOP_SENDING;
        }

        throw std::invalid_argument("Invalid stream closed flag value");
    }

    template<>
    quicr_session_status_t to_c_type<quicr_session_status_t, Session::Status>(const Session::Status& status)
    {
        switch (status) {
            case Session::Status::kReady:
                return QUICR_SESSION_STATUS_READY;
            case Session::Status::kNotReady:
                return QUICR_SESSION_STATUS_NOT_READY;
            case Session::Status::kInternalError:
                return QUICR_SESSION_STATUS_INTERNAL_ERROR;
            case Session::Status::kInvalidParams:
                return QUICR_SESSION_STATUS_INVALID_PARAMS;
            case Session::Status::kConnecting:
                return QUICR_SESSION_STATUS_CONNECTING;
            case Session::Status::kDisconnecting:
                return QUICR_SESSION_STATUS_DISCONNECTING;
            case Session::Status::kNotConnected:
                return QUICR_SESSION_STATUS_NOT_CONNECTED;
            case Session::Status::kFailedToConnect:
                return QUICR_SESSION_STATUS_FAILED_TO_CONNECT;
            case Session::Status::kPendingPeerSetup:
                return QUICR_SESSION_STATUS_PENDING_PEER_SETUP;
        }

        return QUICR_SESSION_STATUS_INTERNAL_ERROR;
    }

    template<>
    quicr_publish_object_status_t to_c_type<quicr_publish_object_status_t, PublishTrackHandler::PublishObjectStatus>(
      const PublishTrackHandler::PublishObjectStatus& status)
    {
        switch (status) {
            case PublishTrackHandler::PublishObjectStatus::kOk:
                return QUICR_PUBLISH_OBJECT_STATUS_OK;
            case PublishTrackHandler::PublishObjectStatus::kInternalError:
                return QUICR_PUBLISH_OBJECT_STATUS_INTERNAL_ERROR;
            case PublishTrackHandler::PublishObjectStatus::kNotAuthorized:
                return QUICR_PUBLISH_OBJECT_STATUS_NOT_AUTHORIZED;
            case PublishTrackHandler::PublishObjectStatus::kNotAnnounced:
                return QUICR_PUBLISH_OBJECT_STATUS_NOT_ANNOUNCED;
            case PublishTrackHandler::PublishObjectStatus::kNoSubscribers:
                return QUICR_PUBLISH_OBJECT_STATUS_NO_SUBSCRIBERS;
            case PublishTrackHandler::PublishObjectStatus::kObjectPayloadLengthExceeded:
                return QUICR_PUBLISH_OBJECT_STATUS_OBJECT_PAYLOAD_LENGTH_EXCEEDED;
            case PublishTrackHandler::PublishObjectStatus::kPreviousObjectTruncated:
                return QUICR_PUBLISH_OBJECT_STATUS_PREVIOUS_OBJECT_TRUNCATED;
            case PublishTrackHandler::PublishObjectStatus::kNoPreviousObject:
                return QUICR_PUBLISH_OBJECT_STATUS_NO_PREVIOUS_OBJECT;
            case PublishTrackHandler::PublishObjectStatus::kObjectDataComplete:
                return QUICR_PUBLISH_OBJECT_STATUS_OBJECT_DATA_COMPLETE;
            case PublishTrackHandler::PublishObjectStatus::kObjectContinuationDataNeeded:
                return QUICR_PUBLISH_OBJECT_STATUS_OBJECT_CONTINUATION_DATA_NEEDED;
            case PublishTrackHandler::PublishObjectStatus::kObjectDataIncomplete:
                return QUICR_PUBLISH_OBJECT_STATUS_OBJECT_DATA_INCOMPLETE;
            case PublishTrackHandler::PublishObjectStatus::kObjectDataTooLarge:
                return QUICR_PUBLISH_OBJECT_STATUS_OBJECT_DATA_TOO_LARGE;
            case PublishTrackHandler::PublishObjectStatus::kPreviousObjectNotCompleteMustStartNewGroup:
                return QUICR_PUBLISH_OBJECT_STATUS_PREVIOUS_OBJECT_NOT_COMPLETE_MUST_START_NEW_GROUP;
            case PublishTrackHandler::PublishObjectStatus::kPreviousObjectNotCompleteMustStartNewTrack:
                return QUICR_PUBLISH_OBJECT_STATUS_PREVIOUS_OBJECT_NOT_COMPLETE_MUST_START_NEW_TRACK;
            case PublishTrackHandler::PublishObjectStatus::kPaused:
                return QUICR_PUBLISH_OBJECT_STATUS_PAUSED;
            case PublishTrackHandler::PublishObjectStatus::kPendingPublishOk:
                return QUICR_PUBLISH_OBJECT_STATUS_PENDING_PUBLISH_OK;
        }

        throw std::invalid_argument("Invalid publish object status value");
    }

    template<>
    quicr_publish_track_handler_status_t to_c_type<quicr_publish_track_handler_status_t, PublishTrackHandler::Status>(
      const PublishTrackHandler::Status& status)
    {
        switch (status) {
            case PublishTrackHandler::Status::kOk:
                return QUICR_PUBLISH_TRACK_HANDLER_STATUS_OK;
            case PublishTrackHandler::Status::kNotConnected:
                return QUICR_PUBLISH_TRACK_HANDLER_STATUS_NOT_CONNECTED;
            case PublishTrackHandler::Status::kNotAnnounced:
                return QUICR_PUBLISH_TRACK_HANDLER_STATUS_NOT_ANNOUNCED;
            case PublishTrackHandler::Status::kPendingAnnounceResponse:
                return QUICR_PUBLISH_TRACK_HANDLER_STATUS_PENDING_ANNOUNCE_RESPONSE;
            case PublishTrackHandler::Status::kAnnounceNotAuthorized:
                return QUICR_PUBLISH_TRACK_HANDLER_STATUS_ANNOUNCE_NOT_AUTHORIZED;
            case PublishTrackHandler::Status::kNoSubscribers:
                return QUICR_PUBLISH_TRACK_HANDLER_STATUS_NO_SUBSCRIBERS;
            case PublishTrackHandler::Status::kUnsubscribed:
                return QUICR_PUBLISH_TRACK_HANDLER_STATUS_UNSUBSCRIBED;
            case PublishTrackHandler::Status::kDoneByFin:
                return QUICR_PUBLISH_TRACK_HANDLER_STATUS_DONE_BY_FIN;
            case PublishTrackHandler::Status::kSendingUnannounce:
                return QUICR_PUBLISH_TRACK_HANDLER_STATUS_SENDING_UNANNOUNCE;
            case PublishTrackHandler::Status::kSubscriptionUpdated:
                return QUICR_PUBLISH_TRACK_HANDLER_STATUS_SUBSCRIPTION_UPDATED;
            case PublishTrackHandler::Status::kNewGroupRequested:
                return QUICR_PUBLISH_TRACK_HANDLER_STATUS_NEW_GROUP_REQUESTED;
            case PublishTrackHandler::Status::kPendingPublishOk:
                return QUICR_PUBLISH_TRACK_HANDLER_STATUS_PENDING_PUBLISH_OK;
            case PublishTrackHandler::Status::kPaused:
                return QUICR_PUBLISH_TRACK_HANDLER_STATUS_PAUSED;
        }

        throw std::invalid_argument("Invalid publish track handler status value");
    }

    template<typename Cpp, typename C>
    Cpp to_cpp_type(const C&);

    template<>
    BytesSpan to_cpp_type<BytesSpan, quicr_byte_span_t>(const quicr_byte_span_t& bytes)
    {
        if (!bytes.data && bytes.size != 0) {
            throw std::invalid_argument("Byte span data is null");
        }

        return { bytes.data, bytes.size };
    }

    template<>
    Bytes to_cpp_type<Bytes, quicr_byte_span_t>(const quicr_byte_span_t& bytes)
    {
        const auto span = to_cpp_type<BytesSpan>(bytes);
        if (span.empty()) {
            return {};
        }

        return { span.begin(), span.end() };
    }

    template<>
    TrackNamespace to_cpp_type<TrackNamespace, quicr_track_namespace_t>(const quicr_track_namespace_t& track_namespace)
    {
        if (!track_namespace.entries && track_namespace.count != 0) {
            throw std::invalid_argument("Track namespace entries are null");
        }

        std::vector<BytesSpan> entries;
        entries.reserve(track_namespace.count);
        for (std::size_t i = 0; i < track_namespace.count; ++i) {
            entries.emplace_back(to_cpp_type<BytesSpan>(track_namespace.entries[i]));
        }

        return TrackNamespace{ std::span<const BytesSpan>{ entries } };
    }

    template<>
    FullTrackName to_cpp_type<FullTrackName, quicr_full_track_name_t>(const quicr_full_track_name_t& full_track_name)
    {
        return {
            to_cpp_type<TrackNamespace>(full_track_name.ns),
            to_cpp_type<Bytes>(full_track_name.track_name),
        };
    }

    template<>
    TrackMode to_cpp_type<TrackMode, quicr_track_mode_t>(const quicr_track_mode_t& track_mode)
    {
        switch (track_mode) {
            case QUICR_TRACK_MODE_DATAGRAM:
                return TrackMode::kDatagram;
            case QUICR_TRACK_MODE_STREAM:
                return TrackMode::kStream;
        }

        throw std::invalid_argument("Invalid track mode value");
    }

    template<>
    ObjectStatus to_cpp_type<ObjectStatus, quicr_object_status_t>(const quicr_object_status_t& status)
    {
        switch (status) {
            case QUICR_OBJECT_STATUS_AVAILABLE:
                return ObjectStatus::kAvailable;
            case QUICR_OBJECT_STATUS_DOES_NOT_EXIST:
                return ObjectStatus::kDoesNotExist;
            case QUICR_OBJECT_STATUS_END_OF_GROUP:
                return ObjectStatus::kEndOfGroup;
            case QUICR_OBJECT_STATUS_END_OF_TRACK:
                return ObjectStatus::kEndOfTrack;
            case QUICR_OBJECT_STATUS_END_OF_SUBGROUP:
                return ObjectStatus::kEndOfSubGroup;
        }

        throw std::invalid_argument("Invalid object status value");
    }

    template<>
    Extensions to_cpp_type<Extensions, quicr_extensions_t>(const quicr_extensions_t& extensions)
    {
        if (!extensions.entries && extensions.entry_count != 0) {
            throw std::invalid_argument("Extension entries are null");
        }

        Extensions result;
        for (std::size_t i = 0; i < extensions.entry_count; ++i) {
            const auto& entry = extensions.entries[i];
            if (!entry.values && entry.value_count != 0) {
                throw std::invalid_argument("Extension values are null");
            }

            auto [it, inserted] = result.try_emplace(entry.type);
            if (!inserted) {
                throw std::invalid_argument("Duplicate extension type");
            }

            auto& values = it->second;
            values.reserve(entry.value_count);
            for (std::size_t j = 0; j < entry.value_count; ++j) {
                values.emplace_back(to_cpp_type<Bytes>(entry.values[j]));
            }
        }

        return result;
    }

    template<>
    ObjectHeaders to_cpp_type<ObjectHeaders, quicr_object_headers_t>(const quicr_object_headers_t& headers)
    {
        ObjectHeaders result{};
        result.group_id = headers.group_id;
        result.object_id = headers.object_id;
        result.subgroup_id = headers.subgroup_id;
        result.payload_length = headers.payload_length;
        result.status = to_cpp_type<ObjectStatus>(headers.status);
        if (headers.has_priority) {
            result.priority = headers.priority;
        }
        if (headers.has_ttl) {
            result.ttl = headers.ttl;
        }
        if (headers.has_track_mode) {
            result.track_mode = to_cpp_type<TrackMode>(headers.track_mode);
        }
        if (headers.extensions) {
            result.extensions = to_cpp_type<Extensions>(*headers.extensions);
        }
        if (headers.immutable_extensions) {
            result.immutable_extensions = to_cpp_type<Extensions>(*headers.immutable_extensions);
        }

        return result;
    }

    template<>
    messages::SubgroupIdType to_cpp_type<messages::SubgroupIdType, quicr_subgroup_id_type_t>(
      const quicr_subgroup_id_type_t& type)
    {
        switch (type) {
            case QUICR_SUBGROUP_ID_TYPE_IS_ZERO:
                return messages::SubgroupIdType::kIsZero;
            case QUICR_SUBGROUP_ID_TYPE_SET_FROM_FIRST_OBJECT:
                return messages::SubgroupIdType::kSetFromFirstObject;
            case QUICR_SUBGROUP_ID_TYPE_EXPLICIT:
                return messages::SubgroupIdType::kExplicit;
            case QUICR_SUBGROUP_ID_TYPE_RESERVED:
                return messages::SubgroupIdType::kReserved;
        }

        throw std::invalid_argument("Invalid subgroup ID type value");
    }

    template<>
    messages::StreamHeaderProperties to_cpp_type<messages::StreamHeaderProperties, quicr_stream_header_properties_t>(
      const quicr_stream_header_properties_t& properties)
    {
        return {
            properties.extensions,   to_cpp_type<messages::SubgroupIdType>(properties.subgroup_id_mode),
            properties.end_of_group, properties.default_priority,
            properties.first_object,
        };
    }

    template<>
    messages::Location to_cpp_type<messages::Location, quicr_location_t>(const quicr_location_t& location)
    {
        return {
            .group = location.group,
            .object = location.object,
        };
    }

    template<>
    CongestionControl to_cpp_type<CongestionControl, quicr_congestion_control_t>(
      const quicr_congestion_control_t& congestion_control)
    {
        switch (congestion_control) {
            case QUICR_CONGESTION_CONTROL_BBR:
                return CongestionControl::kBbr;
            case QUICR_CONGESTION_CONTROL_C4:
                return CongestionControl::kC4;
            case QUICR_CONGESTION_CONTROL_CUBIC:
                return CongestionControl::kCubic;
            case QUICR_CONGESTION_CONTROL_NEW_RENO:
                return CongestionControl::kNewReno;
        }

        throw std::invalid_argument("Invalid congestion control value");
    }

    template<>
    TransportConfig to_cpp_type<TransportConfig, quicr_transport_config_t>(const quicr_transport_config_t& config)
    {
        TransportConfig result{};
        result.tls_cert_filename = config.tls_cert_filename ? config.tls_cert_filename : "";
        result.tls_key_filename = config.tls_key_filename ? config.tls_key_filename : "";
        result.time_queue_init_queue_size = config.time_queue_init_queue_size;
        result.time_queue_max_duration = config.time_queue_max_duration;
        result.time_queue_bucket_interval = config.time_queue_bucket_interval;
        result.time_queue_rx_size = config.time_queue_rx_size;
        result.debug = config.debug;
        result.quic_cwin_minimum = config.quic_cwin_minimum;
        result.quic_wifi_shadow_rtt_us = config.quic_wifi_shadow_rtt_us;
        result.idle_timeout_ms = config.idle_timeout_ms;
        result.congestion_control = to_cpp_type<CongestionControl>(config.congestion_control);
        result.quic_qlog_path = config.quic_qlog_path ? config.quic_qlog_path : "";
        result.quic_priority_limit = config.quic_priority_limit;
        result.max_connections = config.max_connections;
        result.ssl_keylog = config.ssl_keylog;
        result.socket_buffer_size = config.socket_buffer_size;
        result.callback_queue_size = config.callback_queue_size;
        result.metrics_sample_ms = config.metrics_sample_ms;
        result.initial_max_stream_data = config.initial_max_stream_data;
        result.initial_max_stream_id = config.initial_max_stream_id;
        result.quic_shards = config.quic_shards;
        result.use_af_xdp = config.use_af_xdp;

        return result;
    }

    template<>
    ClientConfig to_cpp_type<ClientConfig, quicr_client_config_t>(const quicr_client_config_t& config)
    {
        ClientConfig result{};
        result.endpoint_id = config.endpoint_id ? config.endpoint_id : "";
        result.transport_config = to_cpp_type<TransportConfig>(config.transport_config);
        result.unknown_stream_expiry_ms = config.unknown_stream_expiry_ms;
        result.connect_uri = config.connect_uri ? config.connect_uri : "";
        result.tick_service_sleep_delay_us = config.tick_service_sleep_delay_us;

        return result;
    }

    template<>
    ServerConfig to_cpp_type<ServerConfig, quicr_server_config_t>(const quicr_server_config_t& config)
    {
        ServerConfig result{};
        result.endpoint_id = config.endpoint_id ? config.endpoint_id : "";
        result.transport_config = to_cpp_type<TransportConfig>(config.transport_config);
        result.unknown_stream_expiry_ms = config.unknown_stream_expiry_ms;
        result.server_bind_ip = config.bind_ip ? config.bind_ip : "";
        result.server_port = config.port;
        result.tick_service_sleep_delay_us = config.tick_service_sleep_delay_us;

        return result;
    }
}

/*===========================================================================*/
// Implementations
/*===========================================================================*/

struct quicr_session
{
    std::weak_ptr<quicr::Session> session;
};

struct quicr_session_manager
{
    std::unique_ptr<quicr::SessionManager> impl;
    std::mutex sessions_mutex;
    std::unordered_map<quicr::Session*, std::unique_ptr<quicr_session_t>> sessions;

    quicr_session_t* GetOrCreateSessionHandle(const std::shared_ptr<quicr::Session>& session)
    {
        std::lock_guard lock(sessions_mutex);

        auto it = sessions.find(session.get());
        if (it != sessions.end()) {
            return it->second.get();
        }

        auto new_handle = std::make_unique<quicr_session_t>();
        new_handle->session = session;
        auto* handle = new_handle.get();
        sessions.emplace(session.get(), std::move(new_handle));
        return handle;
    }

    quicr_session_t* AddSessionHandle(const std::shared_ptr<quicr::Session>& session,
                                      std::unique_ptr<quicr_session_t> handle)
    {
        std::lock_guard lock(sessions_mutex);

        auto* result = handle.get();
        handle->session = session;
        sessions.emplace(session.get(), std::move(handle));
        return result;
    }

    quicr_session_t* FindSessionHandle(quicr::Session* session)
    {
        std::lock_guard lock(sessions_mutex);

        auto it = sessions.find(session);
        return it != sessions.end() ? it->second.get() : nullptr;
    }

    void RemoveSessionHandle(quicr::Session* session)
    {
        std::lock_guard lock(sessions_mutex);
        sessions.erase(session);
    }

    void ClearSessionHandles()
    {
        std::lock_guard lock(sessions_mutex);
        sessions.clear();
    }
};

struct quicr_track_handler
{
    std::shared_ptr<TrackHandler> handler;
};

struct quicr_publish_track_handler
{
    quicr_track_handler base;
};

/*===========================================================================*/
// C++ Adapters
/*===========================================================================*/

template<typename DerivedSessionCallbacks, typename CCallbacks>
    requires std::is_base_of_v<Session::Callbacks, DerivedSessionCallbacks>
class SessionCallbacksAdapter : public DerivedSessionCallbacks
{
  public:
    SessionCallbacksAdapter(quicr_session_t* owner, const CCallbacks& callbacks)
      : owner_(owner)
      , callbacks_(callbacks)
    {
    }

    SessionCallbacksAdapter(quicr_session_manager_t* manager, const CCallbacks& callbacks)
      : manager_(manager)
      , callbacks_(callbacks)
    {
    }

    void OnStreamClosed(std::uint64_t stream_id, StreamClosedFlag flag) override
    {
        if (!callbacks_.common.on_stream_closed) {
            return;
        }

        callbacks_.common.on_stream_closed(stream_id, to_c_type<quicr_stream_closed_flag_t>(flag));
    }

    void StatusChanged(const std::shared_ptr<Session>& session, Session::Status status) override
    {
        if (!callbacks_.common.status_changed) {
            return;
        }

        auto* owner = owner_ ? owner_ : manager_->GetOrCreateSessionHandle(session);
        callbacks_.common.status_changed(owner, to_c_type<quicr_session_status_t>(status), callbacks_.common.user_data);
    }

  private:
    quicr_session_t* owner_{ nullptr };
    quicr_session_manager_t* manager_{ nullptr };
    CCallbacks callbacks_;
};

class ClientCallbacksAdapter final
  : public SessionCallbacksAdapter<Session::ClientCallbacks, quicr_session_client_callbacks_t>
{
  public:
    using SessionCallbacksAdapter::SessionCallbacksAdapter;
};

class ServerCallbacksAdapter final
  : public SessionCallbacksAdapter<Session::ServerCallbacks, quicr_session_server_callbacks_t>
{
  public:
    using SessionCallbacksAdapter::SessionCallbacksAdapter;
};

class SessionManagerCallbacksAdapter final : public quicr::SessionManager::Callbacks
{
  public:
    SessionManagerCallbacksAdapter(quicr_session_manager_t* owner, const quicr_session_manager_callbacks_t& callbacks)
      : owner_(owner)
      , callbacks_(callbacks)
    {
    }

    void OnNewServerSession(const std::shared_ptr<quicr::Session>& session) override
    {
        if (!session) {
            return;
        }

        auto* handle = owner_->GetOrCreateSessionHandle(session);
        if (callbacks_.new_server_session) {
            callbacks_.new_server_session(owner_, handle, callbacks_.user_data);
        }
    }

    void OnSessionRemoved(const std::shared_ptr<quicr::Session>& session) override
    {
        if (!session) {
            return;
        }

        auto* handle = owner_->FindSessionHandle(session.get());
        if (callbacks_.session_removed) {
            callbacks_.session_removed(owner_, handle, callbacks_.user_data);
        }

        owner_->RemoveSessionHandle(session.get());
    }

  private:
    quicr_session_manager_t* owner_;
    quicr_session_manager_callbacks_t callbacks_;
};

extern "C"
{
    void quicr_transport_config_init(quicr_transport_config_t* config)
    {
        if (!config) {
            return;
        }

        *config = {};

        config->tls_cert_filename = "";
        config->tls_key_filename = "";
        config->time_queue_init_queue_size = 1000;
        config->time_queue_max_duration = 2000;
        config->time_queue_bucket_interval = 500;
        config->time_queue_rx_size = 1000;
        config->quic_cwin_minimum = 131072;
        config->idle_timeout_ms = 30000;
        config->congestion_control = QUICR_CONGESTION_CONTROL_BBR;
        config->max_connections = 1;
        config->callback_queue_size = 2000;
        config->metrics_sample_ms = 5000;
        config->initial_max_stream_id = 512;
        config->quic_shards = 1;
        config->use_af_xdp = true;
    }

    void quicr_client_config_init(quicr_client_config_t* config)
    {
        if (!config) {
            return;
        }

        *config = {};

        quicr_transport_config_init(&config->transport_config);

        config->unknown_stream_expiry_ms = 3000;
        config->tick_service_sleep_delay_us = 333;
    }

    void quicr_server_config_init(quicr_server_config_t* config)
    {
        if (!config) {
            return;
        }

        *config = {};

        quicr_transport_config_init(&config->transport_config);

        config->unknown_stream_expiry_ms = 3000;
        config->tick_service_sleep_delay_us = 333;
    }

    /*=======================================================================*/
    // Session
    /*=======================================================================*/

    quicr_status_t quicr_session_get_status(const quicr_session_t* session, quicr_session_status_t* status)
    {
        if (!session) {
            return QUICR_STATUS_INVALID_ARGUMENT;
        }

        auto session_impl = session->session.lock();
        if (!session_impl) {
            return QUICR_STATUS_INTERNAL_ERROR;
        }

        *status = to_c_type<quicr_session_status_t>(session_impl->GetStatus());

        return QUICR_STATUS_OK;
    }

    /*=======================================================================*/
    // Handlers
    /*=======================================================================*/

    void quicr_object_headers_init(quicr_object_headers_t* headers)
    {
        if (!headers) {
            return;
        }

        *headers = {};
    }

    void quicr_stream_header_properties_init(quicr_stream_header_properties_t* properties)
    {
        if (!properties) {
            return;
        }

        *properties = {};
    }

    void quicr_location_init(quicr_location_t* location)
    {
        if (!location) {
            return;
        }

        *location = {};
    }

    const quicr_track_handler_t* quicr_publish_track_handler_as_track_handler(
      const quicr_publish_track_handler_t* handler)
    {
        return std::addressof(handler->base);
    }

    quicr_status_t quicr_publish_track_handler_create(const quicr_full_track_name_t* ftn,
                                                      quicr_track_mode_t track_mode,
                                                      uint8_t publisher_priority,
                                                      uint8_t publish_object_priority,
                                                      uint32_t default_ttl,
                                                      const quicr_location_t* largest_location,
                                                      quicr_publish_track_handler_t** handler)
    {
        if (!handler || !ftn) {
            return QUICR_STATUS_INVALID_ARGUMENT;
        }

        *handler = new quicr_publish_track_handler();

        (*handler)->base.handler = PublishTrackHandler::Create(
          to_cpp_type<FullTrackName>(*ftn),
          to_cpp_type<TrackMode>(track_mode),
          publisher_priority,
          publish_object_priority,
          default_ttl,
          largest_location ? to_cpp_type<messages::Location>(*largest_location) : messages::Location{});

        return QUICR_STATUS_OK;
    }

    void quicr_publish_track_handler_destroy(quicr_publish_track_handler_t* handler)
    {
        if (!handler) {
            return;
        }

        delete handler;
        handler = nullptr;
    }

    bool quicr_publish_track_handler_can_publish(const quicr_publish_track_handler_t* handler)
    {
        if (!handler || !handler->base.handler) {
            return false;
        }

        return std::static_pointer_cast<PublishTrackHandler>(handler->base.handler)->CanPublish();
    }

    quicr_status_t quicr_publish_track_handler_get_status(const quicr_publish_track_handler_t* handler,
                                                          quicr_publish_track_handler_status_t* status)
    {
        if (!handler || !handler->base.handler || !status) {
            return QUICR_STATUS_INVALID_ARGUMENT;
        }

        const auto cpp_status = std::static_pointer_cast<PublishTrackHandler>(handler->base.handler)->GetStatus();
        *status = to_c_type<quicr_publish_track_handler_status_t>(cpp_status);

        return QUICR_STATUS_OK;
    }

    quicr_status_t quicr_publish_track_handler_publish_object(quicr_publish_track_handler_t* handler,
                                                              const quicr_object_headers_t* headers,
                                                              quicr_byte_span_t data,
                                                              const quicr_stream_header_properties_t* properties,
                                                              quicr_publish_object_status_t* status)
    {
        if (!handler || !headers || (data.size != 0 && data.data == nullptr)) {
            return QUICR_STATUS_INVALID_ARGUMENT;
        }

        const auto object_status =
          std::static_pointer_cast<PublishTrackHandler>(handler->base.handler)
            ->PublishObject(to_cpp_type<ObjectHeaders>(*headers),
                            to_cpp_type<BytesSpan>(data),
                            properties ? std::make_optional(to_cpp_type<messages::StreamHeaderProperties>(*properties))
                                       : std::nullopt);

        if (status) {
            *status = to_c_type<quicr_publish_object_status_t>(object_status);
        }

        return QUICR_STATUS_OK;
    }
}

/*=======================================================================*/
// Session Manager
/*=======================================================================*/

quicr_status_t
quicr_session_manager_create(const quicr_session_manager_callbacks_t* callbacks, quicr_session_manager_t** manager)
{
    if (!manager) {
        return QUICR_STATUS_INVALID_ARGUMENT;
    }

    *manager = new quicr_session_manager();

    auto mgr_callbacks = std::make_shared<SessionManagerCallbacksAdapter>(
      *manager, callbacks ? *callbacks : quicr_session_manager_callbacks_t{});
    (*manager)->impl = std::make_unique<quicr::SessionManager>(std::move(mgr_callbacks));

    return QUICR_STATUS_OK;
}

void
quicr_session_manager_destroy(quicr_session_manager_t* manager)
{
    if (!manager) {
        return;
    }

    manager->impl.reset();
    manager->ClearSessionHandles();

    delete manager;
    manager = nullptr;
}

quicr_status_t
quicr_session_manager_add_client(quicr_session_manager_t* manager,
                                 const quicr_client_config_t* config,
                                 const quicr_session_client_callbacks_t* callbacks,
                                 quicr_session_t** session)
{
    if (!manager || !config || !session) {
        return QUICR_STATUS_INVALID_ARGUMENT;
    }

    *session = nullptr;
    auto handle = std::make_unique<quicr_session_t>();
    auto adapter = std::make_shared<ClientCallbacksAdapter>(
      handle.get(), callbacks ? *callbacks : quicr_session_client_callbacks_t{});
    auto cpp_session = manager->impl->AddTransport(to_cpp_type<ClientConfig>(*config), std::move(adapter)).lock();
    if (!cpp_session) {
        return QUICR_STATUS_INTERNAL_ERROR;
    }

    *session = manager->AddSessionHandle(cpp_session, std::move(handle));

    return QUICR_STATUS_OK;
}

quicr_status_t
quicr_session_manager_add_server(quicr_session_manager_t* manager,
                                 const quicr_server_config_t* config,
                                 const quicr_session_server_callbacks_t* callbacks)
{
    if (!manager || !config) {
        return QUICR_STATUS_INVALID_ARGUMENT;
    }

    auto adapter =
      std::make_shared<ServerCallbacksAdapter>(manager, callbacks ? *callbacks : quicr_session_server_callbacks_t{});
    manager->impl->AddTransport(to_cpp_type<ServerConfig>(*config), std::move(adapter));

    return QUICR_STATUS_OK;
}

quicr_status_t
quicr_session_manager_add_handler(quicr_session_manager_t* manager,
                                  quicr_session_t* session,
                                  const quicr_track_handler_t* handler)
{
    if (!manager || !session || !session->session.lock() || !handler) {
        return QUICR_STATUS_INVALID_ARGUMENT;
    }

    manager->impl->AddHandler(session->session.lock(), handler->handler);

    return QUICR_STATUS_OK;
}
