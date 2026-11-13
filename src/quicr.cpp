// SPDX-FileCopyrightText: Copyright (c) 2026 Cisco Systems
// SPDX-License-Identifier: BSD-2-Clause

#include "quicr/quicr.h"

#include <quicr/handlers/forwarding_subscribe_track_handler.h>
#include <quicr/session.h>
#include <quicr/session_callbacks.h>
#include <quicr/session_manager.h>

#include <atomic>
#include <chrono>
#include <cstring>
#include <functional>
#include <iterator>
#include <limits>
#include <mutex>
#include <new>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <variant>
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

    template<typename E>
    Unexpected<Error<E>> CallbackInternalError() noexcept
    {
        return Error<E>{ E::kInternalError, std::nullopt };
    }

    template<typename F, typename R>
    auto SafeCallback(F&& function, R&& fallback) noexcept -> std::invoke_result_t<F>
    {
        try {
            return function();
        } catch (...) {
            return std::forward<R>(fallback);
        }
    }

    template<typename F, typename... Args>
        requires std::is_void_v<std::invoke_result_t<F, Args...>>
    void SafeCallback(F&& function, Args&&... args) noexcept
    {
        try {
            std::invoke(std::forward<F>(function), std::forward<Args>(args)...);
        } catch (...) {
        }
    }

    template<typename C, typename Cpp>
    C ToCType(const Cpp&);

    template<typename Cpp, typename C>
    Cpp ToCppType(const C&);
}

struct quicr_track_namespace
{
    const TrackNamespace& Get() const
    {
        if (std::holds_alternative<TrackNamespace>(impl)) {
            return std::get<TrackNamespace>(impl);
        }

        return std::get<std::reference_wrapper<const TrackNamespace>>(impl).get();
    }

    std::variant<TrackNamespace, std::reference_wrapper<const TrackNamespace>> impl;
};

struct quicr_full_track_name
{
    const FullTrackName& Get() const
    {
        if (std::holds_alternative<FullTrackName>(impl)) {
            return std::get<FullTrackName>(impl);
        }

        return std::get<std::reference_wrapper<const FullTrackName>>(impl).get();
    }

    std::variant<FullTrackName, std::reference_wrapper<const FullTrackName>> impl;
};

struct quicr_track_extensions
{
    const messages::TrackExtensions& Get() const
    {
        if (std::holds_alternative<messages::TrackExtensions>(impl)) {
            return std::get<messages::TrackExtensions>(impl);
        }

        return std::get<std::reference_wrapper<const messages::TrackExtensions>>(impl).get();
    }

    messages::TrackExtensions& GetMutable()
    {
        if (!std::holds_alternative<messages::TrackExtensions>(impl)) {
            throw std::invalid_argument("Borrowed track extensions are read-only");
        }

        return std::get<messages::TrackExtensions>(impl);
    }

    std::variant<messages::TrackExtensions, std::reference_wrapper<const messages::TrackExtensions>> impl;
};

struct quicr_publish_attributes
{
    const PublishAttributes& impl;
    quicr_full_track_name_t full_track_name;
    quicr_track_extensions_t track_extensions;
};

struct quicr_subscribe_attributes
{
    const SubscribeAttributes& impl;
};

struct quicr_subscribe_namespace_attributes
{
    const SubscribeNamespaceAttributes& impl;
};

struct quicr_publish_response
{
    std::optional<std::uint8_t> subscriber_priority;
    std::optional<messages::GroupOrder> group_order;
    messages::Filter filter;
    std::optional<bool> forward;
    std::optional<std::chrono::milliseconds> subgroup_delivery_timeout;
    std::optional<std::chrono::milliseconds> object_delivery_timeout;
    std::optional<std::uint64_t> new_group_request_id;
    std::shared_ptr<SubscribeTrackHandler> impl;
};

namespace {
    template<>
    quicr_byte_span_t ToCType<quicr_byte_span_t, BytesSpan>(const BytesSpan& bytes)
    {
        return {
            .data = bytes.data(),
            .size = bytes.size(),
        };
    }

    template<>
    quicr_min_max_avg_t ToCType<quicr_min_max_avg_t, MinMaxAvg>(const MinMaxAvg& metrics)
    {
        return {
            .min = metrics.min,
            .max = metrics.max,
            .avg = metrics.avg,
            .value_sum = metrics.value_sum,
            .value_count = metrics.value_count,
        };
    }

    template<>
    quicr_subscribe_track_metrics_t ToCType<quicr_subscribe_track_metrics_t, SubscribeTrackMetrics>(
      const SubscribeTrackMetrics& metrics)
    {
        return {
            .last_sample_time = metrics.last_sample_time,
            .bytes_received = metrics.bytes_received,
            .objects_received = metrics.objects_received,
        };
    }

    template<>
    quicr_publish_track_metrics_t ToCType<quicr_publish_track_metrics_t, PublishTrackMetrics>(
      const PublishTrackMetrics& metrics)
    {
        return {
            .last_sample_time = metrics.last_sample_time,
            .bytes_published = metrics.bytes_published,
            .objects_published = metrics.objects_published,
            .objects_dropped_not_ok = metrics.objects_dropped_not_ok,
            .quic = {
              .tx_buffer_drops = metrics.quic.tx_buffer_drops,
              .tx_queue_discards = metrics.quic.tx_queue_discards,
              .tx_queue_expired = metrics.quic.tx_queue_expired,
              .tx_delayed_callback = metrics.quic.tx_delayed_callback,
              .tx_queue_size = ToCType<quicr_min_max_avg_t>(metrics.quic.tx_queue_size),
              .tx_callback_ms = ToCType<quicr_min_max_avg_t>(metrics.quic.tx_callback_ms),
              .tx_object_duration_us = ToCType<quicr_min_max_avg_t>(metrics.quic.tx_object_duration_us),
            },
        };
    }

    template<>
    quicr_track_namespace_t ToCType<quicr_track_namespace_t, TrackNamespace>(const TrackNamespace& track_namespace)
    {
        return { .impl = std::cref(track_namespace) };
    }

    template<>
    quicr_full_track_name_t ToCType<quicr_full_track_name_t, FullTrackName>(const FullTrackName& full_track_name)
    {
        return { .impl = std::cref(full_track_name) };
    }

    template<>
    quicr_publish_attributes ToCType<quicr_publish_attributes, PublishAttributes>(const PublishAttributes& attributes)
    {
        return {
            .impl = attributes,
            .full_track_name = ToCType<quicr_full_track_name_t>(attributes.track_full_name),
            .track_extensions = { .impl = std::cref(attributes.track_properties) },
        };
    }

    template<>
    quicr_setup_attributes ToCType<quicr_setup_attributes, SetupAttributes>(const SetupAttributes& attributes)
    {
        return {
            .endpoint_id = {
              .data = reinterpret_cast<const std::uint8_t*>(attributes.endpoint_id.data()),
              .size = attributes.endpoint_id.size(),
            },
        };
    }

    template<>
    quicr_publish_namespace_attributes ToCType<quicr_publish_namespace_attributes, PublishNamespaceAttributes>(
      const PublishNamespaceAttributes& attributes)
    {
        return { .request_id = attributes.request_id };
    }

    template<>
    quicr_subscribe_attributes ToCType<quicr_subscribe_attributes, SubscribeAttributes>(
      const SubscribeAttributes& attributes)
    {
        return { .impl = attributes };
    }

