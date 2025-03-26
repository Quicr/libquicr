// SPDX-FileCopyrightText: Copyright (c) 2025 Cisco Systems
// SPDX-License-Identifier: BSD-2-Clause

#include "quicr/fetch_track_handler.h"

namespace quicr {
    void FetchTrackHandler::StreamDataRecv(bool is_start,
                                           [[maybe_unused]] uint64_t stream_id,
                                           std::shared_ptr<const std::vector<uint8_t>> data)
    {
        if (is_start) {
            stream_buffer_.Clear();

            stream_buffer_.InitAny<data_messages::FetchHeader>();
            stream_buffer_.Push(*data);
            stream_buffer_.Pop(); // Remove type header

            // Expect that on initial start of stream, there is enough data to process the stream headers

            auto& f_hdr = stream_buffer_.GetAny<data_messages::FetchHeader>();
            if (not(stream_buffer_ >> f_hdr)) {
                SPDLOG_ERROR("Not enough data to process new stream headers, stream is invalid");
                // TODO: Add metrics to track this
                return;
            }
        } else {
            stream_buffer_.Push(*data);
        }

        stream_buffer_.InitAnyB<data_messages::FetchObject>();
        auto& obj = stream_buffer_.GetAnyB<data_messages::FetchObject>();

        if (stream_buffer_ >> obj) {
            SPDLOG_TRACE("Received fetch_object subscribe_id: {} priority: {} "
                         "group_id: {} subgroup_id: {} object_id: {} data size: {}",
                         *GetSubscribeId(),
                         obj.publisher_priority,
                         obj.group_id,
                         obj.subgroup_id,
                         obj.object_id,
                         obj.payload.size());

            subscribe_track_metrics_.objects_received++;
            subscribe_track_metrics_.bytes_received += obj.payload.size();

            ObjectReceived({ obj.group_id,
                             obj.object_id,
                             obj.subgroup_id,
                             obj.payload.size(),
                             obj.object_status,
                             obj.publisher_priority,
                             std::nullopt,
                             TrackMode::kStream,
                             obj.extensions },
                           obj.payload);

            stream_buffer_.ResetAnyB<data_messages::FetchObject>();
        }
    }
}
