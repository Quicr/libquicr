// SPDX-FileCopyrightText: Copyright (c) 2025 Cisco Systems
// SPDX-License-Identifier: BSD-2-Clause

#include "quicr/detail/joining_fetch_handler.h"

#include "quicr/fetch_track_handler.h"

namespace quicr {
    void JoiningFetchHandler::StreamDataRecv(bool is_start,
                                             std::uint64_t stream_id,
                                             std::shared_ptr<const std::vector<uint8_t>> data)
    {
        auto& stream = streams_[stream_id];

        if (is_start) {
            stream.buffer.Clear();

            stream.buffer.InitAny<messages::FetchHeader>();
            stream.buffer.Push(*data);

            // Expect that on initial start of stream, there is enough data to process the stream headers

            auto& f_hdr = stream.buffer.GetAny<messages::FetchHeader>();
            if (not(stream.buffer >> f_hdr)) {
                SPDLOG_ERROR("Not enough data to process new stream headers, stream is invalid");
                // TODO: Add metrics to track this
                return;
            }
        } else {
            stream.buffer.Push(*data);
        }

        stream.buffer.InitAnyB<messages::FetchObject>();
        auto& obj = stream.buffer.GetAnyB<messages::FetchObject>();

        if (stream.buffer >> obj) {
            // Build.
            ObjectHeaders headers;
            try {
                headers = FetchTrackHandler::From(obj, state_);
                state_.Update(headers);
            } catch (const std::invalid_argument& e) {
                throw messages::ProtocolViolationException(e.what());
            }

            // Deliver.
            SPDLOG_TRACE("Received fetch_object subscribe_id: {} priority: {} "
                         "group_id: {} subgroup_id: {} object_id: {} data size: {}",
                         *GetSubscribeId(),
                         headers.publisher_priority,
                         headers.group_id,
                         headers.subgroup_id,
                         headers.object_id,
                         headers.payload.size());
            try {
                joining_subscribe_->ObjectReceived(headers, obj.payload);
            } catch (const std::exception& e) {
                SPDLOG_ERROR("Caught exception trying to receive Joining Fetch object. (error={})", e.what());
            }

            stream.buffer.ResetAnyB<messages::FetchObject>();
        }
    }
}
