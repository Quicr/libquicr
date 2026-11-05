// SPDX-FileCopyrightText: Copyright (c) 2026 Cisco Systems
// SPDX-License-Identifier: BSD-2-Clause

#include "quicr/quicr.h"

#include <quicr/session.h>
#include <quicr/session_callbacks.h>
#include <quicr/session_manager.h>

#include <limits>
#include <mutex>
#include <new>
#include <stdexcept>
#include <vector>

using namespace quicr;

namespace {
    /**
     * C API call wrapper that catches C++ exceptions and converts them to a quicr_status_t error code.
     */
    template<typename F>
    quicr_status_t SafeCall(F&& function) noexcept
    try {
        return function();
    } catch (const std::invalid_argument&) {
        return QUICR_STATUS_INVALID_ARGUMENT;
    } catch (const messages::ProtocolViolationException&) {
        return QUICR_STATUS_INVALID_ARGUMENT;
    } catch (const std::bad_alloc&) {
        return QUICR_STATUS_OUT_OF_MEMORY;
    } catch (...) {
        return QUICR_STATUS_INTERNAL_ERROR;
    }

    /**
     * Templated method to convert a given C++ type to q given C type.
     */
    template<typename C, typename Cpp>
    C ToCType(const Cpp&);

    template<>
    quicr_byte_span_t ToCType<quicr_byte_span_t, BytesSpan>(const BytesSpan& bytes)
    {
        return {
            .data = bytes.data(),
            .size = bytes.size(),
        };
    }

    template<>
    quicr_track_mode_t ToCType<quicr_track_mode_t, TrackMode>(const TrackMode& track_mode)
    {
        switch (track_mode) {
            case TrackMode::kDatagram:
                return QUICR_TRACK_MODE_DATAGRAM;
            case TrackMode::kStream:
                return QUICR_TRACK_MODE_STREAM;
        }

        throw std::invalid_argument("Invalid track mode value");
    }

    template<>
    quicr_object_status_t ToCType<quicr_object_status_t, ObjectStatus>(const ObjectStatus& status)
    {
        switch (status) {
            case ObjectStatus::kAvailable:
                return QUICR_OBJECT_STATUS_AVAILABLE;
            case ObjectStatus::kDoesNotExist:
                return QUICR_OBJECT_STATUS_DOES_NOT_EXIST;
            case ObjectStatus::kEndOfGroup:
                return QUICR_OBJECT_STATUS_END_OF_GROUP;
            case ObjectStatus::kEndOfTrack:
                return QUICR_OBJECT_STATUS_END_OF_TRACK;
            case ObjectStatus::kEndOfSubGroup:
                return QUICR_OBJECT_STATUS_END_OF_SUBGROUP;
        }

        throw std::invalid_argument("Invalid object status value");
    }

    template<>
    quicr_subgroup_id_type_t ToCType<quicr_subgroup_id_type_t, messages::SubgroupIdType>(
      const messages::SubgroupIdType& type)
    {
        switch (type) {
            case messages::SubgroupIdType::kIsZero:
                return QUICR_SUBGROUP_ID_TYPE_IS_ZERO;
            case messages::SubgroupIdType::kSetFromFirstObject:
                return QUICR_SUBGROUP_ID_TYPE_SET_FROM_FIRST_OBJECT;
            case messages::SubgroupIdType::kExplicit:
                return QUICR_SUBGROUP_ID_TYPE_EXPLICIT;
            case messages::SubgroupIdType::kReserved:
                return QUICR_SUBGROUP_ID_TYPE_RESERVED;
        }

        throw std::invalid_argument("Invalid subgroup ID type value");
    }

    template<>
    quicr_stream_header_properties_t ToCType<quicr_stream_header_properties_t, messages::StreamHeaderProperties>(
      const messages::StreamHeaderProperties& properties)
    {
        return {
            .extensions = properties.extensions,
            .subgroup_id_mode = ToCType<quicr_subgroup_id_type_t>(properties.subgroup_id_mode),
            .end_of_group = properties.end_of_group,
            .default_priority = properties.default_priority,
            .first_object = properties.first_object,
        };
    }

