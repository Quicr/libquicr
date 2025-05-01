// SPDX-FileCopyrightText: Copyright (c) 2024 Cisco Systems
// SPDX-License-Identifier: BSD-2-Clause

#include "quicr/detail/messages.h"
#include "quicr/detail/stream_buffer.h"
#include <quicr/subscribe_track_handler.h>

namespace quicr {

    void SubscribeTrackHandler::ObjectReceived([[maybe_unused]] const ObjectHeaders& object_headers,
                                               [[maybe_unused]] BytesSpan data)
    {
    }

    void SubscribeTrackHandler::StreamDataRecv(bool is_start,
                                               uint64_t stream_id,
                                               std::shared_ptr<const std::vector<uint8_t>> data)
    {
        if (stream_id > current_stream_id_) {
            current_stream_id_ = stream_id;
        } else if (stream_id < current_stream_id_) {
            SPDLOG_DEBUG(
              "Old stream data received, stream_id: {} is less than {}, ignoring", stream_id, current_stream_id_);
            return;
        }

        if (is_start) {
            stream_buffer_.Clear();

            stream_buffer_.InitAny<messages::StreamHeaderSubGroup>();
            stream_buffer_.Push(*data);

            // Expect that on initial start of stream, there is enough data to process the stream headers

            auto& s_hdr = stream_buffer_.GetAny<messages::StreamHeaderSubGroup>();
            if (not(stream_buffer_ >> s_hdr)) {
                SPDLOG_ERROR("Not enough data to process new stream headers, stream is invalid");
                // TODO: Add metrics to track this
                return;
            }
        } else {
            stream_buffer_.Push(*data);
        }

        auto& s_hdr = stream_buffer_.GetAny<messages::StreamHeaderSubGroup>();

        if (not stream_buffer_.AnyHasValueB()) {
            stream_buffer_.InitAnyB<messages::StreamSubGroupObject>();
        }

        auto& obj = stream_buffer_.GetAnyB<messages::StreamSubGroupObject>();
        obj.serialize_extensions = TypeWillSerializeExtensions(s_hdr.type);
        if (stream_buffer_ >> obj) {
            SPDLOG_TRACE("Received stream_subgroup_object type: {} priority: {} track_alias: {} "
                         "group_id: {} subgroup_id: {} object_id: {} data size: {}",
                         static_cast<std::uint8_t>(s_hdr.subgroup_type),
                         s_hdr.priority,
                         s_hdr.track_alias,
                         s_hdr.group_id,
                         s_hdr.subgroup_id.has_value() ? *s_hdr.subgroup_id : -1,
                         obj.object_id,
                         obj.payload.size());

            if (!s_hdr.subgroup_id.has_value()) {
                // TODO(RichLogan): This is a protocol error?
                assert(s_hdr.type == messages::StreamHeaderType::kSubgroupFirstObjectNoExtensions ||
                       s_hdr.type == messages::StreamHeaderType::kSubgroupFirstObjectWithExtensions);
                s_hdr.subgroup_id = obj.object_id;
            }

            subscribe_track_metrics_.objects_received++;
            subscribe_track_metrics_.bytes_received += obj.payload.size();

            ObjectReceived({ s_hdr.group_id,
                             obj.object_id,
                             s_hdr.subgroup_id.value(),
                             obj.payload.size(),
                             obj.object_status,
                             s_hdr.priority,
                             std::nullopt,
                             TrackMode::kStream,
                             obj.extensions },
                           obj.payload);

            stream_buffer_.ResetAnyB<messages::StreamSubGroupObject>();
        }
    }

    void SubscribeTrackHandler::DgramDataRecv(std::shared_ptr<const std::vector<uint8_t>> data)
    {
        stream_buffer_.Clear();

        stream_buffer_.Push(*data);
        stream_buffer_.Pop(); // Remove type header

        messages::ObjectDatagram msg;
        if (stream_buffer_ >> msg) {
            SPDLOG_TRACE("Received object datagram conn_id: {0} data_ctx_id: {1} subscriber_id: {2} "
                         "track_alias: {3} group_id: {4} object_id: {5} data size: {6}",
                         conn_id,
                         (data_ctx_id ? *data_ctx_id : 0),
                         msg.subscribe_id,
                         msg.track_alias,
                         msg.group_id,
                         msg.object_id,
                         msg.payload.size());

            subscribe_track_metrics_.objects_received++;
            subscribe_track_metrics_.bytes_received += msg.payload.size();
            ObjectReceived(
              {
                msg.group_id,
                msg.object_id,
                0, // datagrams don't have subgroups
                msg.payload.size(),
                ObjectStatus::kAvailable,
                msg.priority,
                std::nullopt,
                TrackMode::kDatagram,
                msg.extensions,
              },
              std::move(msg.payload));
        }
    }

    void SubscribeTrackHandler::RequestNewGroup() noexcept
    {
        if (new_group_request_callback_ && GetSubscribeId().has_value() && GetTrackAlias().has_value()) {
            new_group_request_callback_(GetSubscribeId().value(), GetTrackAlias().value());
        }
    }
} // namespace quicr
