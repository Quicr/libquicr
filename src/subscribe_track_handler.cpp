// SPDX-FileCopyrightText: Copyright (c) 2024 Cisco Systems
// SPDX-License-Identifier: BSD-2-Clause

#include "quicr/detail/messages.h"
#include "quicr/detail/stream_buffer.h"
#include <quicr/subscribe_track_handler.h>

namespace quicr {

    void SubscribeTrackHandler::ObjectReceived(const ObjectHeaders& object_headers, BytesSpan data) {}

    void SubscribeTrackHandler::StreamDataRecv(bool is_start, std::shared_ptr<const std::vector<uint8_t>> data)
    {
        if (is_start) { // New stream
            stream_buffer_.Clear();

            // Parse stream header
        }

    }

    void SubscribeTrackHandler::DgramDataRecv(std::shared_ptr<const std::vector<uint8_t>> data)
    {
        stream_buffer_.Clear();

        stream_buffer_.Push(*data);
        stream_buffer_.Pop();   // Remove type header

        messages::MoqObjectDatagram msg;
        if (stream_buffer_ >> msg) {
            SPDLOG_LOGGER_TRACE(logger_,
                                "Received object datagram conn_id: {0} data_ctx_id: {1} subscriber_id: {2} "
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

} // namespace quicr
