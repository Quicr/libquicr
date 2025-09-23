// SPDX-FileCopyrightText: Copyright (c) 2025 Cisco Systems
// SPDX-License-Identifier: BSD-2-Clause

#include "quicr/joining_fetch_handler.h"

#include <spdlog/spdlog.h>

#include <memory>

namespace quicr {
    JoiningFetchHandler::JoiningFetchHandler(std::shared_ptr<SubscribeTrackHandler> joining_subscribe_handler)
      : SubscribeTrackHandler(joining_subscribe_handler->GetFullTrackName(),
                              joining_subscribe_handler->GetPriority(),
                              joining_subscribe_handler->GetGroupOrder(),
                              joining_subscribe_handler->GetFilterType())
      , joining_subscribe_handler_(std::move(joining_subscribe_handler))
    {
    }

    void JoiningFetchHandler::StreamDataRecv(bool is_start,
                                             [[maybe_unused]] uint64_t stream_id,
                                             std::shared_ptr<const std::vector<uint8_t>> data)
    {
        if (is_start) {
            stream_buffer_.Clear();

            stream_buffer_.InitAny<messages::FetchHeader>();
            stream_buffer_.Push(*data);

            // Expect that on initial start of stream, there is enough data to process the stream headers

            auto& f_hdr = stream_buffer_.GetAny<messages::FetchHeader>();
            if (not(stream_buffer_ >> f_hdr)) {
                SPDLOG_ERROR("Not enough data to process new stream headers, stream is invalid");
                // TODO: Add metrics to track this
                return;
            }
        } else {
            stream_buffer_.Push(*data);
        }

        stream_buffer_.InitAnyB<messages::FetchObject>();
        auto& obj = stream_buffer_.GetAnyB<messages::FetchObject>();

        if (stream_buffer_ >> obj) {
            SPDLOG_TRACE("Received fetch_object subscribe_id: {} priority: {} "
                         "group_id: {} subgroup_id: {} object_id: {} data size: {}",
                         *GetSubscribeId(),
                         obj.publisher_priority,
                         obj.group_id,
                         obj.subgroup_id,
                         obj.object_id,
                         obj.payload.size());
            try {
                joining_subscribe_handler_->ObjectReceived({ obj.group_id,
                                                             obj.object_id,
                                                             obj.subgroup_id,
                                                             obj.payload.size(),
                                                             obj.object_status,
                                                             obj.publisher_priority,
                                                             std::nullopt,
                                                             TrackMode::kStream,
                                                             obj.extensions,
                                                             obj.immutable_extensions },
                                                           obj.payload);
            } catch (const std::exception& e) {
                SPDLOG_ERROR("Caught exception trying to receive Joining Fetch object. (error={})", e.what());
            }

            stream_buffer_.ResetAnyB<messages::FetchObject>();
        }
    }
}
