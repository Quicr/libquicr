// SPDX-FileCopyrightText: Copyright (c) 2024 Cisco Systems
// SPDX-License-Identifier: BSD-2-Clause

#include <quicr/publish_track_handler.h>

#include "quicr/detail/transport.h"

namespace quicr {
    void PublishTrackHandler::StatusChanged(Status) {}
    void PublishTrackHandler::MetricsSampled(const PublishTrackMetrics&) {}

    PublishTrackHandler::PublishObjectStatus PublishTrackHandler::ForwardPublishedData(
      bool is_new_stream,
      uint64_t group_id,
      uint64_t subgroup_id,
      std::shared_ptr<const std::vector<uint8_t>> data)
    {
        auto transport = GetTransport().lock();

        if (!transport) {
            return PublishObjectStatus::kInternalError;
        }

        switch (publish_status_) {
            case Status::kOk:
                break;

            case Status::kPaused:
                publish_track_metrics_.objects_dropped_not_ok++;
                return PublishObjectStatus::kPaused;

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
            case Status::kSubscriptionUpdated:
                // reset the status to ok to imply change
                if (!is_new_stream) {
                    break;
                }
                publish_status_ = Status::kOk;
                break;
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

        ITransport::EnqueueFlags eflags;

        if (group_id > largest_location_.group) {
            largest_location_.group = group_id;
            largest_location_.object = 0;
        }

        auto group_subgroup_hash = group_id;
        hash_combine(group_subgroup_hash, subgroup_id);

        switch (default_track_mode_) {
            case TrackMode::kDatagram: {
                eflags.use_reliable = false;
                break;
            }
            default: {
                eflags.use_reliable = true;

                if (is_new_stream) {
                    eflags.close_stream = true;
                    eflags.clear_tx_queue = true;
                    eflags.use_reset = false;

                    auto stream_id =
                      transport->CreateStream(GetConnectionId(), publish_data_ctx_id_, GetDefaultPriority());
                    stream_info_by_group_[group_subgroup_hash] = { stream_id, group_id, subgroup_id, 0 };
                }

                break;
            }
        }

        auto stream_it = stream_info_by_group_.find(group_subgroup_hash);
        if (stream_it == stream_info_by_group_.end()) {
            return PublishTrackHandler::PublishObjectStatus::kInternalError;
        }

        auto result = transport->Enqueue(GetConnectionId(),
                                         publish_data_ctx_id_,
                                         stream_it->second.stream_id,
                                         data,
                                         default_priority_,
                                         default_ttl_,
                                         0,
                                         eflags);

        if (result != TransportError::kNone) {
            throw TransportException(result);
        }

        return PublishTrackHandler::PublishObjectStatus::kOk;
    }

    PublishTrackHandler::PublishObjectStatus PublishTrackHandler::PublishObject(const ObjectHeaders& object_headers,
                                                                                BytesSpan data)
    {
        auto transport = GetTransport().lock();

        if (!transport) {
            return PublishObjectStatus::kInternalError;
        }

        std::uint16_t ttl = object_headers.ttl.has_value() ? object_headers.ttl.value() : default_ttl_;
        std::uint8_t priority =
          object_headers.priority.has_value() ? object_headers.priority.value() : default_priority_;

        auto group_subgroup_hash = object_headers.group_id;
        hash_combine(group_subgroup_hash, object_headers.subgroup_id);

        if (object_headers.group_id > largest_location_.group) {
            largest_location_.group = object_headers.group_id;
            largest_location_.object = object_headers.object_id;

        } else if (largest_location_.group == object_headers.group_id) {
            largest_location_.object = object_headers.object_id;
        }

        bool is_stream_header_needed{ false };

        // If this is the first time this group has been seen, then a new stream is required
        auto stream_it = stream_info_by_group_.find(group_subgroup_hash);
        if (stream_it == stream_info_by_group_.end()) {
            is_stream_header_needed = true;

            auto stream_id = transport->CreateStream(GetConnectionId(), publish_data_ctx_id_, priority);
            auto [it, _] = stream_info_by_group_.emplace(
              group_subgroup_hash,
              StreamInfo{ stream_id, object_headers.group_id, object_headers.subgroup_id, object_headers.object_id });
            stream_it = it;
        }

        switch (publish_status_) {
            case Status::kOk:
                break;

            case Status::kPaused:
                publish_track_metrics_.objects_dropped_not_ok++;
                return PublishObjectStatus::kPaused;

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
                // reset the status to ok to imply change
                if (!is_stream_header_needed) {
                    break;
                }
                publish_status_ = Status::kOk;
                break;
            case Status::kSubscriptionUpdated:

                /*
                 * Always start a new stream on subscription update to support peering/pipelining
                 */
                is_stream_header_needed = true;

                publish_status_ = Status::kOk;
                break;
            default:
                publish_track_metrics_.objects_dropped_not_ok++;
                return PublishObjectStatus::kInternalError;
        }

        if (object_headers.track_mode.has_value() && object_headers.track_mode != default_track_mode_) {
            SetDefaultTrackMode(*object_headers.track_mode);
        }

        uint64_t group_id_delta{ 0 };
        uint64_t object_id_delta{ 0 };

        if (stream_it->second.last_object_id.has_value()) {
            group_id_delta = stream_it->second.last_group_id > object_headers.group_id
                               ? 0
                               : object_headers.group_id - stream_it->second.last_group_id;

            object_id_delta = stream_it->second.last_group_id > object_headers.object_id
                                ? object_headers.object_id
                                : object_headers.object_id - *stream_it->second.last_object_id;
        } else {
            object_id_delta = object_headers.object_id + 1;
        }

        if (object_id_delta)
            object_id_delta--; // Adjust for delta in missing objects

        auto object_extensions = object_headers.extensions;

        if (group_id_delta) {
            // Group change, reset pending new group request
            pending_new_group_request_id_ = std::nullopt;
        }

        // Only client (publishers) can add these extensions. Per moqt, relays do not add these extensions
        if (transport->client_mode_) {
            if (group_id_delta > 1) {
                const std::uint64_t value = group_id_delta - 1;
                std::vector<std::uint8_t> value_bytes(sizeof(value));
                memcpy(value_bytes.data(), &value, sizeof(value));
                if (not object_extensions.has_value()) {
                    object_extensions = Extensions{};
                }

                (*object_extensions)[static_cast<uint64_t>(messages::ExtensionHeaderType::kPriorGroupIdGap)].push_back(
                  value_bytes);
            }

            if (object_id_delta > 0) {
                const std::uint64_t value = object_id_delta;
                std::vector<std::uint8_t> value_bytes(sizeof(value));
                memcpy(value_bytes.data(), &value, sizeof(value));
                if (not object_extensions.has_value()) {
                    object_extensions = Extensions{};
                }

                (*object_extensions)[static_cast<uint64_t>(messages::ExtensionHeaderType::kPriorObjectIdGap)].push_back(
                  value_bytes);
            }
        }

        stream_it->second.last_group_id = object_headers.group_id;
        stream_it->second.last_subgroup_id = object_headers.subgroup_id;
        stream_it->second.last_object_id = object_headers.object_id;
        publish_track_metrics_.bytes_published += data.size();
        publish_track_metrics_.objects_published++;

        if (!GetRequestId().has_value()) {
            return PublishTrackHandler::PublishObjectStatus::kNoSubscribers;
        }

        ITransport::EnqueueFlags eflags;

        object_msg_buffer_.clear();

        switch (default_track_mode_) {
            case TrackMode::kDatagram: {
                messages::ObjectDatagram object;
                object.group_id = object_headers.group_id;
                object.object_id = object_headers.object_id;
                object.priority = priority;
                object.track_alias = GetTrackAlias().value();
                if (object_extensions) {
                    object.extensions = std::move(*object_extensions);
                }
                if (object_headers.immutable_extensions) {
                    object.immutable_extensions = std::move(object_headers.immutable_extensions);
                }
                object.payload.assign(data.begin(), data.end());
                object_msg_buffer_ << object;
                break;
            }
            default: {
                // use stream per subgroup, group change
                eflags.use_reliable = true;

                if (is_stream_header_needed) {
                    eflags.close_stream = true;
                    eflags.clear_tx_queue = true;
                    eflags.use_reset = false;

                    messages::StreamHeaderSubGroup subgroup_hdr;
                    subgroup_hdr.type = GetStreamMode();
                    subgroup_hdr.group_id = object_headers.group_id;
                    auto properties = messages::StreamHeaderProperties(subgroup_hdr.type);
                    if (properties.subgroup_id_type == messages::SubgroupIdType::kExplicit) {
                        subgroup_hdr.subgroup_id = object_headers.subgroup_id;
                    }
                    subgroup_hdr.priority = priority;
                    subgroup_hdr.track_alias = GetTrackAlias().value();
                    object_msg_buffer_ << subgroup_hdr;
                }

                messages::StreamSubGroupObject object;
                object.object_delta = object_id_delta;
                object.stream_type = GetStreamMode();
                if (object_extensions) {
                    object.extensions = std::move(*object_extensions);
                }
                if (object_headers.immutable_extensions) {
                    object.immutable_extensions = std::move(*object_headers.immutable_extensions);
                }
                object.payload.assign(data.begin(), data.end());
                object_msg_buffer_ << object;
                break;
            }
        }

        auto result = transport->Enqueue(
          GetConnectionId(),
          publish_data_ctx_id_,
          stream_it->second.stream_id,
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

} // namespace quicr