    template<>
    quicr_subscribe_namespace_attributes ToCType<quicr_subscribe_namespace_attributes, SubscribeNamespaceAttributes>(
      const SubscribeNamespaceAttributes& attributes)
    {
        return { .impl = attributes };
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
    quicr_location_t ToCType<quicr_location_t, messages::Location>(const messages::Location& location)
    {
        return {
            .group = location.group,
            .object = location.object,
        };
    }

    template<>
    quicr_group_order_t ToCType<quicr_group_order_t, messages::GroupOrder>(const messages::GroupOrder& group_order)
    {
        switch (group_order) {
            case messages::GroupOrder::kAscending:
                return QUICR_GROUP_ORDER_ASCENDING;
            case messages::GroupOrder::kDescending:
                return QUICR_GROUP_ORDER_DESCENDING;
        }

        throw std::invalid_argument("Invalid group order value");
    }

    template<>
    quicr_standalone_fetch_attributes ToCType<quicr_standalone_fetch_attributes, StandaloneFetchAttributes>(
      const StandaloneFetchAttributes& attributes)
    {
        return {
            .priority = attributes.priority,
            .group_order = attributes.group_order ? ToCType<quicr_group_order_t>(*attributes.group_order)
                                                  : static_cast<quicr_group_order_t>(
                                                      QUICR_GROUP_ORDER_UNSPECIFIED),
            .publisher_default_group_order =
              ToCType<quicr_group_order_t>(attributes.publisher_default_group_order),
            .start_location = ToCType<quicr_location_t>(attributes.start_location),
            .end_location = {
              .group = attributes.end_location.group,
              .has_object = attributes.end_location.object.has_value(),
              .object = attributes.end_location.object.value_or(0),
            },
        };
    }

    template<>
    quicr_joining_fetch_attributes ToCType<quicr_joining_fetch_attributes, JoiningFetchAttributes>(
      const JoiningFetchAttributes& attributes)
    {
        return {
            .priority = attributes.priority,
            .group_order = attributes.group_order ? ToCType<quicr_group_order_t>(*attributes.group_order)
                                                  : static_cast<quicr_group_order_t>(QUICR_GROUP_ORDER_UNSPECIFIED),
            .publisher_default_group_order = ToCType<quicr_group_order_t>(attributes.publisher_default_group_order),
            .joining_request_id = attributes.joining_request_id,
            .relative = attributes.relative,
            .joining_start = attributes.joining_start,
        };
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
            case PublishTrackHandler::PublishObjectStatus::kSubgroupClosed:
                return QUICR_PUBLISH_OBJECT_STATUS_SUBGROUP_CLOSED;
            case PublishTrackHandler::PublishObjectStatus::kFetchClosed:
                return QUICR_PUBLISH_OBJECT_STATUS_FETCH_CLOSED;
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
        return track_namespace.Get();
    }

    template<>
    FullTrackName ToCppType<FullTrackName, quicr_full_track_name_t>(const quicr_full_track_name_t& full_track_name)
    {
        return full_track_name.Get();
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
    messages::SubscriptionFilter ToCppType<messages::SubscriptionFilter, quicr_subscription_filter_t>(
      const quicr_subscription_filter_t& filter)
    {
        switch (filter.type) {
            case QUICR_SUBSCRIPTION_FILTER_TYPE_NEXT_GROUP_START:
                return {
                    .type = messages::SubscriptionFilterType::kNextGroupStart,
                };
            case QUICR_SUBSCRIPTION_FILTER_TYPE_LARGEST_OBJECT:
                return {
                    .type = messages::SubscriptionFilterType::kLargestObject,
                };
            case QUICR_SUBSCRIPTION_FILTER_TYPE_ABSOLUTE_START:
                return {
                    .type = messages::SubscriptionFilterType::kAbsoluteStart,
                    .start_location = ToCppType<messages::Location>(filter.start_location),
                };
            case QUICR_SUBSCRIPTION_FILTER_TYPE_ABSOLUTE_RANGE:
                if (filter.end_group_delta > std::numeric_limits<std::uint64_t>::max() - filter.start_location.group) {
                    throw std::invalid_argument("Subscription filter range overflows");
                }

                return {
                    .type = messages::SubscriptionFilterType::kAbsoluteRange,
                    .start_location = ToCppType<messages::Location>(filter.start_location),
                    .end_group_delta = filter.end_group_delta,
                };
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
                return ToCppType<messages::SubscriptionFilter>(filter.subscription);
            case QUICR_FILTER_TYPE_TRACK_FILTER:
                return ToCppType<messages::TrackFilter>(filter.track);
        }

        throw std::invalid_argument("Invalid filter type value");
    }

    template<>
    quicr_congestion_control_t ToCType<quicr_congestion_control_t, CongestionControl>(
      const CongestionControl& congestion_control)
    {
        switch (congestion_control) {
            case CongestionControl::kBbr:
                return QUICR_CONGESTION_CONTROL_BBR;
            case CongestionControl::kC4:
                return QUICR_CONGESTION_CONTROL_C4;
            case CongestionControl::kCubic:
                return QUICR_CONGESTION_CONTROL_CUBIC;
            case CongestionControl::kNewReno:
                return QUICR_CONGESTION_CONTROL_NEW_RENO;
        }

        throw std::invalid_argument("Invalid congestion control value");
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

    std::string ErrorReason(const char* reason)
    {
        return reason ? reason : "";
    }

    template<typename E>
    Unexpected<Error<E>> MakeError(E code, const char* reason)
    {
        return Unexpected<Error<E>>(code, ErrorReason(reason));
    }

    template<>
    RequestErrorCode ToCppType<RequestErrorCode, quicr_request_error_code_t>(const quicr_request_error_code_t& code)
    {
        if (code < QUICR_REQUEST_ERROR_INTERNAL_ERROR || code > QUICR_REQUEST_ERROR_INVALID_JOINING_REQUEST_ID) {
            return RequestErrorCode::kInternalError;
        }
        return static_cast<RequestErrorCode>(code);
    }

    template<>
    PublishErrorCode ToCppType<PublishErrorCode, quicr_publish_error_code_t>(const quicr_publish_error_code_t& code)
    {
        if (code < QUICR_PUBLISH_ERROR_INTERNAL_ERROR || code > QUICR_PUBLISH_ERROR_REJECTED) {
            return PublishErrorCode::kInternalError;
        }
        return static_cast<PublishErrorCode>(code);
    }

    template<>
    PublishNamespaceErrorCode ToCppType<PublishNamespaceErrorCode, quicr_publish_namespace_error_code_t>(
      const quicr_publish_namespace_error_code_t& code)
    {
        if (code > QUICR_PUBLISH_NAMESPACE_ERROR_INTERNAL_ERROR) {
            return PublishNamespaceErrorCode::kInternalError;
        }
        return static_cast<PublishNamespaceErrorCode>(code);
    }

    template<>
    FetchErrorCode ToCppType<FetchErrorCode, quicr_fetch_error_code_t>(const quicr_fetch_error_code_t& code)
    {
        if (code > QUICR_FETCH_ERROR_INTERNAL_ERROR) {
            return FetchErrorCode::kInternalError;
        }
        return static_cast<FetchErrorCode>(code);
    }

    template<>
    ErrorCode ToCppType<ErrorCode, quicr_error_code_t>(const quicr_error_code_t& code)
    {
        switch (code) {
            case QUICR_ERROR_INTERNAL_ERROR:
            case QUICR_ERROR_UNAUTHORIZED:
            case QUICR_ERROR_TIMEOUT:
            case QUICR_ERROR_NOT_SUPPORTED:
            case QUICR_ERROR_MALFORMED_AUTH_TOKEN:
            case QUICR_ERROR_EXPIRED_AUTH_TOKEN:
            case QUICR_ERROR_DOES_NOT_EXIST:
            case QUICR_ERROR_INVALID_RANGE:
            case QUICR_ERROR_MALFORMED_TRACK:
            case QUICR_ERROR_DUPLICATE_SUBSCRIPTION:
            case QUICR_ERROR_UNINTERESTED:
            case QUICR_ERROR_PREFIX_OVERLAP:
            case QUICR_ERROR_INVALID_JOINING_REQUEST_ID:
                return static_cast<ErrorCode>(code);
            default:
                return ErrorCode::kInternalError;
        }
    }

    template<>
    std::vector<TrackNamespace> ToCppType<std::vector<TrackNamespace>, quicr_track_namespace_list_t>(
      const quicr_track_namespace_list_t& list)
    {
        if (!list.entries && list.count != 0) {
            throw std::invalid_argument("Track namespace list entries are null");
        }

        std::vector<TrackNamespace> result;
        result.reserve(list.count);
        for (std::size_t i = 0; i < list.count; ++i) {
            if (!list.entries[i]) {
                throw std::invalid_argument("Track namespace list entry is null");
            }
            result.push_back(ToCppType<TrackNamespace>(*list.entries[i]));
        }
        return result;
    }

    template<>
    PublishResponse ToCppType<PublishResponse, quicr_publish_response_t>(const quicr_publish_response_t& response)
    {
        return {
            .attributes = {
              .subscriber_priority = response.subscriber_priority,
              .group_order = response.group_order,
              .filter = response.filter,
              .forward = response.forward,
              .subgroup_delivery_timeout = response.subgroup_delivery_timeout,
              .object_delivery_timeout = response.object_delivery_timeout,
              .new_group_request_id = response.new_group_request_id,
            },
            .handler = response.impl,
        };
    }

    template<>
    SubscribeResponse ToCppType<SubscribeResponse, quicr_subscribe_response_t>(
      const quicr_subscribe_response_t& response)
    {
        SubscribeResponse result{};
        const auto& track_status = response.track_status;
        if (track_status.has_largest_location) {
            result.largest_location = ToCppType<messages::Location>(track_status.largest_location);
        }
        if (track_status.track_extensions) {
            result.track_properties = track_status.track_extensions->Get();
        }
        result.is_publisher_initiated = response.is_publisher_initiated;
        if (response.has_expires) {
            if (response.expires_ms >
                static_cast<std::uint64_t>(std::numeric_limits<std::chrono::milliseconds::rep>::max())) {
                throw std::invalid_argument("Subscribe response expiry exceeds supported duration");
            }
            result.expires = std::chrono::milliseconds{ response.expires_ms };
        }
        return result;
    }

    template<>
    FetchResponse ToCppType<FetchResponse, quicr_fetch_response_t>(const quicr_fetch_response_t& response)
    {
        return {
            .largest_location = response.has_largest_location
                                  ? std::make_optional(ToCppType<messages::Location>(response.largest_location))
                                  : std::nullopt,
            .publisher_default_group_order =
              response.publisher_default_group_order == QUICR_GROUP_ORDER_UNSPECIFIED
                ? messages::GroupOrder::kAscending
                : ToCppType<messages::GroupOrder>(response.publisher_default_group_order),
        };
    }

    template<>
    TrackStatusResponse ToCppType<TrackStatusResponse, quicr_track_status_response_t>(
      const quicr_track_status_response_t& response)
    {
        TrackStatusResponse result{};
        if (response.has_largest_location) {
            result.largest_location = ToCppType<messages::Location>(response.largest_location);
        }
        if (response.track_extensions) {
            result.track_properties = response.track_extensions->Get();
        }
        return result;
    }

    template<>
    Reply<const PublishResponse, PublishErrorCode>
    ToCppType<Reply<const PublishResponse, PublishErrorCode>, quicr_publish_reply_t>(const quicr_publish_reply_t& reply)
    {
        if (reply.result == QUICR_REPLY_RESULT_ACCEPT) {
            if (reply.response) {
                return ToCppType<PublishResponse>(*reply.response);
            }
            return MakeError(PublishErrorCode::kInternalError, "Accepted publish reply has no response");
        }
        if (reply.result == QUICR_REPLY_RESULT_REJECT) {
            return MakeError(ToCppType<PublishErrorCode>(reply.error.code), reply.error.reason);
        }
        return MakeError(PublishErrorCode::kInternalError, "Invalid publish reply result");
    }

    template<>
    Reply<void, PublishNamespaceErrorCode>
    ToCppType<Reply<void, PublishNamespaceErrorCode>, quicr_publish_namespace_reply_t>(
      const quicr_publish_namespace_reply_t& reply)
    {
        if (reply.result == QUICR_REPLY_RESULT_ACCEPT) {
            return {};
        }
        if (reply.result == QUICR_REPLY_RESULT_REJECT) {
            return MakeError(ToCppType<PublishNamespaceErrorCode>(reply.error.code), reply.error.reason);
        }
        return MakeError(PublishNamespaceErrorCode::kInternalError, "Invalid publish namespace reply result");
    }

    template<>
    Reply<const FetchResponse, FetchErrorCode>
    ToCppType<Reply<const FetchResponse, FetchErrorCode>, quicr_fetch_reply_t>(const quicr_fetch_reply_t& reply)
    {
        if (reply.result == QUICR_REPLY_RESULT_ACCEPT) {
            return ToCppType<FetchResponse>(reply.response);
        }
        if (reply.result == QUICR_REPLY_RESULT_REJECT) {
            return MakeError(ToCppType<FetchErrorCode>(reply.error.code), reply.error.reason);
        }
        return MakeError(FetchErrorCode::kInternalError, "Invalid fetch reply result");
    }

    template<>
    Reply<void, FetchErrorCode> ToCppType<Reply<void, FetchErrorCode>, quicr_fetch_cancel_reply_t>(
      const quicr_fetch_cancel_reply_t& reply)
    {
        if (reply.result == QUICR_REPLY_RESULT_ACCEPT) {
            return {};
        }
        if (reply.result == QUICR_REPLY_RESULT_REJECT) {
            return MakeError(ToCppType<FetchErrorCode>(reply.error.code), reply.error.reason);
        }
        return MakeError(FetchErrorCode::kInternalError, "Invalid fetch cancellation reply result");
    }

    template<>
    Reply<TrackStatusResponse, RequestErrorCode>
    ToCppType<Reply<TrackStatusResponse, RequestErrorCode>, quicr_track_status_reply_t>(
      const quicr_track_status_reply_t& reply)
    {
        if (reply.result == QUICR_REPLY_RESULT_ACCEPT) {
            return ToCppType<TrackStatusResponse>(reply.response);
        }
        if (reply.result == QUICR_REPLY_RESULT_REJECT) {
            return MakeError(ToCppType<RequestErrorCode>(reply.error.code), reply.error.reason);
        }
        return MakeError(RequestErrorCode::kInternalError, "Invalid track status reply result");
    }

    template<>
    Reply<SubscribeResponse, RequestErrorCode>
    ToCppType<Reply<SubscribeResponse, RequestErrorCode>, quicr_subscribe_reply_t>(const quicr_subscribe_reply_t& reply)
    {
        if (reply.result == QUICR_REPLY_RESULT_ACCEPT) {
            return ToCppType<SubscribeResponse>(reply.response);
        }
        if (reply.result == QUICR_REPLY_RESULT_REJECT) {
            return MakeError(ToCppType<RequestErrorCode>(reply.error.code), reply.error.reason);
        }
        return MakeError(RequestErrorCode::kInternalError, "Invalid subscribe reply result");
    }

    template<>
    Reply<std::vector<TrackNamespace>, RequestErrorCode>
    ToCppType<Reply<std::vector<TrackNamespace>, RequestErrorCode>, quicr_track_namespaces_reply_t>(
      const quicr_track_namespaces_reply_t& reply)
    {
        if (reply.result == QUICR_REPLY_RESULT_ACCEPT) {
            return reply.response ? ToCppType<std::vector<TrackNamespace>>(*reply.response)
                                  : std::vector<TrackNamespace>{};
        }
        if (reply.result == QUICR_REPLY_RESULT_REJECT) {
            return MakeError(ToCppType<RequestErrorCode>(reply.error.code), reply.error.reason);
        }
        return MakeError(RequestErrorCode::kInternalError, "Invalid track namespaces reply result");
    }

    template<>
    Reply<void, ErrorCode> ToCppType<Reply<void, ErrorCode>, quicr_error_reply_t>(const quicr_error_reply_t& reply)
    {
        if (reply.result == QUICR_REPLY_RESULT_ACCEPT) {
            return {};
        }
        if (reply.result == QUICR_REPLY_RESULT_REJECT) {
            return MakeError(ToCppType<ErrorCode>(reply.error.code), reply.error.reason);
        }
        return MakeError(ErrorCode::kInternalError, "Invalid protocol reply result");
    }

    template<>
    Expected<void, Error<ErrorCode>> ToCppType<Expected<void, Error<ErrorCode>>, quicr_error_reply_t>(
      const quicr_error_reply_t& reply)
    {
        if (reply.result == QUICR_REPLY_RESULT_ACCEPT) {
            return {};
        }
        if (reply.result == QUICR_REPLY_RESULT_REJECT) {
            return MakeError(ToCppType<ErrorCode>(reply.error.code), reply.error.reason);
        }
        return MakeError(ErrorCode::kInternalError, "Invalid setup reply result");
    }

    template<>
    quicr_subscribe_track_handler_status_t
    ToCType<quicr_subscribe_track_handler_status_t, SubscribeTrackHandler::Status>(
      const SubscribeTrackHandler::Status& status)
    {
        switch (status) {
            case SubscribeTrackHandler::Status::kOk:
                return QUICR_SUBSCRIBE_TRACK_HANDLER_STATUS_OK;
            case SubscribeTrackHandler::Status::kNotConnected:
                return QUICR_SUBSCRIBE_TRACK_HANDLER_STATUS_NOT_CONNECTED;
            case SubscribeTrackHandler::Status::kError:
                return QUICR_SUBSCRIBE_TRACK_HANDLER_STATUS_ERROR;
            case SubscribeTrackHandler::Status::kNotAuthorized:
                return QUICR_SUBSCRIBE_TRACK_HANDLER_STATUS_NOT_AUTHORIZED;
            case SubscribeTrackHandler::Status::kNotSubscribed:
                return QUICR_SUBSCRIBE_TRACK_HANDLER_STATUS_NOT_SUBSCRIBED;
            case SubscribeTrackHandler::Status::kPendingResponse:
                return QUICR_SUBSCRIBE_TRACK_HANDLER_STATUS_PENDING_RESPONSE;
            case SubscribeTrackHandler::Status::kSendingUnsubscribe:
                return QUICR_SUBSCRIBE_TRACK_HANDLER_STATUS_SENDING_UNSUBSCRIBE;
            case SubscribeTrackHandler::Status::kPaused:
                return QUICR_SUBSCRIBE_TRACK_HANDLER_STATUS_PAUSED;
            case SubscribeTrackHandler::Status::kNewGroupRequested:
                return QUICR_SUBSCRIBE_TRACK_HANDLER_STATUS_NEW_GROUP_REQUESTED;
            case SubscribeTrackHandler::Status::kCancelled:
                return QUICR_SUBSCRIBE_TRACK_HANDLER_STATUS_CANCELLED;
            case SubscribeTrackHandler::Status::kDoneByFin:
                return QUICR_SUBSCRIBE_TRACK_HANDLER_STATUS_DONE_BY_FIN;
            case SubscribeTrackHandler::Status::kDoneByReset:
                return QUICR_SUBSCRIBE_TRACK_HANDLER_STATUS_DONE_BY_RESET;
        }

        throw std::invalid_argument("Invalid subscribe track handler status value");
    }

    template<>
    quicr_publish_namespace_handler_status_t
    ToCType<quicr_publish_namespace_handler_status_t, PublishNamespaceHandler::Status>(
      const PublishNamespaceHandler::Status& status)
    {
        switch (status) {
            case PublishNamespaceHandler::Status::kOk:
                return QUICR_PUBLISH_NAMESPACE_HANDLER_STATUS_OK;
            case PublishNamespaceHandler::Status::kNotConnected:
                return QUICR_PUBLISH_NAMESPACE_HANDLER_STATUS_NOT_CONNECTED;
            case PublishNamespaceHandler::Status::kNotPublished:
                return QUICR_PUBLISH_NAMESPACE_HANDLER_STATUS_NOT_PUBLISHED;
            case PublishNamespaceHandler::Status::kPendingResponse:
                return QUICR_PUBLISH_NAMESPACE_HANDLER_STATUS_PENDING_RESPONSE;
            case PublishNamespaceHandler::Status::kPublishNotAuthorized:
                return QUICR_PUBLISH_NAMESPACE_HANDLER_STATUS_NOT_AUTHORIZED;
            case PublishNamespaceHandler::Status::kSendingDone:
                return QUICR_PUBLISH_NAMESPACE_HANDLER_STATUS_SENDING_DONE;
            case PublishNamespaceHandler::Status::kError:
                return QUICR_PUBLISH_NAMESPACE_HANDLER_STATUS_ERROR;
        }

        throw std::invalid_argument("Invalid publish namespace handler status value");
    }

    template<>
    quicr_subscribe_namespace_handler_status_t
    ToCType<quicr_subscribe_namespace_handler_status_t, SubscribeNamespaceHandler::Status>(
      const SubscribeNamespaceHandler::Status& status)
    {
        switch (status) {
            case SubscribeNamespaceHandler::Status::kOk:
                return QUICR_SUBSCRIBE_NAMESPACE_HANDLER_STATUS_OK;
            case SubscribeNamespaceHandler::Status::kNotSubscribed:
                return QUICR_SUBSCRIBE_NAMESPACE_HANDLER_STATUS_NOT_SUBSCRIBED;
            case SubscribeNamespaceHandler::Status::kError:
                return QUICR_SUBSCRIBE_NAMESPACE_HANDLER_STATUS_ERROR;
        }

        throw std::invalid_argument("Invalid subscribe namespace handler status value");
    }

    template<>
    quicr_track_status_handler_status_t ToCType<quicr_track_status_handler_status_t, TrackStatusHandler::Status>(
      const TrackStatusHandler::Status& status)
    {
        switch (status) {
            case TrackStatusHandler::Status::kNotRequested:
                return QUICR_TRACK_STATUS_HANDLER_STATUS_NOT_REQUESTED;
            case TrackStatusHandler::Status::kPendingResponse:
                return QUICR_TRACK_STATUS_HANDLER_STATUS_PENDING_RESPONSE;
            case TrackStatusHandler::Status::kOk:
                return QUICR_TRACK_STATUS_HANDLER_STATUS_OK;
            case TrackStatusHandler::Status::kError:
                return QUICR_TRACK_STATUS_HANDLER_STATUS_ERROR;
            case TrackStatusHandler::Status::kDoneByFin:
                return QUICR_TRACK_STATUS_HANDLER_STATUS_DONE_BY_FIN;
            case TrackStatusHandler::Status::kDoneByReset:
                return QUICR_TRACK_STATUS_HANDLER_STATUS_DONE_BY_RESET;
            case TrackStatusHandler::Status::kNotConnected:
                return QUICR_TRACK_STATUS_HANDLER_STATUS_NOT_CONNECTED;
        }

        throw std::invalid_argument("Invalid track status handler status value");
    }

    template<>
    SubscribeNamespaceHandler::Mode ToCppType<SubscribeNamespaceHandler::Mode, quicr_subscribe_namespace_mode_t>(
      const quicr_subscribe_namespace_mode_t& mode)
    {
        switch (mode) {
            case QUICR_SUBSCRIBE_NAMESPACE_MODE_NAMESPACES:
                return SubscribeNamespaceHandler::Mode::kNamespaces;
            case QUICR_SUBSCRIBE_NAMESPACE_MODE_TRACKS:
                return SubscribeNamespaceHandler::Mode::kTracks;
        }

        throw std::invalid_argument("Invalid subscribe namespace mode value");
    }

    template<>
    messages::FetchEndLocation ToCppType<messages::FetchEndLocation, quicr_fetch_end_location_t>(
      const quicr_fetch_end_location_t& location)
    {
        return {
            .group = location.group,
            .object = location.has_object ? std::make_optional(location.object) : std::nullopt,
        };
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

    std::unique_ptr<quicr_session_t> RemoveSessionHandle(quicr::Session* session)
    {
        std::lock_guard lock(sessions_mutex);

        auto handle = sessions.extract(session);
        return handle.empty() ? nullptr : std::move(handle.mapped());
    }

    void ClearSessionHandles()
    {
        std::lock_guard lock(sessions_mutex);
        sessions.clear();
    }
};

class HandlerCallbackState;

struct quicr_publish_track_handler
{
    std::shared_ptr<PublishTrackHandler> impl;
    std::shared_ptr<HandlerCallbackState> callback_state;
};

struct quicr_subscribe_track_handler
{
    std::shared_ptr<SubscribeTrackHandler> impl;
    std::shared_ptr<HandlerCallbackState> callback_state;
};

struct quicr_fetch_track_handler
{
    std::shared_ptr<FetchTrackHandler> impl;
    std::shared_ptr<HandlerCallbackState> callback_state;
};

struct quicr_publish_namespace_handler
{
    std::shared_ptr<PublishNamespaceHandler> impl;
    std::shared_ptr<HandlerCallbackState> callback_state;
};

struct quicr_subscribe_namespace_handler
{
    std::shared_ptr<SubscribeNamespaceHandler> impl;
    std::shared_ptr<HandlerCallbackState> callback_state;
};

struct quicr_track_status_track_handler
{
    std::shared_ptr<TrackStatusHandler> impl;
    std::shared_ptr<HandlerCallbackState> callback_state;
};

namespace {
    quicr_status_t CopyHandlerError(const std::optional<Error<ErrorCode>>& cpp_error,
                                    quicr_error_code_t* code,
                                    char* reason,
                                    size_t* reason_size)
    {
        if (!code || (reason && !reason_size)) {
            return QUICR_STATUS_INVALID_ARGUMENT;
        }

        if (!cpp_error) {
            if (reason_size) {
                *reason_size = 0;
            }
            return QUICR_STATUS_NOT_FOUND;
        }

        *code = static_cast<quicr_error_code_t>(cpp_error->reason_code);
        if (!reason_size) {
            return QUICR_STATUS_OK;
        }

        const size_t capacity = *reason_size;
        const size_t required_size = cpp_error->error_reason ? cpp_error->error_reason->size() + 1 : 0;
        *reason_size = required_size;

        if (!reason) {
            return QUICR_STATUS_OK;
        }

        if (capacity < required_size) {
            return QUICR_STATUS_BUFFER_TOO_SMALL;
        }

        if (required_size != 0) {
            std::memcpy(reason, cpp_error->error_reason->c_str(), required_size);
        }

        return QUICR_STATUS_OK;
    }
}

/*===========================================================================*/
// C++ Adapters
/*===========================================================================*/

class HandlerCallbackState
{
  public:
    template<typename Callback, typename... Args>
    void Invoke(Callback&& callback, Args&&... args) noexcept
    {
        if (!BeginCallback()) {
            return;
        }

        SafeCallback(std::forward<Callback>(callback), std::forward<Args>(args)...);
        EndCallback();
    }

    void EnableCallbacks() noexcept { callbacks_enabled_ = true; }

    bool DisableCallbacks() noexcept
    {
        callbacks_enabled_ = false;
        return active_callbacks_ == 0;
    }

  private:
    bool BeginCallback() noexcept
    {
        if (!callbacks_enabled_) {
            return false;
        }

        ++active_callbacks_;
        if (!callbacks_enabled_) {
            --active_callbacks_;
            return false;
        }

        return true;
    }

    void EndCallback() noexcept { --active_callbacks_; }

  private:
    std::atomic<std::size_t> active_callbacks_{ 0 };
    std::atomic<bool> callbacks_enabled_{ true };
};

template<typename DerivedSessionCallbacks, typename CCallbacks>
    requires std::is_base_of_v<Session::Callbacks, DerivedSessionCallbacks>
class SessionCallbacksAdapter : public DerivedSessionCallbacks
{
  public:
    SessionCallbacksAdapter(quicr_session_t* owner, const CCallbacks& callbacks)
      : owner(owner)
      , callbacks(callbacks)
    {
    }

    SessionCallbacksAdapter(quicr_session_manager_t* manager, const CCallbacks& callbacks)
      : manager(manager)
      , callbacks(callbacks)
    {
    }

    Expected<void, Error<ErrorCode>> SetupReceived(const std::shared_ptr<Session>& session,
                                                   const SetupAttributes& attributes) override
    {
        if (!callbacks.setup_received) {
            return Session::Callbacks::SetupReceived(session, attributes);
        }

        const auto c_attributes = ToCType<quicr_setup_attributes>(attributes);
        return SafeCallback(
          [&] {
              const auto reply =
                callbacks.setup_received(GetSessionHandle(session), &c_attributes, callbacks.user_data);
              return ToCppType<Expected<void, Error<ErrorCode>>>(reply);
          },
          CallbackInternalError<ErrorCode>());
    }

    void OnStreamClosed(std::uint64_t stream_id, StreamClosedFlag flag) override
    {
        if (!callbacks.on_stream_closed) {
            return;
        }

        SafeCallback([&] {
            callbacks.on_stream_closed(stream_id, ToCType<quicr_stream_closed_flag_t>(flag), callbacks.user_data);
        });
    }

    void StatusChanged(const std::shared_ptr<Session>& session, Session::Status status) override
    {
        if (!callbacks.status_changed) {
            return;
        }

        auto* owner = GetSessionHandle(session);
        SafeCallback(
          [&] { callbacks.status_changed(owner, ToCType<quicr_session_status_t>(status), callbacks.user_data); });
    }

    Reply<const PublishResponse, PublishErrorCode> PublishReceived(
      const std::shared_ptr<Session>& session,
      std::uint64_t request_id,
      const PublishAttributes& attributes,
      std::weak_ptr<SubscribeNamespaceHandler> sub_ns_handler) override
    {
        if (!callbacks.publish_received) {
            return Session::Callbacks::PublishReceived(session, request_id, attributes, std::move(sub_ns_handler));
        }

        const auto c_attributes = ToCType<quicr_publish_attributes>(attributes);
        const auto cpp_handler = sub_ns_handler.lock();
        quicr_subscribe_namespace_handler c_handler{ .impl = cpp_handler };
        return SafeCallback(
          [&] {
              const auto reply = callbacks.publish_received(GetSessionHandle(session),
                                                            request_id,
                                                            &c_attributes,
                                                            cpp_handler ? &c_handler : nullptr,
                                                            callbacks.user_data);
              return ToCppType<Reply<const PublishResponse, PublishErrorCode>>(reply);
          },
          CallbackInternalError<PublishErrorCode>());
    }

    Reply<void, PublishNamespaceErrorCode> PublishNamespaceReceived(
      const std::shared_ptr<Session>& session,
      const TrackNamespace& track_namespace,
      const PublishNamespaceAttributes& attributes) override
    {
        if (!callbacks.publish_namespace_received) {
            return Session::Callbacks::PublishNamespaceReceived(session, track_namespace, attributes);
        }

        const auto c_track_namespace = ToCType<quicr_track_namespace_t>(track_namespace);
        const auto c_attributes = ToCType<quicr_publish_namespace_attributes>(attributes);
        return SafeCallback(
          [&] {
              const auto reply = callbacks.publish_namespace_received(
                GetSessionHandle(session), &c_track_namespace, &c_attributes, callbacks.user_data);
              return ToCppType<Reply<void, PublishNamespaceErrorCode>>(reply);
          },
          CallbackInternalError<PublishNamespaceErrorCode>());
    }

    Reply<const FetchResponse, FetchErrorCode> StandaloneFetchReceived(
      const std::shared_ptr<Session>& session,
      std::uint64_t request_id,
      const FullTrackName& full_track_name,
      const StandaloneFetchAttributes& attributes) override
    {
        if (!callbacks.standalone_fetch_received) {
            return Session::Callbacks::StandaloneFetchReceived(session, request_id, full_track_name, attributes);
        }

        const auto c_full_track_name = ToCType<quicr_full_track_name_t>(full_track_name);
        const auto c_attributes = ToCType<quicr_standalone_fetch_attributes>(attributes);
        return SafeCallback(
          [&] {
              const auto reply = callbacks.standalone_fetch_received(
                GetSessionHandle(session), request_id, &c_full_track_name, &c_attributes, callbacks.user_data);
              return ToCppType<Reply<const FetchResponse, FetchErrorCode>>(reply);
          },
          CallbackInternalError<FetchErrorCode>());
    }

    Reply<const FetchResponse, FetchErrorCode> JoiningFetchReceived(const std::shared_ptr<Session>& session,
                                                                    std::uint64_t request_id,
                                                                    const FullTrackName& full_track_name,
                                                                    const JoiningFetchAttributes& attributes) override
    {
        if (!callbacks.joining_fetch_received) {
            return Session::Callbacks::JoiningFetchReceived(session, request_id, full_track_name, attributes);
        }

        const auto c_full_track_name = ToCType<quicr_full_track_name_t>(full_track_name);
        const auto c_attributes = ToCType<quicr_joining_fetch_attributes>(attributes);
        return SafeCallback(
          [&] {
              const auto reply = callbacks.joining_fetch_received(
                GetSessionHandle(session), request_id, &c_full_track_name, &c_attributes, callbacks.user_data);
              return ToCppType<Reply<const FetchResponse, FetchErrorCode>>(reply);
          },
          CallbackInternalError<FetchErrorCode>());
    }

    Reply<void, FetchErrorCode> FetchCancelReceived(const std::shared_ptr<Session>& session,
                                                    std::uint64_t request_id) override
    {
        if (!callbacks.fetch_cancel_received) {
            return Session::Callbacks::FetchCancelReceived(session, request_id);
        }

        return SafeCallback(
          [&] {
              const auto reply =
                callbacks.fetch_cancel_received(GetSessionHandle(session), request_id, callbacks.user_data);
              return ToCppType<Reply<void, FetchErrorCode>>(reply);
          },
          CallbackInternalError<FetchErrorCode>());
    }

    Reply<TrackStatusResponse, RequestErrorCode> TrackStatusReceived(const std::shared_ptr<Session>& session,
                                                                     const FullTrackName& full_track_name) override
    {
        if (!callbacks.track_status_received) {
            return Session::Callbacks::TrackStatusReceived(session, full_track_name);
        }

        const auto c_full_track_name = ToCType<quicr_full_track_name_t>(full_track_name);
        return SafeCallback(
          [&] {
              const auto reply =
                callbacks.track_status_received(GetSessionHandle(session), &c_full_track_name, callbacks.user_data);
              return ToCppType<Reply<TrackStatusResponse, RequestErrorCode>>(reply);
          },
          CallbackInternalError<RequestErrorCode>());
    }

  protected:
    quicr_session_t* GetSessionHandle(const std::shared_ptr<Session>& session)
    {
        return owner ? owner : manager->GetOrCreateSessionHandle(session);
    }

    quicr_session_t* owner{ nullptr };
    quicr_session_manager_t* manager{ nullptr };
    CCallbacks callbacks;
};

class ClientCallbacksAdapter final
  : public SessionCallbacksAdapter<Session::ClientCallbacks, quicr_session_client_callbacks_t>
{
  public:
    using SessionCallbacksAdapter::SessionCallbacksAdapter;

    Reply<void, ErrorCode> UnpublishedSubscribeReceived(const std::shared_ptr<Session>& session,
                                                        const FullTrackName& full_track_name,
                                                        const SubscribeAttributes& attributes) override
    {
        if (!callbacks.unpublished_subscribe_received) {
            return Session::ClientCallbacks::UnpublishedSubscribeReceived(session, full_track_name, attributes);
        }

        const auto c_full_track_name = ToCType<quicr_full_track_name_t>(full_track_name);
        const auto c_attributes = ToCType<quicr_subscribe_attributes>(attributes);
        return SafeCallback(
          [&] {
              const auto reply = callbacks.unpublished_subscribe_received(
                GetSessionHandle(session), &c_full_track_name, &c_attributes, callbacks.user_data);
              return ToCppType<Reply<void, ErrorCode>>(reply);
          },
          CallbackInternalError<ErrorCode>());
    }
};

class ServerCallbacksAdapter final
  : public SessionCallbacksAdapter<Session::ServerCallbacks, quicr_session_server_callbacks_t>
{
  public:
    using SessionCallbacksAdapter::SessionCallbacksAdapter;

    Reply<std::vector<TrackNamespace>, RequestErrorCode> SubscribeNamespaceReceived(
      const std::shared_ptr<Session>& session,
      const TrackNamespace& prefix_namespace,
      const SubscribeNamespaceAttributes& attributes) override
    {
        if (!callbacks.subscribe_namespace_received) {
            return Session::ServerCallbacks::SubscribeNamespaceReceived(session, prefix_namespace, attributes);
        }

        const auto c_prefix_namespace = ToCType<quicr_track_namespace_t>(prefix_namespace);
        const auto c_attributes = ToCType<quicr_subscribe_namespace_attributes>(attributes);
        return SafeCallback(
          [&] {
              const auto reply = callbacks.subscribe_namespace_received(
                GetSessionHandle(session), &c_prefix_namespace, &c_attributes, callbacks.user_data);
              return ToCppType<Reply<std::vector<TrackNamespace>, RequestErrorCode>>(reply);
          },
          CallbackInternalError<RequestErrorCode>());
    }

    Reply<std::vector<TrackNamespace>, RequestErrorCode> SubscribeTracksReceived(
      const std::shared_ptr<Session>& session,
      const TrackNamespace& prefix_namespace,
      const SubscribeNamespaceAttributes& attributes) override
    {
        if (!callbacks.subscribe_tracks_received) {
            return Session::ServerCallbacks::SubscribeTracksReceived(session, prefix_namespace, attributes);
        }

        const auto c_prefix_namespace = ToCType<quicr_track_namespace_t>(prefix_namespace);
        const auto c_attributes = ToCType<quicr_subscribe_namespace_attributes>(attributes);
        return SafeCallback(
          [&] {
              const auto reply = callbacks.subscribe_tracks_received(
                GetSessionHandle(session), &c_prefix_namespace, &c_attributes, callbacks.user_data);
              return ToCppType<Reply<std::vector<TrackNamespace>, RequestErrorCode>>(reply);
          },
          CallbackInternalError<RequestErrorCode>());
    }

    Reply<SubscribeResponse, RequestErrorCode> SubscribeReceived(const std::shared_ptr<Session>& session,
                                                                 std::uint64_t request_id,
                                                                 const FullTrackName& full_track_name,
                                                                 const SubscribeAttributes& attributes) override
    {
        if (!callbacks.subscribe_received) {
            return Session::ServerCallbacks::SubscribeReceived(session, request_id, full_track_name, attributes);
        }

        const auto c_full_track_name = ToCType<quicr_full_track_name_t>(full_track_name);
        const auto c_attributes = ToCType<quicr_subscribe_attributes>(attributes);
        return SafeCallback(
          [&] {
              const auto reply = callbacks.subscribe_received(
                GetSessionHandle(session), request_id, &c_full_track_name, &c_attributes, callbacks.user_data);
              return ToCppType<Reply<SubscribeResponse, RequestErrorCode>>(reply);
          },
          CallbackInternalError<RequestErrorCode>());
    }

    Reply<void, ErrorCode> NewGroupRequested(const FullTrackName& full_track_name, std::uint64_t group_id) override
    {
        if (!callbacks.new_group_requested) {
            return Session::ServerCallbacks::NewGroupRequested(full_track_name, group_id);
        }

        const auto c_full_track_name = ToCType<quicr_full_track_name_t>(full_track_name);
        return SafeCallback(
          [&] {
              const auto reply = callbacks.new_group_requested(&c_full_track_name, group_id, callbacks.user_data);
              return ToCppType<Reply<void, ErrorCode>>(reply);
          },
          CallbackInternalError<ErrorCode>());
    }
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
            SafeCallback(callbacks_.new_server_session, owner_, handle, callbacks_.user_data);
        }
    }

    void OnSessionRemoved(const std::shared_ptr<quicr::Session>& session) override
    {
        if (!session) {
            return;
        }

        auto handle = owner_->RemoveSessionHandle(session.get());
        const auto callback = callbacks_.session_removed;
        const auto user_data = callbacks_.user_data;
        auto* owner = owner_;

        if (callback) {
            SafeCallback(callback, owner, handle.get(), user_data);
        }
    }

  private:
    quicr_session_manager_t* owner_;
    quicr_session_manager_callbacks_t callbacks_;
};

class PublishTrackHandlerAdapter : public quicr::PublishTrackHandler
{
  public:
    PublishTrackHandlerAdapter(quicr_publish_track_handler* owner,
                               const quicr_publish_track_handler_callbacks_t& callbacks,
                               const FullTrackName& full_track_name,
                               TrackMode track_mode,
                               std::uint8_t publisher_priority,
                               std::uint8_t publish_object_priority,
                               std::uint32_t default_ttl,
                               const messages::Location& largest_location)
      : PublishTrackHandler(full_track_name,
                            track_mode,
                            publisher_priority,
                            publish_object_priority,
                            default_ttl,
                            std::nullopt,
                            largest_location)
      , owner_(owner)
      , callback_state_(owner->callback_state)
      , callbacks_(callbacks)
    {
    }

    void StatusChanged(Status status) override
    {
        if (owner_ && callbacks_.status_changed) {
            callback_state_->Invoke(callbacks_.status_changed,
                                    owner_,
                                    ToCType<quicr_publish_track_handler_status_t>(status),
                                    callbacks_.user_data);
        }
    }

    void MetricsSampled(const PublishTrackMetrics& metrics) override
    {
        if (owner_ && callbacks_.metrics_sampled) {
            const auto c_metrics = ToCType<quicr_publish_track_metrics_t>(metrics);
            callback_state_->Invoke(callbacks_.metrics_sampled, owner_, &c_metrics, callbacks_.user_data);
        }
    }

    void StreamClosed(std::uint64_t stream_id, bool reset) override
    {
        PublishTrackHandler::StreamClosed(stream_id, reset);

        if (owner_ && callbacks_.stream_closed) {
            callback_state_->Invoke(callbacks_.stream_closed, owner_, stream_id, reset, callbacks_.user_data);
        }
    }

  private:
    quicr_publish_track_handler* owner_;
    std::shared_ptr<HandlerCallbackState> callback_state_;
    quicr_publish_track_handler_callbacks_t callbacks_;
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
      , callback_state_(owner->callback_state)
      , callbacks_(callbacks)
    {
    }

    void StatusChanged(Status status) override
    {
        if (owner_ && callbacks_.status_changed) {
            callback_state_->Invoke(callbacks_.status_changed,
                                    owner_,
                                    ToCType<quicr_subscribe_track_handler_status_t>(status),
                                    callbacks_.user_data);
        }
    }

    void MetricsSampled(const SubscribeTrackMetrics& metrics) override
    {
        if (owner_ && callbacks_.metrics_sampled) {
            const auto c_metrics = ToCType<quicr_subscribe_track_metrics_t>(metrics);
            callback_state_->Invoke(callbacks_.metrics_sampled, owner_, &c_metrics, callbacks_.user_data);
        }
    }

    void ObjectReceived(const ObjectHeaders& headers,
                        BytesSpan data,
                        std::optional<messages::StreamHeaderProperties> properties) override
    {
        if (!owner_ || !callbacks_.object_received) {
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

        callback_state_->Invoke(callbacks_.object_received,
                                std::addressof(c_headers),
                                ToCType<quicr_byte_span_t>(data),
                                properties.has_value() ? std::addressof(c_properties) : nullptr,
                                callbacks_.user_data);
    }

    void ObjectStatusReceived(std::uint64_t group_id,
                              std::uint64_t object_id,
                              std::uint8_t priority,
                              ObjectStatus status,
                              std::optional<Extensions> extensions,
                              std::optional<Extensions> immutable_extensions) override
    {
        if (owner_ && callbacks_.object_status_received) {
            quicr_object_headers_t headers{
                .group_id = group_id,
                .object_id = object_id,
                .payload_length = 0,
                .status = ToCType<quicr_object_status_t>(status),
                .has_priority = true,
                .priority = priority,
            };

            std::optional<CExtensionsView> c_extensions;
            std::optional<CExtensionsView> c_immutable_extensions;
            if (extensions) {
                c_extensions.emplace(*extensions);
                headers.extensions = c_extensions->Get();
            }
            if (immutable_extensions) {
                c_immutable_extensions.emplace(*immutable_extensions);
                headers.immutable_extensions = c_immutable_extensions->Get();
            }

            callback_state_->Invoke(callbacks_.object_status_received, &headers, callbacks_.user_data);
        }
    }

    void SubgroupEnded(std::uint64_t group_id, std::uint64_t subgroup_id, bool reset) override
    {
        if (callbacks_.subgroup_ended) {
            callback_state_->Invoke(callbacks_.subgroup_ended, group_id, subgroup_id, reset, callbacks_.user_data);
        }
    }

    void DgramDataRecv(std::shared_ptr<const std::vector<uint8_t>> data) override
    {
        if (!callbacks_.datagram_received) {
            SubscribeTrackHandler::DgramDataRecv(std::move(data));
            return;
        }

        if (data) {
            callback_state_->Invoke(callbacks_.datagram_received,
                                    ToCType<quicr_byte_span_t>(BytesSpan{ data->data(), data->size() }),
                                    callbacks_.user_data);
        }
    }

  private:
    quicr_subscribe_track_handler* owner_;
    std::shared_ptr<HandlerCallbackState> callback_state_;
    quicr_subscribe_track_handler_callbacks_t callbacks_;
};

class ForwardingSubscribeTrackHandlerAdapter : public quicr::ForwardingSubscribeTrackHandler
{
  public:
    ForwardingSubscribeTrackHandlerAdapter(quicr_subscribe_track_handler* owner,
                                           const quicr_forwarding_subscribe_track_handler_callbacks_t& callbacks,
                                           const FullTrackName& full_track_name,
                                           std::uint8_t priority,
                                           std::optional<messages::GroupOrder> group_order,
                                           const messages::Filter& filter)
      : ForwardingSubscribeTrackHandler(full_track_name, priority, group_order, filter)
      , owner_(owner)
      , callback_state_(owner->callback_state)
      , callbacks_(callbacks)
    {
    }

    void StatusChanged(Status status) override
    {
        const auto& callbacks = callbacks_.track_callbacks;
        if (owner_ && callbacks.status_changed) {
            callback_state_->Invoke(callbacks.status_changed,
                                    owner_,
                                    ToCType<quicr_subscribe_track_handler_status_t>(status),
                                    callbacks.user_data);
        }
    }

    void MetricsSampled(const SubscribeTrackMetrics& metrics) override
    {
        const auto& callbacks = callbacks_.track_callbacks;
        if (owner_ && callbacks.metrics_sampled) {
            const auto c_metrics = ToCType<quicr_subscribe_track_metrics_t>(metrics);
            callback_state_->Invoke(callbacks.metrics_sampled, owner_, &c_metrics, callbacks.user_data);
        }
    }

    void ObjectReceived(const ObjectHeaders& headers,
                        BytesSpan data,
                        std::optional<messages::StreamHeaderProperties> properties) override
    {
        const auto& callbacks = callbacks_.track_callbacks;
        if (!callbacks.object_received) {
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
        if (properties) {
            c_properties = ToCType<quicr_stream_header_properties_t>(*properties);
        }

        callback_state_->Invoke(callbacks.object_received,
                                &c_headers,
                                ToCType<quicr_byte_span_t>(data),
                                properties ? &c_properties : nullptr,
                                callbacks.user_data);
    }

    void ObjectStatusReceived(std::uint64_t group_id,
                              std::uint64_t object_id,
                              std::uint8_t priority,
                              ObjectStatus status,
                              std::optional<Extensions> extensions,
                              std::optional<Extensions> immutable_extensions) override
    {
        const auto& callbacks = callbacks_.track_callbacks;
        if (!callbacks.object_status_received) {
            return;
        }

        quicr_object_headers_t headers{
            .group_id = group_id,
            .object_id = object_id,
            .payload_length = 0,
            .status = ToCType<quicr_object_status_t>(status),
            .has_priority = true,
            .priority = priority,
        };

        std::optional<CExtensionsView> c_extensions;
        std::optional<CExtensionsView> c_immutable_extensions;
        if (extensions) {
            c_extensions.emplace(*extensions);
            headers.extensions = c_extensions->Get();
        }
        if (immutable_extensions) {
            c_immutable_extensions.emplace(*immutable_extensions);
            headers.immutable_extensions = c_immutable_extensions->Get();
        }

        callback_state_->Invoke(callbacks.object_status_received, &headers, callbacks.user_data);
    }

    void SubgroupEnded(std::uint64_t group_id, std::uint64_t subgroup_id, bool reset) override
    {
        const auto& callbacks = callbacks_.track_callbacks;
        if (callbacks.subgroup_ended) {
            callback_state_->Invoke(callbacks.subgroup_ended, group_id, subgroup_id, reset, callbacks.user_data);
        }
    }

    void DgramDataRecv(std::shared_ptr<const std::vector<uint8_t>> data) override
    {
        const auto& callbacks = callbacks_.track_callbacks;
        if (!callbacks.datagram_received) {
            SubscribeTrackHandler::DgramDataRecv(std::move(data));
            return;
        }

        if (data) {
            callback_state_->Invoke(callbacks.datagram_received,
                                    ToCType<quicr_byte_span_t>(BytesSpan{ data->data(), data->size() }),
                                    callbacks.user_data);
        }
    }

    void SubgroupStarted(std::uint64_t group_id,
                         std::uint64_t subgroup_id,
                         std::optional<std::uint8_t> priority,
                         messages::StreamHeaderProperties properties) override
    {
        if (!callbacks_.subgroup_started) {
            return;
        }

        const auto c_properties = ToCType<quicr_stream_header_properties_t>(properties);
        callback_state_->Invoke(callbacks_.subgroup_started,
                                group_id,
                                subgroup_id,
                                priority.has_value(),
                                priority.value_or(0),
                                &c_properties,
                                callbacks_.track_callbacks.user_data);
    }

    void StreamBytesForwarded(std::uint64_t group_id, std::uint64_t subgroup_id, Bytes&& data) override
    {
        if (callbacks_.stream_bytes_forwarded) {
            callback_state_->Invoke(callbacks_.stream_bytes_forwarded,
                                    group_id,
                                    subgroup_id,
                                    ToCType<quicr_byte_span_t>(BytesSpan{ data.data(), data.size() }),
                                    callbacks_.track_callbacks.user_data);
        }
    }

  private:
    quicr_subscribe_track_handler* owner_;
    std::shared_ptr<HandlerCallbackState> callback_state_;
    quicr_forwarding_subscribe_track_handler_callbacks_t callbacks_;
};

class FetchTrackHandlerAdapter : public quicr::FetchTrackHandler
{
  public:
    FetchTrackHandlerAdapter(quicr_fetch_track_handler* owner,
                             const quicr_fetch_track_handler_callbacks_t& callbacks,
                             const FullTrackName& full_track_name,
                             std::uint8_t priority,
                             const messages::Location& start_location,
                             const messages::FetchEndLocation& end_location,
                             messages::GroupOrder group_order)
      : FetchTrackHandler(full_track_name, priority, start_location, end_location, group_order)
      , owner_(owner)
      , callback_state_(owner->callback_state)
      , callbacks_(callbacks)
    {
    }

    void StatusChanged(Status status) override
    {
        if (owner_ && callbacks_.status_changed) {
            callback_state_->Invoke(callbacks_.status_changed,
                                    owner_,
                                    ToCType<quicr_fetch_track_handler_status_t>(status),
                                    callbacks_.user_data);
        }
    }

    void MetricsSampled(const SubscribeTrackMetrics& metrics) override
    {
        if (owner_ && callbacks_.metrics_sampled) {
            const auto c_metrics = ToCType<quicr_subscribe_track_metrics_t>(metrics);
            callback_state_->Invoke(callbacks_.metrics_sampled, owner_, &c_metrics, callbacks_.user_data);
        }
    }

    void ObjectReceived(const ObjectHeaders& headers,
                        BytesSpan data,
                        std::optional<messages::StreamHeaderProperties> properties) override
    {
        if (!owner_ || !callbacks_.object_received) {
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
        if (properties) {
            c_properties = ToCType<quicr_stream_header_properties_t>(*properties);
        }

        callback_state_->Invoke(callbacks_.object_received,
                                &c_headers,
                                ToCType<quicr_byte_span_t>(data),
                                properties ? &c_properties : nullptr,
                                callbacks_.user_data);
    }

    void ObjectStatusReceived(std::uint64_t group_id,
                              std::uint64_t object_id,
                              std::uint8_t priority,
                              ObjectStatus status,
                              std::optional<Extensions> extensions,
                              std::optional<Extensions> immutable_extensions) override
    {
        if (owner_ && callbacks_.object_status_received) {
            quicr_object_headers_t headers{
                .group_id = group_id,
                .object_id = object_id,
                .payload_length = 0,
                .status = ToCType<quicr_object_status_t>(status),
                .has_priority = true,
                .priority = priority,
            };

            std::optional<CExtensionsView> c_extensions;
            std::optional<CExtensionsView> c_immutable_extensions;
            if (extensions) {
                c_extensions.emplace(*extensions);
                headers.extensions = c_extensions->Get();
            }
            if (immutable_extensions) {
                c_immutable_extensions.emplace(*immutable_extensions);
                headers.immutable_extensions = c_immutable_extensions->Get();
            }

            callback_state_->Invoke(callbacks_.object_status_received, &headers, callbacks_.user_data);
        }
    }

    void SubgroupEnded(std::uint64_t group_id, std::uint64_t subgroup_id, bool reset) override
    {
        if (callbacks_.subgroup_ended) {
            callback_state_->Invoke(callbacks_.subgroup_ended, group_id, subgroup_id, reset, callbacks_.user_data);
        }
    }

    void DgramDataRecv(std::shared_ptr<const std::vector<uint8_t>> data) override
    {
        if (!callbacks_.datagram_received) {
            FetchTrackHandler::DgramDataRecv(std::move(data));
            return;
        }

        if (data) {
            callback_state_->Invoke(callbacks_.datagram_received,
                                    ToCType<quicr_byte_span_t>(BytesSpan{ data->data(), data->size() }),
                                    callbacks_.user_data);
        }
    }

  private:
    quicr_fetch_track_handler* owner_;
    std::shared_ptr<HandlerCallbackState> callback_state_;
    quicr_fetch_track_handler_callbacks_t callbacks_;
};

class PublishNamespaceHandlerAdapter : public quicr::PublishNamespaceHandler
{
  public:
    PublishNamespaceHandlerAdapter(quicr_publish_namespace_handler* owner,
                                   const quicr_publish_namespace_handler_callbacks_t& callbacks,
                                   const TrackNamespace& prefix)
      : PublishNamespaceHandler(prefix)
      , owner_(owner)
      , callback_state_(owner->callback_state)
      , callbacks_(callbacks)
    {
    }

    void StatusChanged(Status status) override
    {
        if (owner_ && callbacks_.status_changed) {
            callback_state_->Invoke(callbacks_.status_changed,
                                    owner_,
                                    ToCType<quicr_publish_namespace_handler_status_t>(status),
                                    callbacks_.user_data);
        }
    }

  private:
    quicr_publish_namespace_handler* owner_;
    std::shared_ptr<HandlerCallbackState> callback_state_;
    quicr_publish_namespace_handler_callbacks_t callbacks_;
};

class SubscribeNamespaceHandlerAdapter : public quicr::SubscribeNamespaceHandler
{
  public:
    SubscribeNamespaceHandlerAdapter(quicr_subscribe_namespace_handler* owner,
                                     const quicr_subscribe_namespace_handler_callbacks_t& callbacks,
                                     const TrackNamespace& prefix,
                                     Mode mode,
                                     const messages::Filter& filter)
      : SubscribeNamespaceHandler(prefix, mode, filter)
      , owner_(owner)
      , callback_state_(owner->callback_state)
      , callbacks_(callbacks)
    {
    }

    void StatusChanged(Status status) override
    {
        if (owner_ && callbacks_.status_changed) {
            callback_state_->Invoke(callbacks_.status_changed,
                                    owner_,
                                    ToCType<quicr_subscribe_namespace_handler_status_t>(status),
                                    callbacks_.user_data);
        }
    }

    void NamespaceReceived(const TrackNamespace& track_namespace) override
    {
        if (owner_ && callbacks_.namespace_received) {
            const auto c_track_namespace = ToCType<quicr_track_namespace_t>(track_namespace);
            callback_state_->Invoke(callbacks_.namespace_received, owner_, &c_track_namespace, callbacks_.user_data);
        }
    }

    void NamespaceDoneReceived(const TrackNamespace& track_namespace) override
    {
        if (owner_ && callbacks_.namespace_done_received) {
            const auto c_track_namespace = ToCType<quicr_track_namespace_t>(track_namespace);
            callback_state_->Invoke(
              callbacks_.namespace_done_received, owner_, &c_track_namespace, callbacks_.user_data);
        }
    }

  private:
    quicr_subscribe_namespace_handler* owner_;
    std::shared_ptr<HandlerCallbackState> callback_state_;
    quicr_subscribe_namespace_handler_callbacks_t callbacks_;
};

class TrackStatusHandlerAdapter : public quicr::TrackStatusHandler
{
  public:
    TrackStatusHandlerAdapter(quicr_track_status_track_handler* owner,
                              const quicr_track_status_handler_callbacks_t& callbacks,
                              const FullTrackName& full_track_name)
      : TrackStatusHandler(full_track_name)
      , owner_(owner)
      , callback_state_(owner->callback_state)
      , callbacks_(callbacks)
    {
    }

    void StatusChanged(Status status) override
    {
        if (owner_ && callbacks_.status_changed) {
            callback_state_->Invoke(callbacks_.status_changed,
                                    owner_,
                                    ToCType<quicr_track_status_handler_status_t>(status),
                                    callbacks_.user_data);
        }
    }

  private:
    quicr_track_status_track_handler* owner_;
    std::shared_ptr<HandlerCallbackState> callback_state_;
    quicr_track_status_handler_callbacks_t callbacks_;
};

template<typename CHandler>
quicr_status_t
DestroyHandler(CHandler* handler)
{
    if (!handler) {
        return QUICR_STATUS_OK;
    }

    if (!handler->impl || !handler->callback_state || handler->impl->IsRegistered()) {
        return QUICR_STATUS_BUSY;
    }

    if (!handler->callback_state->DisableCallbacks()) {
        return QUICR_STATUS_BUSY;
    }

    delete handler;
    return QUICR_STATUS_OK;
}

extern "C"
{
    quicr_status_t quicr_track_namespace_create(const quicr_byte_span_t* entries,
                                                size_t count,
                                                quicr_track_namespace_t** track_namespace)
    {
        return SafeCall([&] {
            if (!track_namespace || (!entries && count != 0)) {
                return QUICR_STATUS_INVALID_ARGUMENT;
            }

            *track_namespace = nullptr;
            std::vector<BytesSpan> cpp_entries;
            cpp_entries.reserve(count);
            for (std::size_t i = 0; i < count; ++i) {
                cpp_entries.push_back(ToCppType<BytesSpan>(entries[i]));
            }

            auto result =
              std::make_unique<quicr_track_namespace_t>(TrackNamespace{ std::span<const BytesSpan>{ cpp_entries } });
            *track_namespace = result.release();
            return QUICR_STATUS_OK;
        });
    }

    void quicr_track_namespace_destroy(quicr_track_namespace_t* track_namespace)
    {
        delete track_namespace;
    }

    size_t quicr_track_namespace_count(const quicr_track_namespace_t* track_namespace)
    {
        return track_namespace ? track_namespace->Get().GetEntries().size() : 0;
    }

    quicr_status_t quicr_track_namespace_entry(const quicr_track_namespace_t* track_namespace,
                                               size_t index,
                                               quicr_byte_span_t* entry)
    {
        return SafeCall([&] {
            if (!track_namespace || !entry) {
                return QUICR_STATUS_INVALID_ARGUMENT;
            }

            const auto& entries = track_namespace->Get().GetEntries();
            if (index >= entries.size()) {
                return QUICR_STATUS_INVALID_ARGUMENT;
            }

            *entry = ToCType<quicr_byte_span_t>(entries[index]);
            return QUICR_STATUS_OK;
        });
    }

    quicr_status_t quicr_full_track_name_create(const quicr_track_namespace_t* track_namespace,
                                                quicr_byte_span_t track_name,
                                                quicr_full_track_name_t** full_track_name)
    {
        return SafeCall([&] {
            if (!track_namespace || !full_track_name) {
                return QUICR_STATUS_INVALID_ARGUMENT;
            }

            *full_track_name = nullptr;
            auto result = std::make_unique<quicr_full_track_name_t>(
              FullTrackName{ track_namespace->Get(), ToCppType<Bytes>(track_name) });
            *full_track_name = result.release();
            return QUICR_STATUS_OK;
        });
    }

    void quicr_full_track_name_destroy(quicr_full_track_name_t* full_track_name)
    {
        delete full_track_name;
    }

    size_t quicr_full_track_name_namespace_count(const quicr_full_track_name_t* full_track_name)
    {
        return full_track_name ? full_track_name->Get().name_space.GetEntries().size() : 0;
    }

    quicr_status_t quicr_full_track_name_namespace_entry(const quicr_full_track_name_t* full_track_name,
                                                         size_t index,
                                                         quicr_byte_span_t* entry)
    {
        return SafeCall([&] {
            if (!full_track_name || !entry) {
                return QUICR_STATUS_INVALID_ARGUMENT;
            }

            const auto& entries = full_track_name->Get().name_space.GetEntries();
            if (index >= entries.size()) {
                return QUICR_STATUS_INVALID_ARGUMENT;
            }

            *entry = ToCType<quicr_byte_span_t>(entries[index]);
            return QUICR_STATUS_OK;
        });
    }

    quicr_byte_span_t quicr_full_track_name_track_name(const quicr_full_track_name_t* full_track_name)
    {
        return full_track_name ? ToCType<quicr_byte_span_t>(BytesSpan{ full_track_name->Get().name })
                               : quicr_byte_span_t{};
    }

    void quicr_transport_config_init(quicr_transport_config_t* config)
    {
        if (!config) {
            return;
        }

        const TransportConfig defaults{};
        *config = {
            .tls_cert_filename = "",
            .tls_key_filename = "",
            .time_queue_init_queue_size = defaults.time_queue_init_queue_size,
            .time_queue_max_duration = defaults.time_queue_max_duration,
            .time_queue_bucket_interval = defaults.time_queue_bucket_interval,
            .time_queue_rx_size = defaults.time_queue_rx_size,
            .debug = defaults.debug,
            .quic_cwin_minimum = defaults.quic_cwin_minimum,
            .quic_wifi_shadow_rtt_us = defaults.quic_wifi_shadow_rtt_us,
            .idle_timeout_ms = defaults.idle_timeout_ms,
            .congestion_control = ToCType<quicr_congestion_control_t>(defaults.congestion_control),
            .quic_qlog_path = "",
            .quic_priority_limit = defaults.quic_priority_limit,
            .max_connections = defaults.max_connections,
            .ssl_keylog = defaults.ssl_keylog,
            .socket_buffer_size = defaults.socket_buffer_size,
            .callback_queue_size = defaults.callback_queue_size,
            .metrics_sample_ms = defaults.metrics_sample_ms,
            .initial_max_stream_data = defaults.initial_max_stream_data,
            .initial_max_stream_id = defaults.initial_max_stream_id,
            .quic_shards = defaults.quic_shards,
            .use_af_xdp = defaults.use_af_xdp,
        };
    }

    void quicr_client_config_init(quicr_client_config_t* config)
    {
        if (!config) {
            return;
        }

        const ClientConfig defaults{};
        *config = {
            .endpoint_id = "",
            .unknown_stream_expiry_ms = defaults.unknown_stream_expiry_ms,
            .connect_uri = "",
            .tick_service_sleep_delay_us = defaults.tick_service_sleep_delay_us,
        };

        quicr_transport_config_init(&config->transport_config);
    }

    void quicr_server_config_init(quicr_server_config_t* config)
    {
        if (!config) {
            return;
        }

        const ServerConfig defaults{};
        *config = {
            .endpoint_id = "",
            .unknown_stream_expiry_ms = defaults.unknown_stream_expiry_ms,
            .bind_ip = "",
            .port = defaults.server_port,
            .tick_service_sleep_delay_us = defaults.tick_service_sleep_delay_us,
        };

        quicr_transport_config_init(&config->transport_config);
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

    quicr_status_t quicr_session_disconnect(quicr_session_t* session)
    {
        return SafeCall([&] {
            if (!session) {
                return QUICR_STATUS_INVALID_ARGUMENT;
            }

            const auto session_impl = session->impl.lock();
            if (!session_impl) {
                return QUICR_STATUS_NOT_FOUND;
            }

            session_impl->Disconnect();
            return QUICR_STATUS_OK;
        });
    }

    quicr_status_t quicr_session_goaway(quicr_session_t* session,
                                        const char* new_session_uri,
                                        uint64_t timeout_ms,
                                        const uint64_t* first_unprocessed_request_id)
    {
        return SafeCall([&] {
            if (!session || timeout_ms > static_cast<uint64_t>(std::chrono::milliseconds::max().count())) {
                return QUICR_STATUS_INVALID_ARGUMENT;
            }

            const auto session_impl = session->impl.lock();
            if (!session_impl) {
                return QUICR_STATUS_NOT_FOUND;
            }

            session_impl->GoAway(new_session_uri ? new_session_uri : "",
                                 std::chrono::milliseconds{ timeout_ms },
                                 first_unprocessed_request_id ? std::make_optional(*first_unprocessed_request_id)
                                                              : std::nullopt);
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
        properties->subgroup_id_mode = QUICR_SUBGROUP_ID_TYPE_IS_ZERO;
    }

    void quicr_location_init(quicr_location_t* location)
    {
        if (!location) {
            return;
        }

        *location = {};
    }

    quicr_status_t quicr_publish_track_handler_create(const quicr_publish_track_handler_callbacks_t* callbacks,
                                                      const quicr_full_track_name_t* ftn,
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
            result->callback_state = std::make_shared<HandlerCallbackState>();

            result->impl = std::make_shared<PublishTrackHandlerAdapter>(
              result.get(),
              callbacks ? *callbacks : quicr_publish_track_handler_callbacks_t{},
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

    quicr_status_t quicr_publish_track_handler_destroy(quicr_publish_track_handler_t* handler)
    {
        return DestroyHandler(handler);
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

    quicr_status_t quicr_publish_track_handler_end_subgroup(quicr_publish_track_handler_t* handler,
                                                            uint64_t group_id,
                                                            uint64_t subgroup_id,
                                                            bool completed)
    {
        return SafeCall([&] {
            if (!handler || !handler->impl) {
                return QUICR_STATUS_INVALID_ARGUMENT;
            }

            handler->impl->EndSubgroup(group_id, subgroup_id, completed);

            return QUICR_STATUS_OK;
        });
    }

    quicr_status_t quicr_publish_track_handler_end_all_subgroups(quicr_publish_track_handler_t* handler, bool completed)
    {
        return SafeCall([&] {
            if (!handler || !handler->impl) {
                return QUICR_STATUS_INVALID_ARGUMENT;
            }

            handler->impl->EndAllSubgroups(completed);

            return QUICR_STATUS_OK;
        });
    }

    void quicr_subscription_filter_init(quicr_subscription_filter_t* filter)
    {
        if (!filter) {
            return;
        }

        *filter = {};
        filter->type = QUICR_SUBSCRIPTION_FILTER_TYPE_NEXT_GROUP_START;
    }

    void quicr_track_filter_init(quicr_track_filter_t* filter)
    {
        if (!filter) {
            return;
        }

        *filter = {};
    }

    quicr_status_t quicr_subscribe_track_handler_create(const quicr_subscribe_track_handler_callbacks_t* callbacks,
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
            result->callback_state = std::make_shared<HandlerCallbackState>();
            result->impl = std::make_shared<SubscribeTrackHandlerAdapter>(
              result.get(),
              callbacks ? *callbacks : quicr_subscribe_track_handler_callbacks_t{},
              ToCppType<FullTrackName>(*ftn),
              priority,
              group_order == QUICR_GROUP_ORDER_UNSPECIFIED
                ? std::nullopt
                : std::make_optional(ToCppType<messages::GroupOrder>(group_order)),
              filter ? ToCppType<messages::Filter>(*filter) : messages::Filter{});

            *handler = result.release();
            return QUICR_STATUS_OK;
        });
    }

    quicr_status_t quicr_forwarding_subscribe_track_handler_create(
      const quicr_forwarding_subscribe_track_handler_callbacks_t* callbacks,
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
            result->callback_state = std::make_shared<HandlerCallbackState>();
            result->impl = std::make_shared<ForwardingSubscribeTrackHandlerAdapter>(
              result.get(),
              callbacks ? *callbacks : quicr_forwarding_subscribe_track_handler_callbacks_t{},
              ToCppType<FullTrackName>(*ftn),
              priority,
              group_order == QUICR_GROUP_ORDER_UNSPECIFIED
                ? std::nullopt
                : std::make_optional(ToCppType<messages::GroupOrder>(group_order)),
              filter ? ToCppType<messages::Filter>(*filter) : messages::Filter{});

            *handler = result.release();
            return QUICR_STATUS_OK;
        });
    }

    quicr_status_t quicr_subscribe_track_handler_destroy(quicr_subscribe_track_handler_t* handler)
    {
        return DestroyHandler(handler);
    }

    quicr_status_t quicr_subscribe_track_handler_get_status(const quicr_subscribe_track_handler_t* handler,
                                                            quicr_subscribe_track_handler_status_t* status)
    {
        return SafeCall([&] {
            if (!handler || !handler->impl || !status) {
                return QUICR_STATUS_INVALID_ARGUMENT;
            }

            *status = ToCType<quicr_subscribe_track_handler_status_t>(handler->impl->GetStatus());
            return QUICR_STATUS_OK;
        });
    }

    quicr_status_t quicr_subscribe_track_handler_pause(quicr_subscribe_track_handler_t* handler)
    {
        return SafeCall([&] {
            if (!handler || !handler->impl) {
                return QUICR_STATUS_INVALID_ARGUMENT;
            }

            handler->impl->Pause();

            return QUICR_STATUS_OK;
        });
    }

    quicr_status_t quicr_subscribe_track_handler_resume(quicr_subscribe_track_handler_t* handler)
    {
        return SafeCall([&] {
            if (!handler || !handler->impl) {
                return QUICR_STATUS_INVALID_ARGUMENT;
            }

            handler->impl->Resume();

            return QUICR_STATUS_OK;
        });
    }

    quicr_status_t quicr_subscribe_track_handler_request_new_group(quicr_subscribe_track_handler_t* handler,
                                                                   uint64_t group_id)
    {
        return SafeCall([&] {
            if (!handler || !handler->impl) {
                return QUICR_STATUS_INVALID_ARGUMENT;
            }

            handler->impl->RequestNewGroup(group_id);

            return QUICR_STATUS_OK;
        });
    }

    quicr_status_t quicr_subscribe_track_handler_support_new_group_request(quicr_subscribe_track_handler_t* handler,
                                                                           bool supported)
    {
        return SafeCall([&] {
            if (!handler || !handler->impl) {
                return QUICR_STATUS_INVALID_ARGUMENT;
            }

            handler->impl->SupportNewGroupRequest(supported);

            return QUICR_STATUS_OK;
        });
    }

    quicr_status_t quicr_session_update_subscribe_track_handler(quicr_session_t* session,
                                                                quicr_subscribe_track_handler_t* handler)
    {
        return SafeCall([&] {
            if (!session || !handler || !handler->impl) {
                return QUICR_STATUS_INVALID_ARGUMENT;
            }

            const auto session_impl = session->impl.lock();
            if (!session_impl) {
                return QUICR_STATUS_NOT_FOUND;
            }

            session_impl->UpdateTrackSubscription(handler->impl);
            return QUICR_STATUS_OK;
        });
    }

    quicr_status_t quicr_fetch_track_handler_create(const quicr_fetch_track_handler_callbacks_t* callbacks,
                                                    const quicr_full_track_name_t* ftn,
                                                    uint8_t priority,
                                                    const quicr_location_t* start_location,
                                                    const quicr_fetch_end_location_t* end_location,
                                                    quicr_group_order_t group_order,
                                                    quicr_fetch_track_handler_t** handler)
    {
        return SafeCall([&] {
            if (!handler || !ftn || !start_location || !end_location) {
                return QUICR_STATUS_INVALID_ARGUMENT;
            }

            *handler = nullptr;
            auto result = std::make_unique<quicr_fetch_track_handler>();
            result->callback_state = std::make_shared<HandlerCallbackState>();
            result->impl = std::make_shared<FetchTrackHandlerAdapter>(
              result.get(),
              callbacks ? *callbacks : quicr_fetch_track_handler_callbacks_t{},
              ToCppType<FullTrackName>(*ftn),
              priority,
              ToCppType<messages::Location>(*start_location),
              ToCppType<messages::FetchEndLocation>(*end_location),
              ToCppType<messages::GroupOrder>(group_order));

            *handler = result.release();
            return QUICR_STATUS_OK;
        });
    }

    quicr_status_t quicr_fetch_track_handler_destroy(quicr_fetch_track_handler_t* handler)
    {
        return DestroyHandler(handler);
    }

    quicr_status_t quicr_fetch_track_handler_get_status(const quicr_fetch_track_handler_t* handler,
                                                        quicr_fetch_track_handler_status_t* status)
    {
        return SafeCall([&] {
            if (!handler || !handler->impl || !status) {
                return QUICR_STATUS_INVALID_ARGUMENT;
            }

            *status = ToCType<quicr_fetch_track_handler_status_t>(handler->impl->GetStatus());
            return QUICR_STATUS_OK;
        });
    }

    quicr_status_t quicr_fetch_track_handler_pause(quicr_fetch_track_handler_t* handler)
    {
        return SafeCall([&] {
            if (!handler || !handler->impl) {
                return QUICR_STATUS_INVALID_ARGUMENT;
            }

            handler->impl->Pause();

            return QUICR_STATUS_OK;
        });
    }

    quicr_status_t quicr_fetch_track_handler_resume(quicr_fetch_track_handler_t* handler)
    {
        return SafeCall([&] {
            if (!handler || !handler->impl) {
                return QUICR_STATUS_INVALID_ARGUMENT;
            }

            handler->impl->Resume();

            return QUICR_STATUS_OK;
        });
    }

    quicr_status_t quicr_fetch_track_handler_request_new_group(quicr_fetch_track_handler_t* handler, uint64_t group_id)
    {
        return SafeCall([&] {
            if (!handler || !handler->impl) {
                return QUICR_STATUS_INVALID_ARGUMENT;
            }

            handler->impl->RequestNewGroup(group_id);

            return QUICR_STATUS_OK;
        });
    }

    quicr_status_t quicr_fetch_track_handler_support_new_group_request(quicr_fetch_track_handler_t* handler,
                                                                       bool supported)
    {
        return SafeCall([&] {
            if (!handler || !handler->impl) {
                return QUICR_STATUS_INVALID_ARGUMENT;
            }

            handler->impl->SupportNewGroupRequest(supported);

            return QUICR_STATUS_OK;
        });
    }

    quicr_status_t quicr_publish_namespace_handler_create(const quicr_publish_namespace_handler_callbacks_t* callbacks,
                                                          const quicr_track_namespace_t* prefix,
                                                          quicr_publish_namespace_handler_t** handler)
    {
        return SafeCall([&] {
            if (!handler || !prefix) {
                return QUICR_STATUS_INVALID_ARGUMENT;
            }

            *handler = nullptr;
            auto result = std::make_unique<quicr_publish_namespace_handler>();
            result->callback_state = std::make_shared<HandlerCallbackState>();
            result->impl = std::make_shared<PublishNamespaceHandlerAdapter>(
              result.get(),
              callbacks ? *callbacks : quicr_publish_namespace_handler_callbacks_t{},
              ToCppType<TrackNamespace>(*prefix));

            *handler = result.release();
            return QUICR_STATUS_OK;
        });
    }

    quicr_status_t quicr_publish_namespace_handler_destroy(quicr_publish_namespace_handler_t* handler)
    {
        return DestroyHandler(handler);
    }

    quicr_status_t quicr_publish_namespace_handler_get_status(const quicr_publish_namespace_handler_t* handler,
                                                              quicr_publish_namespace_handler_status_t* status)
    {
        return SafeCall([&] {
            if (!handler || !handler->impl || !status) {
                return QUICR_STATUS_INVALID_ARGUMENT;
            }

            *status = ToCType<quicr_publish_namespace_handler_status_t>(handler->impl->GetStatus());
            return QUICR_STATUS_OK;
        });
    }

    quicr_status_t quicr_publish_namespace_handler_get_error(const quicr_publish_namespace_handler_t* handler,
                                                             quicr_error_code_t* code,
                                                             char* reason,
                                                             size_t* reason_size)
    {
        return SafeCall([&]() -> quicr_status_t {
            if (!handler || !handler->impl) {
                return QUICR_STATUS_INVALID_ARGUMENT;
            }

            return CopyHandlerError(handler->impl->GetError(), code, reason, reason_size);
        });
    }

    quicr_status_t quicr_publish_namespace_handler_publish_track(quicr_publish_namespace_handler_t* handler,
                                                                 const quicr_publish_track_handler_t* track_handler)
    {
        return SafeCall([&] {
            if (!handler || !handler->impl || !track_handler || !track_handler->impl) {
                return QUICR_STATUS_INVALID_ARGUMENT;
            }

            handler->impl->PublishTrack(track_handler->impl);

            return QUICR_STATUS_OK;
        });
    }

    quicr_status_t quicr_publish_namespace_handler_unpublish_track(quicr_publish_namespace_handler_t* handler,
                                                                   const quicr_publish_track_handler_t* track_handler)
    {
        return SafeCall([&] {
            if (!handler || !handler->impl || !track_handler || !track_handler->impl) {
                return QUICR_STATUS_INVALID_ARGUMENT;
            }

            handler->impl->UnPublishTrack(track_handler->impl);

            return QUICR_STATUS_OK;
        });
    }

    quicr_status_t quicr_subscribe_namespace_handler_create(
      const quicr_subscribe_namespace_handler_callbacks_t* callbacks,
      const quicr_track_namespace_t* prefix,
      quicr_subscribe_namespace_mode_t mode,
      const quicr_filter_t* filter,
      quicr_subscribe_namespace_handler_t** handler)
    {
        return SafeCall([&] {
            if (!handler || !prefix) {
                return QUICR_STATUS_INVALID_ARGUMENT;
            }

            *handler = nullptr;
            auto result = std::make_unique<quicr_subscribe_namespace_handler>();
            result->callback_state = std::make_shared<HandlerCallbackState>();
            result->impl = std::make_shared<SubscribeNamespaceHandlerAdapter>(
              result.get(),
              callbacks ? *callbacks : quicr_subscribe_namespace_handler_callbacks_t{},
              ToCppType<TrackNamespace>(*prefix),
              ToCppType<SubscribeNamespaceHandler::Mode>(mode),
              filter ? ToCppType<messages::Filter>(*filter) : messages::Filter{});

            *handler = result.release();
            return QUICR_STATUS_OK;
        });
    }

    quicr_status_t quicr_subscribe_namespace_handler_destroy(quicr_subscribe_namespace_handler_t* handler)
    {
        return DestroyHandler(handler);
    }

    quicr_status_t quicr_subscribe_namespace_handler_get_status(const quicr_subscribe_namespace_handler_t* handler,
                                                                quicr_subscribe_namespace_handler_status_t* status)
    {
        return SafeCall([&] {
            if (!handler || !handler->impl || !status) {
                return QUICR_STATUS_INVALID_ARGUMENT;
            }

            *status = ToCType<quicr_subscribe_namespace_handler_status_t>(handler->impl->GetStatus());
            return QUICR_STATUS_OK;
        });
    }

    quicr_status_t quicr_subscribe_namespace_handler_get_error(const quicr_subscribe_namespace_handler_t* handler,
                                                               quicr_error_code_t* code,
                                                               char* reason,
                                                               size_t* reason_size)
    {
        return SafeCall([&]() -> quicr_status_t {
            if (!handler || !handler->impl) {
                return QUICR_STATUS_INVALID_ARGUMENT;
            }

            return CopyHandlerError(handler->impl->GetError(), code, reason, reason_size);
        });
    }

    quicr_status_t quicr_track_status_handler_create(const quicr_track_status_handler_callbacks_t* callbacks,
                                                     const quicr_full_track_name_t* ftn,
                                                     quicr_track_status_track_handler_t** handler)
    {
        return SafeCall([&] {
            if (!handler || !ftn) {
                return QUICR_STATUS_INVALID_ARGUMENT;
            }

            *handler = nullptr;
            auto result = std::make_unique<quicr_track_status_track_handler>();
            result->callback_state = std::make_shared<HandlerCallbackState>();
            result->impl = std::make_shared<TrackStatusHandlerAdapter>(
              result.get(),
              callbacks ? *callbacks : quicr_track_status_handler_callbacks_t{},
              ToCppType<FullTrackName>(*ftn));

            *handler = result.release();
            return QUICR_STATUS_OK;
        });
    }

    quicr_status_t quicr_track_status_handler_destroy(quicr_track_status_track_handler_t* handler)
    {
        return DestroyHandler(handler);
    }

    quicr_status_t quicr_track_status_handler_get_status(const quicr_track_status_track_handler_t* handler,
                                                         quicr_track_status_handler_status_t* status)
    {
        return SafeCall([&] {
            if (!handler || !handler->impl || !status) {
                return QUICR_STATUS_INVALID_ARGUMENT;
            }

            *status = ToCType<quicr_track_status_handler_status_t>(handler->impl->GetStatus());

            return QUICR_STATUS_OK;
        });
    }

    quicr_status_t quicr_track_status_handler_get_response(const quicr_track_status_track_handler_t* handler,
                                                           bool* has_response,
                                                           quicr_track_status_response_t* response,
                                                           quicr_track_extensions_t* track_extensions)
    {
        return SafeCall([&] {
            if (!handler || !handler->impl || !has_response || !response) {
                return QUICR_STATUS_INVALID_ARGUMENT;
            }

            *response = {};
            const auto cpp_response = handler->impl->GetResponse();
            *has_response = cpp_response.has_value();
            if (cpp_response) {
                response->has_largest_location = cpp_response->largest_location.has_value();
                if (cpp_response->largest_location) {
                    response->largest_location = ToCType<quicr_location_t>(*cpp_response->largest_location);
                }

                if (track_extensions) {
                    track_extensions->GetMutable() = cpp_response->track_properties;
                    response->track_extensions = track_extensions;
                }
            }

            return QUICR_STATUS_OK;
        });
    }

    quicr_status_t quicr_track_status_handler_get_error(const quicr_track_status_track_handler_t* handler,
                                                        quicr_error_code_t* code,
                                                        char* reason,
                                                        size_t* reason_size)
    {
        return SafeCall([&]() -> quicr_status_t {
            if (!handler || !handler->impl) {
                return QUICR_STATUS_INVALID_ARGUMENT;
            }

            return CopyHandlerError(handler->impl->GetError(), code, reason, reason_size);
        });
    }

    quicr_status_t quicr_track_extensions_create(quicr_track_extensions_t** extensions)
    {
        return SafeCall([&] {
            if (!extensions) {
                return QUICR_STATUS_INVALID_ARGUMENT;
            }

            *extensions = new quicr_track_extensions_t{};
            return QUICR_STATUS_OK;
        });
    }

    void quicr_track_extensions_destroy(quicr_track_extensions_t* extensions)
    {
        delete extensions;
    }

    quicr_status_t quicr_track_extensions_add(quicr_track_extensions_t* extensions,
                                              uint64_t type,
                                              quicr_byte_span_t value)
    {
        return SafeCall([&] {
            if (!extensions) {
                return QUICR_STATUS_INVALID_ARGUMENT;
            }

            extensions->GetMutable().extensions[type] = ToCppType<Bytes>(value);
            return QUICR_STATUS_OK;
        });
    }

    quicr_status_t quicr_track_extensions_add_immutable(quicr_track_extensions_t* extensions,
                                                        uint64_t type,
                                                        quicr_byte_span_t value)
    {
        return SafeCall([&] {
            if (!extensions) {
                return QUICR_STATUS_INVALID_ARGUMENT;
            }

            extensions->GetMutable().immutable_extensions[type] = ToCppType<Bytes>(value);
            return QUICR_STATUS_OK;
        });
    }

    size_t quicr_track_extensions_count(const quicr_track_extensions_t* extensions)
    {
        return extensions ? extensions->Get().extensions.size() : 0;
    }

    size_t quicr_track_extensions_immutable_count(const quicr_track_extensions_t* extensions)
    {
        return extensions ? extensions->Get().immutable_extensions.size() : 0;
    }

    quicr_status_t quicr_track_extensions_entry(const quicr_track_extensions_t* extensions,
                                                size_t index,
                                                uint64_t* type,
                                                quicr_byte_span_t* value)
    {
        return SafeCall([&] {
            if (!extensions || !type || !value || index >= extensions->Get().extensions.size()) {
                return QUICR_STATUS_INVALID_ARGUMENT;
            }

            const auto& entries = extensions->Get().extensions;
            const auto entry = std::next(entries.begin(), static_cast<std::ptrdiff_t>(index));
            *type = entry->first;
            *value = ToCType<quicr_byte_span_t>(BytesSpan{ entry->second });
            return QUICR_STATUS_OK;
        });
    }

    quicr_status_t quicr_track_extensions_immutable_entry(const quicr_track_extensions_t* extensions,
                                                          size_t index,
                                                          uint64_t* type,
                                                          quicr_byte_span_t* value)
    {
        return SafeCall([&] {
            if (!extensions || !type || !value || index >= extensions->Get().immutable_extensions.size()) {
                return QUICR_STATUS_INVALID_ARGUMENT;
            }

            const auto& entries = extensions->Get().immutable_extensions;
            const auto entry = std::next(entries.begin(), static_cast<std::ptrdiff_t>(index));
            *type = entry->first;
            *value = ToCType<quicr_byte_span_t>(BytesSpan{ entry->second });
            return QUICR_STATUS_OK;
        });
    }

    const quicr_full_track_name_t* quicr_publish_attributes_full_track_name(
      const quicr_publish_attributes_t* attributes)
    {
        return attributes ? &attributes->full_track_name : nullptr;
    }

    uint64_t quicr_publish_attributes_track_alias(const quicr_publish_attributes_t* attributes)
    {
        return attributes ? attributes->impl.track_alias : 0;
    }

    size_t quicr_publish_attributes_auth_token_count(const quicr_publish_attributes_t* attributes)
    {
        return attributes ? attributes->impl.auth_tokens.size() : 0;
    }

    quicr_status_t quicr_publish_attributes_auth_token(const quicr_publish_attributes_t* attributes,
                                                       size_t index,
                                                       quicr_token_t* token)
    {
        return SafeCall([&] {
            if (!attributes || !token || index >= attributes->impl.auth_tokens.size()) {
                return QUICR_STATUS_INVALID_ARGUMENT;
            }

            const auto& value = attributes->impl.auth_tokens[index];
            *token = {
                .alias_type = static_cast<quicr_token_alias_type_t>(value.alias_type),
                .has_token_alias = value.token_alias.has_value(),
                .token_alias = value.token_alias.value_or(0),
                .has_token_type = value.token_type.has_value(),
                .token_type = value.token_type.value_or(0),
                .token_value = ToCType<quicr_byte_span_t>(BytesSpan{ value.token_value }),
            };

            return QUICR_STATUS_OK;
        });
    }

    quicr_status_t quicr_publish_attributes_expires(const quicr_publish_attributes_t* attributes,
                                                    bool* has_expires,
                                                    uint64_t* expires)
    {
        return SafeCall([&] {
            if (!attributes || !has_expires || !expires) {
                return QUICR_STATUS_INVALID_ARGUMENT;
            }

            *has_expires = attributes->impl.expires.has_value();
            *expires = attributes->impl.expires.value_or(0);

            return QUICR_STATUS_OK;
        });
    }

    quicr_status_t quicr_publish_attributes_largest_object(const quicr_publish_attributes_t* attributes,
                                                           bool* has_largest_object,
                                                           quicr_location_t* largest_object)
    {
        return SafeCall([&] {
            if (!attributes || !has_largest_object || !largest_object) {
                return QUICR_STATUS_INVALID_ARGUMENT;
            }

            *has_largest_object = attributes->impl.largest_object.has_value();
            *largest_object = attributes->impl.largest_object
                                ? ToCType<quicr_location_t>(*attributes->impl.largest_object)
                                : quicr_location_t{};

            return QUICR_STATUS_OK;
        });
    }

    bool quicr_publish_attributes_forward(const quicr_publish_attributes_t* attributes)
    {
        return attributes && attributes->impl.forward;
    }

    quicr_status_t quicr_publish_attributes_group_order(const quicr_publish_attributes_t* attributes,
                                                        quicr_group_order_t* group_order)
    {
        return SafeCall([&] {
            if (!attributes || !group_order) {
                return QUICR_STATUS_INVALID_ARGUMENT;
            }

            *group_order = ToCType<quicr_group_order_t>(attributes->impl.default_publisher_group_order);

            return QUICR_STATUS_OK;
        });
    }

    bool quicr_publish_attributes_dynamic_groups(const quicr_publish_attributes_t* attributes)
    {
        return attributes && attributes->impl.dynamic_groups;
    }

    uint8_t quicr_publish_attributes_publisher_priority(const quicr_publish_attributes_t* attributes)
    {
        return attributes ? attributes->impl.publisher_priority : 0;
    }

    const quicr_track_extensions_t* quicr_publish_attributes_track_extensions(
      const quicr_publish_attributes_t* attributes)
    {
        return attributes ? &attributes->track_extensions : nullptr;
    }

    quicr_status_t quicr_publish_attributes_max_cache_duration(const quicr_publish_attributes_t* attributes,
                                                               bool* has_max_cache_duration,
                                                               uint64_t* max_cache_duration)
    {
        return SafeCall([&] {
            if (!attributes || !has_max_cache_duration || !max_cache_duration) {
                return QUICR_STATUS_INVALID_ARGUMENT;
            }

            *has_max_cache_duration = attributes->impl.max_cache_duration.has_value();
            *max_cache_duration = attributes->impl.max_cache_duration.value_or(0);

            return QUICR_STATUS_OK;
        });
    }

    quicr_status_t quicr_publish_attributes_delivery_timeout(const quicr_publish_attributes_t* attributes,
                                                             bool* has_delivery_timeout,
                                                             uint64_t* delivery_timeout)
    {
        return SafeCall([&] {
            if (!attributes || !has_delivery_timeout || !delivery_timeout) {
                return QUICR_STATUS_INVALID_ARGUMENT;
            }

            *has_delivery_timeout = attributes->impl.delivery_timeout.has_value();
            *delivery_timeout = attributes->impl.delivery_timeout.value_or(0);

            return QUICR_STATUS_OK;
        });
    }

    uint8_t quicr_subscribe_attributes_priority(const quicr_subscribe_attributes_t* attributes)
    {
        return attributes ? attributes->impl.priority : 0;
    }

    quicr_status_t quicr_subscribe_attributes_group_order(const quicr_subscribe_attributes_t* attributes,
                                                          bool* has_group_order,
                                                          quicr_group_order_t* group_order)
    {
        return SafeCall([&] {
            if (!attributes || !has_group_order || !group_order) {
                return QUICR_STATUS_INVALID_ARGUMENT;
            }

            *has_group_order = attributes->impl.group_order.has_value();
            *group_order = attributes->impl.group_order ? ToCType<quicr_group_order_t>(*attributes->impl.group_order)
                                                        : QUICR_GROUP_ORDER_ASCENDING;

            return QUICR_STATUS_OK;
        });
    }

    quicr_status_t quicr_subscribe_attributes_publisher_group_order(const quicr_subscribe_attributes_t* attributes,
                                                                    quicr_group_order_t* group_order)
    {
        return SafeCall([&] {
            if (!attributes || !group_order) {
                return QUICR_STATUS_INVALID_ARGUMENT;
            }

            *group_order = ToCType<quicr_group_order_t>(attributes->impl.publisher_default_group_order);

            return QUICR_STATUS_OK;
        });
    }

    uint64_t quicr_subscribe_attributes_delivery_timeout(const quicr_subscribe_attributes_t* attributes)
    {
        return attributes ? static_cast<uint64_t>(attributes->impl.delivery_timeout.count()) : 0;
    }

    uint64_t quicr_subscribe_attributes_expires(const quicr_subscribe_attributes_t* attributes)
    {
        return attributes ? static_cast<uint64_t>(attributes->impl.expires.count()) : 0;
    }

    bool quicr_subscribe_attributes_forward(const quicr_subscribe_attributes_t* attributes)
    {
        return attributes && attributes->impl.forward != 0;
    }

    quicr_status_t quicr_subscribe_attributes_new_group_request_id(const quicr_subscribe_attributes_t* attributes,
                                                                   bool* has_new_group_request_id,
                                                                   uint64_t* new_group_request_id)
    {
        return SafeCall([&] {
            if (!attributes || !has_new_group_request_id || !new_group_request_id) {
                return QUICR_STATUS_INVALID_ARGUMENT;
            }

            *has_new_group_request_id = attributes->impl.new_group_request_id.has_value();
            *new_group_request_id = attributes->impl.new_group_request_id.value_or(0);

            return QUICR_STATUS_OK;
        });
    }

    bool quicr_subscribe_attributes_is_publisher_initiated(const quicr_subscribe_attributes_t* attributes)
    {
        return attributes && attributes->impl.is_publisher_initiated;
    }

    quicr_location_t quicr_subscribe_attributes_start_location(const quicr_subscribe_attributes_t* attributes)
    {
        return attributes ? ToCType<quicr_location_t>(attributes->impl.start_location) : quicr_location_t{};
    }

    uint64_t quicr_subscribe_attributes_rendezvous_timeout(const quicr_subscribe_attributes_t* attributes)
    {
        return attributes ? static_cast<uint64_t>(attributes->impl.rendezvous_timeout.count()) : 0;
    }

    uint64_t quicr_subscribe_namespace_attributes_request_id(const quicr_subscribe_namespace_attributes_t* attributes)
    {
        return attributes ? attributes->impl.request_id : 0;
    }

    quicr_status_t quicr_publish_response_create(quicr_subscribe_track_handler_t* handler,
                                                 quicr_publish_response_t** response)
    {
        return SafeCall([&] {
            if (!handler || !handler->impl || !response) {
                return QUICR_STATUS_INVALID_ARGUMENT;
            }

            *response = nullptr;
            auto result = std::make_unique<quicr_publish_response_t>();
            result->impl = handler->impl;
            *response = result.release();

            return QUICR_STATUS_OK;
        });
    }

    void quicr_publish_response_destroy(quicr_publish_response_t* response)
    {
        delete response;
    }

    quicr_status_t quicr_publish_response_set_subscriber_priority(quicr_publish_response_t* response, uint8_t priority)
    {
        return SafeCall([&] {
            if (!response) {
                return QUICR_STATUS_INVALID_ARGUMENT;
            }
            response->subscriber_priority = priority;
            return QUICR_STATUS_OK;
        });
    }

    quicr_status_t quicr_publish_response_set_group_order(quicr_publish_response_t* response,
                                                          quicr_group_order_t group_order)
    {
        return SafeCall([&] {
            if (!response) {
                return QUICR_STATUS_INVALID_ARGUMENT;
            }
            response->group_order = group_order == QUICR_GROUP_ORDER_UNSPECIFIED
                                      ? std::nullopt
                                      : std::optional{ ToCppType<messages::GroupOrder>(group_order) };
            return QUICR_STATUS_OK;
        });
    }

    quicr_status_t quicr_publish_response_set_filter(quicr_publish_response_t* response, const quicr_filter_t* filter)
    {
        return SafeCall([&] {
            if (!response || !filter) {
                return QUICR_STATUS_INVALID_ARGUMENT;
            }
            response->filter = ToCppType<messages::Filter>(*filter);
            return QUICR_STATUS_OK;
        });
    }

    quicr_status_t quicr_publish_response_set_forward(quicr_publish_response_t* response, bool forward)
    {
        return SafeCall([&] {
            if (!response) {
                return QUICR_STATUS_INVALID_ARGUMENT;
            }
            response->forward = forward;
            return QUICR_STATUS_OK;
        });
    }

    quicr_status_t quicr_publish_response_set_subgroup_delivery_timeout(quicr_publish_response_t* response,
                                                                        uint64_t timeout_ms)
    {
        return SafeCall([&] {
            if (!response ||
                timeout_ms > static_cast<std::uint64_t>(std::numeric_limits<std::chrono::milliseconds::rep>::max())) {
                return QUICR_STATUS_INVALID_ARGUMENT;
            }
            response->subgroup_delivery_timeout = std::chrono::milliseconds{ timeout_ms };
            return QUICR_STATUS_OK;
        });
    }

    quicr_status_t quicr_publish_response_set_object_delivery_timeout(quicr_publish_response_t* response,
                                                                      uint64_t timeout_ms)
    {
        return SafeCall([&] {
            if (!response ||
                timeout_ms > static_cast<std::uint64_t>(std::numeric_limits<std::chrono::milliseconds::rep>::max())) {
                return QUICR_STATUS_INVALID_ARGUMENT;
            }
            response->object_delivery_timeout = std::chrono::milliseconds{ timeout_ms };
            return QUICR_STATUS_OK;
        });
    }

    quicr_status_t quicr_publish_response_set_new_group_request_id(quicr_publish_response_t* response,
                                                                   uint64_t request_id)
    {
        return SafeCall([&] {
            if (!response) {
                return QUICR_STATUS_INVALID_ARGUMENT;
            }
            response->new_group_request_id = request_id;
            return QUICR_STATUS_OK;
        });
    }
}

namespace {
    template<typename CHandler>
    quicr_status_t AddHandler(quicr_session_manager_t* manager, quicr_session_t* session, const CHandler* handler)
    {
        return SafeCall([&] {
            if (!manager || !manager->impl || !session || !handler || !handler->impl || !handler->callback_state) {
                return QUICR_STATUS_INVALID_ARGUMENT;
            }

            auto session_impl = session->impl.lock();
            if (!session_impl) {
                return QUICR_STATUS_INVALID_ARGUMENT;
            }

            handler->callback_state->EnableCallbacks();
            try {
                manager->impl->AddHandler(session_impl, handler->impl);
            } catch (...) {
                if (!handler->impl->IsRegistered()) {
                    handler->callback_state->DisableCallbacks();
                }
                throw;
            }

            return QUICR_STATUS_OK;
        });
    }

    template<typename CHandler>
    quicr_status_t RemoveHandler(quicr_session_manager_t* manager, quicr_session_t* session, const CHandler* handler)
    {
        return SafeCall([&] {
            if (!manager || !manager->impl || !session || !handler || !handler->impl || !handler->callback_state) {
                return QUICR_STATUS_INVALID_ARGUMENT;
            }

            auto session_impl = session->impl.lock();
            if (!session_impl) {
                return QUICR_STATUS_INVALID_ARGUMENT;
            }

            manager->impl->RemoveHandler(session_impl, handler->impl);
            handler->callback_state->DisableCallbacks();

            return QUICR_STATUS_OK;
        });
    }

    template<typename CHandler>
    quicr_status_t GoAwayHandler(quicr_session_t* session,
                                 const char* new_session_uri,
                                 uint64_t timeout_ms,
                                 const CHandler* handler)
    {
        return SafeCall([&] {
            if (!session || !handler || !handler->impl ||
                timeout_ms > static_cast<uint64_t>(std::chrono::milliseconds::max().count())) {
                return QUICR_STATUS_INVALID_ARGUMENT;
            }

            const auto session_impl = session->impl.lock();
            if (!session_impl) {
                return QUICR_STATUS_NOT_FOUND;
            }

            session_impl->GoAway(
              new_session_uri ? new_session_uri : "", std::chrono::milliseconds{ timeout_ms }, handler->impl);
            return QUICR_STATUS_OK;
        });
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
    return AddHandler(manager, session, handler);
}

quicr_status_t
quicr_session_manager_remove_publish_track_handler(quicr_session_manager_t* manager,
                                                   quicr_session_t* session,
                                                   const quicr_publish_track_handler_t* handler)
{
    return RemoveHandler(manager, session, handler);
}

quicr_status_t
quicr_session_manager_add_subscribe_track_handler(quicr_session_manager_t* manager,
                                                  quicr_session_t* session,
                                                  const quicr_subscribe_track_handler_t* handler)
{
    return AddHandler(manager, session, handler);
}

quicr_status_t
quicr_session_manager_remove_subscribe_track_handler(quicr_session_manager_t* manager,
                                                     quicr_session_t* session,
                                                     const quicr_subscribe_track_handler_t* handler)
{
    return RemoveHandler(manager, session, handler);
}

quicr_status_t
quicr_session_manager_add_fetch_track_handler(quicr_session_manager_t* manager,
                                              quicr_session_t* session,
                                              const quicr_fetch_track_handler_t* handler)
{
    return AddHandler(manager, session, handler);
}

quicr_status_t
quicr_session_manager_remove_fetch_track_handler(quicr_session_manager_t* manager,
                                                 quicr_session_t* session,
                                                 const quicr_fetch_track_handler_t* handler)
{
    return RemoveHandler(manager, session, handler);
}

quicr_status_t
quicr_session_manager_add_publish_namespace_handler(quicr_session_manager_t* manager,
                                                    quicr_session_t* session,
                                                    const quicr_publish_namespace_handler_t* handler)
{
    return AddHandler(manager, session, handler);
}

quicr_status_t
quicr_session_manager_remove_publish_namespace_handler(quicr_session_manager_t* manager,
                                                       quicr_session_t* session,
                                                       const quicr_publish_namespace_handler_t* handler)
{
    return RemoveHandler(manager, session, handler);
}

quicr_status_t
quicr_session_manager_add_subscribe_namespace_handler(quicr_session_manager_t* manager,
                                                      quicr_session_t* session,
                                                      const quicr_subscribe_namespace_handler_t* handler)
{
    return AddHandler(manager, session, handler);
}

quicr_status_t
quicr_session_manager_remove_subscribe_namespace_handler(quicr_session_manager_t* manager,
                                                         quicr_session_t* session,
                                                         const quicr_subscribe_namespace_handler_t* handler)
{
    return RemoveHandler(manager, session, handler);
}

quicr_status_t
quicr_session_manager_add_track_status_track_handler(quicr_session_manager_t* manager,
                                                     quicr_session_t* session,
                                                     const quicr_track_status_track_handler_t* handler)
{
    return AddHandler(manager, session, handler);
}

quicr_status_t
quicr_session_manager_remove_track_status_track_handler(quicr_session_manager_t* manager,
                                                        quicr_session_t* session,
                                                        const quicr_track_status_track_handler_t* handler)
{
    return RemoveHandler(manager, session, handler);
}

quicr_status_t
quicr_session_goaway_publish_track_handler(quicr_session_t* session,
                                           const char* new_session_uri,
                                           uint64_t timeout_ms,
                                           const quicr_publish_track_handler_t* handler)
{
    return GoAwayHandler(session, new_session_uri, timeout_ms, handler);
}

quicr_status_t
quicr_session_goaway_subscribe_track_handler(quicr_session_t* session,
                                             const char* new_session_uri,
                                             uint64_t timeout_ms,
                                             const quicr_subscribe_track_handler_t* handler)
{
    return GoAwayHandler(session, new_session_uri, timeout_ms, handler);
}

quicr_status_t
quicr_session_goaway_fetch_track_handler(quicr_session_t* session,
                                         const char* new_session_uri,
                                         uint64_t timeout_ms,
                                         const quicr_fetch_track_handler_t* handler)
{
    return GoAwayHandler(session, new_session_uri, timeout_ms, handler);
}

quicr_status_t
quicr_session_goaway_publish_namespace_handler(quicr_session_t* session,
                                               const char* new_session_uri,
                                               uint64_t timeout_ms,
                                               const quicr_publish_namespace_handler_t* handler)
{
    return GoAwayHandler(session, new_session_uri, timeout_ms, handler);
}

quicr_status_t
quicr_session_goaway_subscribe_namespace_handler(quicr_session_t* session,
                                                 const char* new_session_uri,
                                                 uint64_t timeout_ms,
                                                 const quicr_subscribe_namespace_handler_t* handler)
{
    return GoAwayHandler(session, new_session_uri, timeout_ms, handler);
}

quicr_status_t
quicr_session_goaway_track_status_handler(quicr_session_t* session,
                                          const char* new_session_uri,
                                          uint64_t timeout_ms,
                                          const quicr_track_status_track_handler_t* handler)
{
    return GoAwayHandler(session, new_session_uri, timeout_ms, handler);
}