    template<>
    quicr_object_headers_t ToCType<quicr_object_headers_t, ObjectHeaders>(const ObjectHeaders& headers)
    {
        return {
            .group_id = headers.group_id,
            .object_id = headers.object_id,
            .subgroup_id = headers.subgroup_id,
            .payload_length = headers.payload_length,
            .status = ToCType<quicr_object_status_t>(headers.status),
            .has_priority = headers.priority.has_value(),
            .priority = headers.priority.value_or(0),
            .has_ttl = headers.ttl.has_value(),
            .ttl = headers.ttl.value_or(0),
            .has_track_mode = headers.track_mode.has_value(),
            .track_mode = headers.track_mode ? ToCType<quicr_track_mode_t>(*headers.track_mode)
                                             : static_cast<quicr_track_mode_t>(QUICR_TRACK_MODE_DATAGRAM),
            .extensions = nullptr,
            .immutable_extensions = nullptr,
        };
    }

    class CExtensionsView
    {
      public:
        explicit CExtensionsView(const Extensions& extensions)
        {
            values_.reserve(extensions.size());
            entries_.reserve(extensions.size());

            for (const auto& [type, values] : extensions) {
                auto& c_values = values_.emplace_back();
                c_values.reserve(values.size());
                for (const auto& value : values) {
                    c_values.push_back(ToCType<quicr_byte_span_t>(BytesSpan{ value }));
                }

                entries_.push_back({
                  .type = type,
                  .values = c_values.data(),
                  .count = c_values.size(),
                });
            }

            value_ = {
                .entries = entries_.data(),
                .count = entries_.size(),
            };
        }

        const quicr_extensions_t* Get() const noexcept { return &value_; }

      private:
        std::vector<std::vector<quicr_byte_span_t>> values_;
        std::vector<quicr_extension_entry_t> entries_;
        quicr_extensions_t value_{};
    };

