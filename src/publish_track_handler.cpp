// SPDX-FileCopyrightText: Copyright (c) 2024 Cisco Systems
// SPDX-License-Identifier: BSD-2-Clause

#include "quicr/handlers/publish_track_handler.h"

#include "quicr/messages/messages.h"
#include "quicr/messages/parameters.h"
#include "quicr/session.h"

#include <spdlog/spdlog.h>

namespace quicr {
    PublishTrackHandler::PublishTrackHandler(const FullTrackName& full_track_name,
                                             TrackMode track_mode,
                                             uint8_t default_priority,
                                             uint32_t default_ttl,
                                             std::optional<messages::StreamHeaderProperties> stream_mode,
                                             messages::Location largest_location)
      : TrackHandler(full_track_name)
      , default_track_mode_(track_mode)
      , default_priority_(default_priority)
      , default_ttl_(default_ttl)
      , largest_location_(largest_location)
    {
        switch (track_mode) {
            case TrackMode::kDatagram:
                if (stream_mode.has_value()) {
                    throw std::invalid_argument("Datagram track mode should not specify a stream mode");
                }
                break;
            case TrackMode::kStream:
                if (stream_mode.has_value()) {
                    stream_mode_.emplace(*stream_mode);
                } else {
                    stream_mode_.emplace(true, messages::SubgroupIdType::kExplicit, false, false, true);
                }
                break;
        }
    }

    void PublishTrackHandler::StatusChanged(Status) {}

    void PublishTrackHandler::MetricsSampled(const PublishTrackMetrics&) {}

    PublishTrackHandler::PublishObjectStatus PublishTrackHandler::ForwardPublishedData(
      bool is_new_stream,
      uint64_t group_id,
      uint64_t subgroup_id,
      std::shared_ptr<const std::vector<uint8_t>> data)
    {
        auto transport = GetSession().lock();
        uint64_t stream_id{ 0 };

        if (!transport) {
            return PublishObjectStatus::kInternalError;
        }

        const auto status = GetStatus();
        switch (status) {
            case Status::kOk:
                break;

            case Status::kPaused:
                publish_track_metrics_.objects_dropped_not_ok++;
                return PublishObjectStatus::kPaused;

            case Status::kUnsubscribed:
                [[fallthrough]];
            case Status::kDoneByFin:
                [[fallthrough]];
            case Status::kNoSubscribers:
                publish_track_metrics_.objects_dropped_not_ok++;
                return PublishObjectStatus::kNoSubscribers;

            case Status::kPendingAnnounceResponse:
                [[fallthrough]];
            case Status::kNotAnnounced:
                [[fallthrough]];
            case Status::kNotConnected:
                publish_track_metrics_.objects_dropped_not_ok++;
                return PublishObjectStatus::kNotAnnounced;
            case Status::kAnnounceNotAuthorized:
                publish_track_metrics_.objects_dropped_not_ok++;
                return PublishObjectStatus::kNotAuthorized;
            case Status::kNewGroupRequested:
                [[fallthrough]];
            case Status::kSubscriptionUpdated: {
                // reset the status to ok to imply change
                if (!is_new_stream) {
                    break;
                }
                auto current = status;
                publish_status_.compare_exchange_strong(
                  current, Status::kOk, std::memory_order_acq_rel, std::memory_order_acquire);
                break;
            }
            case Status::kPendingPublishOk:
                publish_track_metrics_.objects_dropped_not_ok++;
                return PublishObjectStatus::kPendingPublishOk;
            default:
                publish_track_metrics_.objects_dropped_not_ok++;
                return PublishObjectStatus::kInternalError;
        }

        publish_track_metrics_.bytes_published += data->size();

        if (!GetRequestId().has_value()) {
            return PublishTrackHandler::PublishObjectStatus::kNoSubscribers;
        }

        Transport::EnqueueFlags eflags;

        if (group_id > largest_location_.group) {
            largest_location_.group = group_id;
            largest_location_.object = 0;
        }

        switch (default_track_mode_) {
            case TrackMode::kDatagram: {
                eflags.use_reliable = false;
                break;
            }
            default: {
                eflags.use_reliable = true;

                if (is_new_stream) {
                    // Disable creating stream and doing pipeline on new stream considering delta and subgroups needs
                    // first object parsed
#if 0
                    auto stream_id =
                      transport->CreateStream(publish_data_ctx_id_, GetDefaultPriority());
                    stream_info_by_group_[group_id][subgroup_id] = { stream_id, group_id, subgroup_id };
#endif

                    // Do not pipeline on new stream till PublishObject() is called once
                    return PublishObjectStatus::kObjectDataIncomplete;
                }

                auto group_it = stream_info_by_group_.find(group_id);
                if (group_it == stream_info_by_group_.end()) {
                    return PublishTrackHandler::PublishObjectStatus::kInternalError;
                }
                auto subgroup_it = group_it->second.find(subgroup_id);
                if (subgroup_it == group_it->second.end()) {
                    return PublishTrackHandler::PublishObjectStatus::kInternalError;
                }

                stream_id = subgroup_it->second.stream_id;
                break;
            }
        }

        auto result =
          transport->Enqueue(publish_data_ctx_id_, stream_id, data, default_priority_, default_ttl_, 0, eflags);

        if (result != TransportError::kNone) {
            throw TransportException(result);
        }

        return PublishTrackHandler::PublishObjectStatus::kOk;
    }

