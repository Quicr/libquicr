// SPDX-FileCopyrightText: Copyright (c) 2025 Cisco Systems
// SPDX-License-Identifier: BSD-2-Clause

#include "quicr/handlers/joining_fetch_handler.h"

#include <spdlog/spdlog.h>

namespace quicr {
    void JoiningFetchHandler::TryParseStreamBufferData(StreamContext& stream)
    {
        auto& f_hdr = stream.buffer.GetAny<messages::FetchHeader>();
        if (not(stream.buffer >> f_hdr)) {
            return;
        }

        while (not stream.buffer.Empty()) {
            if (not stream.buffer.AnyHasValueB()) {
                stream.buffer.InitAnyB<messages::FetchObject>();
            }

            auto& obj = stream.buffer.GetAnyB<messages::FetchObject>();
            if (not(stream.buffer >> obj)) {
                return;
            }

            SPDLOG_TRACE("Received fetch_object subscribe_id: {} priority: {} "
                         "group_id: {} subgroup_id: {} object_id: {} data size: {}",
                         *GetSubscribeId(),
                         obj.publisher_priority,
                         obj.group_id,
                         obj.subgroup_id,
                         obj.object_id,
                         obj.payload.size());
            try {
                joining_subscribe_->ObjectReceived({ obj.group_id,
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

            stream.buffer.ResetAnyB();
        }
    }
}