    template<>
    quicr_stream_closed_flag_t ToCType<quicr_stream_closed_flag_t, StreamClosedFlag>(const StreamClosedFlag& flag)
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
    quicr_session_status_t ToCType<quicr_session_status_t, Session::Status>(const Session::Status& status)
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
    quicr_publish_object_status_t ToCType<quicr_publish_object_status_t, PublishTrackHandler::PublishObjectStatus>(
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
    quicr_publish_track_handler_status_t ToCType<quicr_publish_track_handler_status_t, PublishTrackHandler::Status>(
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
    Cpp ToCppType(const C&);

    template<>
    BytesSpan ToCppType<BytesSpan, quicr_byte_span_t>(const quicr_byte_span_t& bytes)
    {
        if (!bytes.data && bytes.size != 0) {
            throw std::invalid_argument("Byte span data is null");
        }

        return { bytes.data, bytes.size };
    }

    template<>
    Bytes ToCppType<Bytes, quicr_byte_span_t>(const quicr_byte_span_t& bytes)
    {
        const auto span = ToCppType<BytesSpan>(bytes);
        if (span.empty()) {
            return {};
        }

        return { span.begin(), span.end() };
    }

    template<>
    TrackNamespace ToCppType<TrackNamespace, quicr_track_namespace_t>(const quicr_track_namespace_t& track_namespace)
    {
        if (!track_namespace.entries && track_namespace.count != 0) {
            throw std::invalid_argument("Track namespace entries are null");
        }

        std::vector<BytesSpan> entries;
        entries.reserve(track_namespace.count);
        for (std::size_t i = 0; i < track_namespace.count; ++i) {
            entries.emplace_back(ToCppType<BytesSpan>(track_namespace.entries[i]));
        }

        return TrackNamespace{ std::span<const BytesSpan>{ entries } };
    }

    template<>
    FullTrackName ToCppType<FullTrackName, quicr_full_track_name_t>(const quicr_full_track_name_t& full_track_name)
    {
        return {
            ToCppType<TrackNamespace>(full_track_name.ns),
            ToCppType<Bytes>(full_track_name.track_name),
        };
    }

    template<>
    TrackMode ToCppType<TrackMode, quicr_track_mode_t>(const quicr_track_mode_t& track_mode)
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
    ObjectStatus ToCppType<ObjectStatus, quicr_object_status_t>(const quicr_object_status_t& status)
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
    Extensions ToCppType<Extensions, quicr_extensions_t>(const quicr_extensions_t& extensions)
    {
        if (!extensions.entries && extensions.count != 0) {
            throw std::invalid_argument("Extension entries are null");
        }

        Extensions result;
        for (std::size_t i = 0; i < extensions.count; ++i) {
            const auto& entry = extensions.entries[i];
            if (!entry.values && entry.count != 0) {
                throw std::invalid_argument("Extension values are null");
            }

            auto [it, inserted] = result.try_emplace(entry.type);
            if (!inserted) {
                throw std::invalid_argument("Duplicate extension type");
            }

            auto& values = it->second;
            values.reserve(entry.count);
            for (std::size_t j = 0; j < entry.count; ++j) {
                values.emplace_back(ToCppType<Bytes>(entry.values[j]));
            }
        }

        return result;
    }

    template<>
    ObjectHeaders ToCppType<ObjectHeaders, quicr_object_headers_t>(const quicr_object_headers_t& headers)
    {
        ObjectHeaders result{};
        result.group_id = headers.group_id;
        result.object_id = headers.object_id;
        result.subgroup_id = headers.subgroup_id;
        result.payload_length = headers.payload_length;
        result.status = ToCppType<ObjectStatus>(headers.status);
        if (headers.has_priority) {
            result.priority = headers.priority;
        }
        if (headers.has_ttl) {
            result.ttl = headers.ttl;
        }
        if (headers.has_track_mode) {
            result.track_mode = ToCppType<TrackMode>(headers.track_mode);
        }
        if (headers.extensions) {
            result.extensions = ToCppType<Extensions>(*headers.extensions);
        }
        if (headers.immutable_extensions) {
            result.immutable_extensions = ToCppType<Extensions>(*headers.immutable_extensions);
        }

        return result;
    }

    template<>
    messages::SubgroupIdType ToCppType<messages::SubgroupIdType, quicr_subgroup_id_type_t>(
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
    messages::StreamHeaderProperties ToCppType<messages::StreamHeaderProperties, quicr_stream_header_properties_t>(
      const quicr_stream_header_properties_t& properties)
    {
        return {
            properties.extensions,   ToCppType<messages::SubgroupIdType>(properties.subgroup_id_mode),
            properties.end_of_group, properties.default_priority,
            properties.first_object,
        };
    }

    template<>
    messages::Location ToCppType<messages::Location, quicr_location_t>(const quicr_location_t& location)
    {
        return {
            .group = location.group,
            .object = location.object,
        };
    }

    template<>
    messages::GroupOrder ToCppType<messages::GroupOrder, quicr_group_order_t>(const quicr_group_order_t& group_order)
    {
        switch (group_order) {
            case QUICR_GROUP_ORDER_ASCENDING:
                return messages::GroupOrder::kAscending;
            case QUICR_GROUP_ORDER_DESCENDING:
                return messages::GroupOrder::kDescending;
        }

        throw std::invalid_argument("Invalid group order value");
    }

    template<>
    messages::LocationFilter ToCppType<messages::LocationFilter, quicr_subscription_filter_t>(
      const quicr_subscription_filter_t& filter)
    {
        if (filter.start_location.object != 0) {
            throw std::invalid_argument("Subscription filters do not support object locations");
        }

        // TODO: Fix this implementation once the PR fixing SubFIlter is in.
        switch (filter.type) {
            case QUICR_SUBSCRIPTION_FILTER_TYPE_ABSOLUTE_START:
                return {
                    .range = {
                      .start = filter.start_location.group,
                      .end = std::nullopt,
                    },
                };
            case QUICR_SUBSCRIPTION_FILTER_TYPE_ABSOLUTE_RANGE:
                if (filter.end_group_delta > std::numeric_limits<std::uint64_t>::max() - filter.start_location.group) {
                    throw std::invalid_argument("Subscription filter range overflows");
                }

                return {
                    .range = {
                      .start = filter.start_location.group,
                      .end = filter.start_location.group + filter.end_group_delta,
                    },
                };
            case QUICR_SUBSCRIPTION_FILTER_TYPE_NEXT_GROUP_START:
            case QUICR_SUBSCRIPTION_FILTER_TYPE_LARGEST_OBJECT:
                throw std::invalid_argument("Subscription filter type is not supported by the C++ filter model");
        }

        throw std::invalid_argument("Invalid subscription filter type value");
    }

    template<>
    messages::TrackFilter ToCppType<messages::TrackFilter, quicr_track_filter_t>(const quicr_track_filter_t& filter)
    {
        return {
            .property_type = filter.property_type,
            .max_tracks_selected = filter.max_tracks_selected,
            .timeout = filter.timeout,
        };
    }

    template<>
    messages::Filter ToCppType<messages::Filter, quicr_filter_t>(const quicr_filter_t& filter)
    {
        switch (filter.type) {
            case QUICR_FILTER_TYPE_NONE:
                return std::monostate{};
            case QUICR_FILTER_TYPE_SUBSCRIPTION_FILTER:
                return ToCppType<messages::LocationFilter>(filter.subscription);
            case QUICR_FILTER_TYPE_TRACK_FILTER:
                return ToCppType<messages::TrackFilter>(filter.track);
        }

        throw std::invalid_argument("Invalid filter type value");
    }

    template<>
    CongestionControl ToCppType<CongestionControl, quicr_congestion_control_t>(
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
    TransportConfig ToCppType<TransportConfig, quicr_transport_config_t>(const quicr_transport_config_t& config)
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
        result.congestion_control = ToCppType<CongestionControl>(config.congestion_control);
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
    ClientConfig ToCppType<ClientConfig, quicr_client_config_t>(const quicr_client_config_t& config)
    {
        ClientConfig result{};
        result.endpoint_id = config.endpoint_id ? config.endpoint_id : "";
        result.transport_config = ToCppType<TransportConfig>(config.transport_config);
        result.unknown_stream_expiry_ms = config.unknown_stream_expiry_ms;
        result.connect_uri = config.connect_uri ? config.connect_uri : "";
        result.tick_service_sleep_delay_us = config.tick_service_sleep_delay_us;

        return result;
    }

    template<>
    ServerConfig ToCppType<ServerConfig, quicr_server_config_t>(const quicr_server_config_t& config)
    {
        ServerConfig result{};
        result.endpoint_id = config.endpoint_id ? config.endpoint_id : "";
        result.transport_config = ToCppType<TransportConfig>(config.transport_config);
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
    std::weak_ptr<quicr::Session> impl;
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
        new_handle->impl = session;

        auto* handle = new_handle.get();
        sessions.emplace(session.get(), std::move(new_handle));

        return handle;
    }

    quicr_session_t* AddSessionHandle(const std::shared_ptr<quicr::Session>& session,
                                      std::unique_ptr<quicr_session_t> handle)
    {
        std::lock_guard lock(sessions_mutex);

        auto* result = handle.get();
        handle->impl = session;
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

struct quicr_publish_track_handler
{
    std::shared_ptr<PublishTrackHandler> impl;
};

struct quicr_subscribe_track_handler
{
    std::shared_ptr<SubscribeTrackHandler> impl;
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

        callbacks_.common.on_stream_closed(stream_id, ToCType<quicr_stream_closed_flag_t>(flag));
    }

    void StatusChanged(const std::shared_ptr<Session>& session, Session::Status status) override
    {
        if (!callbacks_.common.status_changed) {
            return;
        }

        auto* owner = owner_ ? owner_ : manager_->GetOrCreateSessionHandle(session);
        callbacks_.common.status_changed(owner, ToCType<quicr_session_status_t>(status), callbacks_.common.user_data);
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

class SubscribeTrackHandlerAdapter : public quicr::SubscribeTrackHandler
{
  public:
    SubscribeTrackHandlerAdapter(quicr_subscribe_track_handler* owner,
                                 const quicr_subscribe_track_handler_callbacks_t& callbacks,
                                 const FullTrackName& full_track_name,
                                 std::uint8_t priority,
                                 std::optional<messages::GroupOrder> group_order,
                                 const messages::Filter& filter = std::monostate{},
                                 const std::optional<JoiningFetch>& joining_fetch = std::nullopt,
                                 bool publisher_initiated = false)
      : SubscribeTrackHandler(full_track_name, priority, group_order, filter, joining_fetch, publisher_initiated)
      , owner_(owner)
      , callbacks_(callbacks)
    {
    }

    void ObjectReceived(const ObjectHeaders& headers,
                        BytesSpan data,
                        std::optional<messages::StreamHeaderProperties> properties) override
    {
        if (!owner_ || !callbacks_.object_received || data.empty()) {
            return;
        }

        auto c_headers = ToCType<quicr_object_headers_t>(headers);
        std::optional<CExtensionsView> extensions;
        std::optional<CExtensionsView> immutable_extensions;
        if (headers.extensions) {
            extensions.emplace(*headers.extensions);
            c_headers.extensions = extensions->Get();
        }
        if (headers.immutable_extensions) {
            immutable_extensions.emplace(*headers.immutable_extensions);
            c_headers.immutable_extensions = immutable_extensions->Get();
        }

        quicr_stream_header_properties_t c_properties;
        if (properties.has_value()) {
            c_properties = ToCType<quicr_stream_header_properties_t>(*properties);
        }

        callbacks_.object_received(std::addressof(c_headers),
                                   ToCType<quicr_byte_span_t>(data),
                                   properties.has_value() ? std::addressof(c_properties) : nullptr,
                                   callbacks_.user_data);
    }

  private:
    quicr_subscribe_track_handler* owner_;
    quicr_subscribe_track_handler_callbacks_t callbacks_;
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
        return SafeCall([&] {
            if (!session || !status) {
                return QUICR_STATUS_INVALID_ARGUMENT;
            }

            auto session_impl = session->impl.lock();
            if (!session_impl) {
                return QUICR_STATUS_INTERNAL_ERROR;
            }

            *status = ToCType<quicr_session_status_t>(session_impl->GetStatus());

            return QUICR_STATUS_OK;
        });
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

    quicr_status_t quicr_publish_track_handler_create(const quicr_full_track_name_t* ftn,
                                                      quicr_track_mode_t track_mode,
                                                      uint8_t publisher_priority,
                                                      uint8_t publish_object_priority,
                                                      uint32_t default_ttl,
                                                      const quicr_location_t* largest_location,
                                                      quicr_publish_track_handler_t** handler)
    {
        return SafeCall([&] {
            if (!handler || !ftn) {
                return QUICR_STATUS_INVALID_ARGUMENT;
            }

            *handler = nullptr;
            auto result = std::make_unique<quicr_publish_track_handler_t>();

            result->impl = PublishTrackHandler::Create(
              ToCppType<FullTrackName>(*ftn),
              ToCppType<TrackMode>(track_mode),
              publisher_priority,
              publish_object_priority,
              default_ttl,
              largest_location ? ToCppType<messages::Location>(*largest_location) : messages::Location{});

            *handler = result.release();
            return QUICR_STATUS_OK;
        });
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
        if (!handler || !handler->impl) {
            return false;
        }

        return handler->impl->CanPublish();
    }

    quicr_status_t quicr_publish_track_handler_get_status(const quicr_publish_track_handler_t* handler,
                                                          quicr_publish_track_handler_status_t* status)
    {
        return SafeCall([&] {
            if (!handler || !handler->impl || !status) {
                return QUICR_STATUS_INVALID_ARGUMENT;
            }

            const auto cpp_status = handler->impl->GetStatus();
            *status = ToCType<quicr_publish_track_handler_status_t>(cpp_status);

            return QUICR_STATUS_OK;
        });
    }

    quicr_status_t quicr_publish_track_handler_publish_object(quicr_publish_track_handler_t* handler,
                                                              const quicr_object_headers_t* headers,
                                                              quicr_byte_span_t data,
                                                              const quicr_stream_header_properties_t* properties,
                                                              quicr_publish_object_status_t* status)
    {
        return SafeCall([&] {
            if (!handler || !handler->impl || !headers || (data.size != 0 && data.data == nullptr)) {
                return QUICR_STATUS_INVALID_ARGUMENT;
            }

            const auto object_status = handler->impl->PublishObject(
              ToCppType<ObjectHeaders>(*headers),
              ToCppType<BytesSpan>(data),
              properties ? std::make_optional(ToCppType<messages::StreamHeaderProperties>(*properties)) : std::nullopt);

            if (status) {
                *status = ToCType<quicr_publish_object_status_t>(object_status);
            }

            return QUICR_STATUS_OK;
        });
    }

    QUICR_C_API void quicr_subscription_filter_init(quicr_subscription_filter_t* filter)
    {
        if (!filter) {
            return;
        }

        *filter = {};
    }

    QUICR_C_API void quicr_track_filter_init(quicr_track_filter_t* filter)
    {
        if (!filter) {
            return;
        }

        *filter = {};
    }

    QUICR_C_API quicr_status_t
    quicr_subscribe_track_handler_create(const quicr_subscribe_track_handler_callbacks_t* callbacks,
                                         const quicr_full_track_name_t* ftn,
                                         uint8_t priority,
                                         quicr_group_order_t group_order,
                                         const quicr_filter_t* filter,
                                         quicr_subscribe_track_handler_t** handler)
    {
        return SafeCall([&] {
            if (!handler || !ftn) {
                return QUICR_STATUS_INVALID_ARGUMENT;
            }

            *handler = nullptr;
            auto result = std::make_unique<quicr_subscribe_track_handler>();
            result->impl = std::make_shared<SubscribeTrackHandlerAdapter>(
              result.get(),
              callbacks ? *callbacks : quicr_subscribe_track_handler_callbacks_t{},
              ToCppType<FullTrackName>(*ftn),
              priority,
              ToCppType<messages::GroupOrder>(group_order),
              filter ? ToCppType<messages::Filter>(*filter) : messages::Filter{});

            *handler = result.release();
            return QUICR_STATUS_OK;
        });
    }

    QUICR_C_API void quicr_subscribe_track_handler_destroy(quicr_subscribe_track_handler_t* handler)
    {
        if (!handler) {
            return;
        }

        delete handler;
        handler = nullptr;
    }
}

/*=======================================================================*/
// Session Manager
/*=======================================================================*/

quicr_status_t
quicr_session_manager_create(const quicr_session_manager_callbacks_t* callbacks, quicr_session_manager_t** manager)
{
    return SafeCall([&] {
        if (!manager) {
            return QUICR_STATUS_INVALID_ARGUMENT;
        }

        *manager = nullptr;
        auto result = std::make_unique<quicr_session_manager_t>();

        auto mgr_callbacks = std::make_shared<SessionManagerCallbacksAdapter>(
          result.get(), callbacks ? *callbacks : quicr_session_manager_callbacks_t{});
        result->impl = std::make_unique<quicr::SessionManager>(std::move(mgr_callbacks));

        *manager = result.release();
        return QUICR_STATUS_OK;
    });
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
    return SafeCall([&] {
        if (!manager || !manager->impl || !config || !session) {
            return QUICR_STATUS_INVALID_ARGUMENT;
        }

        *session = nullptr;
        auto handle = std::make_unique<quicr_session_t>();
        auto adapter = std::make_shared<ClientCallbacksAdapter>(
          handle.get(), callbacks ? *callbacks : quicr_session_client_callbacks_t{});
        auto cpp_session = manager->impl->AddTransport(ToCppType<ClientConfig>(*config), std::move(adapter)).lock();
        if (!cpp_session) {
            return QUICR_STATUS_INTERNAL_ERROR;
        }

        *session = manager->AddSessionHandle(cpp_session, std::move(handle));

        return QUICR_STATUS_OK;
    });
}

quicr_status_t
quicr_session_manager_add_server(quicr_session_manager_t* manager,
                                 const quicr_server_config_t* config,
                                 const quicr_session_server_callbacks_t* callbacks)
{
    return SafeCall([&] {
        if (!manager || !manager->impl || !config) {
            return QUICR_STATUS_INVALID_ARGUMENT;
        }

        auto adapter = std::make_shared<ServerCallbacksAdapter>(
          manager, callbacks ? *callbacks : quicr_session_server_callbacks_t{});
        manager->impl->AddTransport(ToCppType<ServerConfig>(*config), std::move(adapter));

        return QUICR_STATUS_OK;
    });
}

quicr_status_t
quicr_session_manager_add_publish_track_handler(quicr_session_manager_t* manager,
                                                quicr_session_t* session,
                                                const quicr_publish_track_handler_t* handler)
{
    return SafeCall([&] {
        if (!manager || !manager->impl || !session || !handler || !handler->impl) {
            return QUICR_STATUS_INVALID_ARGUMENT;
        }

        auto session_impl = session->impl.lock();
        if (!session_impl) {
            return QUICR_STATUS_INVALID_ARGUMENT;
        }

        manager->impl->AddHandler(session_impl, handler->impl);

        return QUICR_STATUS_OK;
    });
}

QUICR_C_API quicr_status_t
quicr_session_manager_remove_publish_track_handler(quicr_session_manager_t* manager,
                                                   quicr_session_t* session,
                                                   const quicr_publish_track_handler_t* handler)
{
    return SafeCall([&] {
        if (!manager || !manager->impl || !session || !handler || !handler->impl) {
            return QUICR_STATUS_INVALID_ARGUMENT;
        }

        auto session_impl = session->impl.lock();
        if (!session_impl) {
            return QUICR_STATUS_INVALID_ARGUMENT;
        }

        manager->impl->RemoveHandler(session_impl, handler->impl);

        return QUICR_STATUS_OK;
    });
}