    PublishTrackHandler::PublishObjectStatus PublishTrackHandler::PublishObject(
      const ObjectHeaders& object_headers,
      BytesSpan data,
      std::optional<messages::StreamHeaderProperties> stream_mode)
    {
        auto transport = GetSession().lock();

        if (!transport) {
            return PublishObjectStatus::kInternalError;
        }

        const auto status = GetStatus();
        switch (status) {
            case Status::kOk:
                break;

            case Status::kPaused:
                publish_track_metrics_.objects_dropped_not_ok++;
                return PublishObjectStatus::kPaused;

            case Status::kUnsubscribed:
                [[fallthrough]];
            case Status::kDoneByFin:
                [[fallthrough]];
            case Status::kNoSubscribers:
                publish_track_metrics_.objects_dropped_not_ok++;
                return PublishObjectStatus::kNoSubscribers;

            case Status::kPendingAnnounceResponse:
                [[fallthrough]];
            case Status::kNotAnnounced:
                [[fallthrough]];
            case Status::kNotConnected:
                publish_track_metrics_.objects_dropped_not_ok++;
                return PublishObjectStatus::kNotAnnounced;
            case Status::kAnnounceNotAuthorized:
                publish_track_metrics_.objects_dropped_not_ok++;
                return PublishObjectStatus::kNotAuthorized;
            case Status::kNewGroupRequested: {
                // reset the status to ok to imply change
                auto current = status;
                publish_status_.compare_exchange_strong(
                  current, Status::kOk, std::memory_order_acq_rel, std::memory_order_acquire);
                break;
            }
            case Status::kSubscriptionUpdated: {

                /*
                 * TODO: Need to revisit the below since subgroups doesn't really support this
                 * Always start a new stream on subscription update to support peering/pipelining
                 */

                auto current = status;
                publish_status_.compare_exchange_strong(
                  current, Status::kOk, std::memory_order_acq_rel, std::memory_order_acquire);
                break;
            }
            default:
                publish_track_metrics_.objects_dropped_not_ok++;
                return PublishObjectStatus::kInternalError;
        }

        if (!GetRequestId().has_value()) {
            return PublishTrackHandler::PublishObjectStatus::kNoSubscribers;
        }

        std::uint16_t ttl = object_headers.ttl.value_or(default_ttl_);
        std::uint8_t priority = object_headers.priority.value_or(default_priority_);

        if (object_headers.group_id > largest_location_.group) {
            largest_location_.group = object_headers.group_id;
            largest_location_.object = object_headers.object_id;

        } else if (largest_location_.group == object_headers.group_id) {
            largest_location_.object = object_headers.object_id;
        }

        std::map<std::uint64_t, StreamInfo> subgroup_it;
        bool is_stream_header_needed{ false };
        uint64_t group_id_delta{ 0 };
        uint64_t object_id_delta{ 0 };
        uint64_t stream_id{ 0 };

        if (default_track_mode_ == TrackMode::kStream) {
            // If this is the first time this group/subgroup has been seen, then a new stream is required
            auto group_it = stream_info_by_group_.find(object_headers.group_id);
            decltype(group_it->second.begin()) subgroup_it;

            if (group_it == stream_info_by_group_.end()) {
                auto [it, _] = stream_info_by_group_.try_emplace(object_headers.group_id);
                group_it = std::move(it);
            }

            subgroup_it = group_it->second.find(object_headers.subgroup_id);
            if (subgroup_it == group_it->second.end()) {
                is_stream_header_needed = true;
                stream_id = transport->CreateStream(publish_data_ctx_id_, priority);

                auto& subgroup_map = stream_info_by_group_[object_headers.group_id];
                auto [it, _] =
                  subgroup_map.emplace(object_headers.subgroup_id,
                                       StreamInfo{ stream_id, object_headers.group_id, object_headers.subgroup_id });
                subgroup_it = std::move(it);
            }

            if (subgroup_it->second.last_object_id.has_value()) {
                group_id_delta = subgroup_it->second.last_group_id > object_headers.group_id
                                   ? 0
                                   : object_headers.group_id - subgroup_it->second.last_group_id;

                object_id_delta = subgroup_it->second.last_object_id > object_headers.object_id
                                    ? object_headers.object_id
                                    : object_headers.object_id - *subgroup_it->second.last_object_id;
            } else {
                object_id_delta = object_headers.object_id + 1;
            }

            if (object_id_delta)
                object_id_delta--; // Adjust for delta in missing objects

            if (group_id_delta) {
                // Group change, reset pending new group request
                pending_new_group_request_id_ = std::nullopt;
            }

            subgroup_it->second.last_group_id = object_headers.group_id;
            subgroup_it->second.last_subgroup_id = object_headers.subgroup_id;
            subgroup_it->second.last_object_id = object_headers.object_id;

            stream_id = subgroup_it->second.stream_id;
        }

        if (object_headers.track_mode.has_value() && object_headers.track_mode != default_track_mode_) {
            SetDefaultTrackMode(*object_headers.track_mode);
        }

        publish_track_metrics_.bytes_published += data.size();
        publish_track_metrics_.objects_published++;

        Transport::EnqueueFlags eflags;

        object_msg_buffer_.clear();

        switch (default_track_mode_) {
            case TrackMode::kDatagram: {
                messages::ObjectDatagram object;
                object.group_id = object_headers.group_id;
                object.object_id = object_headers.object_id;
                object.priority = priority;
                object.track_alias = GetTrackAlias().value();
                object.extensions = object_headers.extensions;
                object.immutable_extensions = object_headers.immutable_extensions;
                object.payload.assign(data.begin(), data.end());
                object_msg_buffer_ << object;
                break;
            }
            default: {
                // use stream per subgroup, group change
                eflags.use_reliable = true;

                const auto properties = stream_mode.value_or(*GetStreamMode());
                if (is_stream_header_needed) {
                    messages::StreamHeaderSubGroup subgroup_hdr;
                    subgroup_hdr.properties.emplace(properties);
                    subgroup_hdr.group_id = object_headers.group_id;
                    if (properties.subgroup_id_mode == messages::SubgroupIdType::kExplicit) {
                        subgroup_hdr.subgroup_id = object_headers.subgroup_id;
                    }
                    if (!properties.default_priority) {
                        subgroup_hdr.priority = priority;
                    }
                    subgroup_hdr.track_alias = GetTrackAlias().value();
                    object_msg_buffer_ << subgroup_hdr;
                }

                messages::StreamSubGroupObject object;
                object.object_delta = object_id_delta;
                object.object_status = object_headers.status;
                object.properties.emplace(properties);
                object.extensions = object_headers.extensions;
                object.immutable_extensions = object_headers.immutable_extensions;
                object.payload.assign(data.begin(), data.end());
                object_msg_buffer_ << object;
                break;
            }
        }

        SPDLOG_TRACE("Published conn_id: {} object stream_id: {} group: {} subgroup: {} object: {}",

                     subgroup_it->second.stream_id,
                     object_headers.group_id,
                     object_headers.subgroup_id,
                     object_headers.object_id);
        auto result = transport->Enqueue(

          publish_data_ctx_id_,
          stream_id,
          std::make_shared<std::vector<uint8_t>>(object_msg_buffer_.begin(), object_msg_buffer_.end()),
          priority,
          ttl,
          0,
          eflags);

        if (result != TransportError::kNone) {
            throw TransportException(result);
        }

        return PublishTrackHandler::PublishObjectStatus::kOk;
    }

    void PublishTrackHandler::EndSubgroup(uint64_t group_id, uint64_t subgroup_id, bool completed)
    {
        auto transport = GetSession().lock();

        if (!transport) {
            return;
        }

        auto group_it = stream_info_by_group_.find(group_id);
        if (group_it == stream_info_by_group_.end()) {
            return;
        }

        auto subgroup_it = group_it->second.find(subgroup_id);
        if (subgroup_it == group_it->second.end()) {
            return;
        }

        object_msg_buffer_.clear();

        Transport::EnqueueFlags eflags;
        eflags.use_reliable = true;
        eflags.close_stream = true;
        eflags.use_reset = !completed;

        transport->Enqueue(
          publish_data_ctx_id_, subgroup_it->second.stream_id, {}, default_priority_, default_ttl_, 0, eflags);

        group_it->second.erase(subgroup_it);
        if (group_it->second.empty()) {
            stream_info_by_group_.erase(group_it);
        }
    }

    void PublishTrackHandler::RequestOkReceived(const messages::Parameters& params)
    {
        if (GetStatus() == Status::kPendingPublishOk) {
            // PUBLISH_OK.
            messages::ValidateParameters(params,
                                         { messages::ParameterType::kDeliveryTimeout,
                                           messages::ParameterType::kSubgroupDeliveryTimeout,
                                           messages::ParameterType::kSubscriberPriority,
                                           messages::ParameterType::kGroupOrder,
                                           messages::ParameterType::kSubscriptionFilter,
                                           messages::ParameterType::kNewGroupRequest,
                                           messages::ParameterType::kExpires,
                                           messages::ParameterType::kForward });
            // TODO: OBJECT_DELIVERY_TIMEOUT
            // TODO: SUBGROUP_DELIVERY_TIMEOUT
            // TODO: SUBSCRIBER_PRIORITY
            // TODO: GROUP_ORDER
            // TODO: SUBSCRIPTION_FILTER
            // TODO: EXPIRES

            const auto ngr = params.GetOptional<std::uint64_t>(messages::ParameterType::kNewGroupRequest);
            if (ngr.has_value()) {
                if (!support_new_group_request_) {
                    throw messages::ProtocolViolationException("Must not request new group on non-dynamic track");
                }
                // TODO: Use the NEW_GROUP_REQUEST value to do something.
                SetStatus(Status::kNewGroupRequested);
            }

            const auto forward = messages::ResolveForward(params, true);
            SetStatus(forward ? Status::kOk : Status::kPaused);
        } else {
            // REQUEST_UPDATE_OK.
            // TODO: In theory largest_object here?
            // TODO: Handle unsolicited REQUEST_UPDATE_OK?
            messages::ValidateParameters(params, { messages::ParameterType::kExpires });
            // TODO: EXPIRES.
        }
    }

    void PublishTrackHandler::RequestUpdateReceived(const messages::Parameters& params)
    {
        // The subscriber can update their subscription with lots of details.
        // TODO: AUTHORIZATION_TOKEN
        // TODO: OBJECT_DELIVERY_TIMEOUT
        // TODO: SUBGROUP_DELIVERY_TIMEOUT
        // TODO: SUBSCRIBER_PRIORITY
        // TODO: SUBSCRIPTION_FILTER
        if (auto forward = params.GetOptional<bool>(messages::ParameterType::kForward); forward) {
            SetStatus(*forward ? Status::kOk : Status::kPaused);
        }

        if (auto ngr = params.GetOptional<std::uint64_t>(messages::ParameterType::kNewGroupRequest); ngr) {
            SetStatus(Status::kNewGroupRequested);
        }

        if (const auto transport = GetSession().lock()) {
            transport->ResolveRequestUpdate(*GetRequestId(), { .error = std::nullopt, .params = {} });
        }
    }

    void PublishTrackHandler::StreamClosed(std::uint64_t stream_id, [[maybe_unused]] bool reset)
    {
        for (auto group_it = stream_info_by_group_.begin(); group_it != stream_info_by_group_.end();) {
            for (auto subgroup_it = group_it->second.begin(); subgroup_it != group_it->second.end();) {
                if (subgroup_it->second.stream_id == stream_id) {
                    subgroup_it = group_it->second.erase(subgroup_it);

                    if (group_it->second.empty()) {
                        group_it = stream_info_by_group_.erase(group_it);
                    }

                    return;
                }

                ++subgroup_it;
            }
            ++group_it;
        }
    }
} // namespace quicr
