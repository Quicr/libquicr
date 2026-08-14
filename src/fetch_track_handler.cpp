// SPDX-FileCopyrightText: Copyright (c) 2025 Cisco Systems
// SPDX-License-Identifier: BSD-2-Clause

#include "quicr/handlers/fetch_track_handler.h"

#include <spdlog/spdlog.h>

namespace quicr {
    void FetchTrackHandler::TryParseStreamBufferData(StreamContext& stream)
    {
        if (not stream.buffer.AnyHasValue()) {
            stream.buffer.InitAny<messages::FetchHeader>();
            serialization_state_ = messages::FetchObjectSerializationState(*GetGroupOrder());
        }

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

            const auto resolved = serialization_state_.Decode(std::move(obj));
            if (!resolved.has_value()) {
                // TODO: We're being told this object doesn't exist, should we notify?
                stream.buffer.ResetAnyB();
                continue;
            }

            SPDLOG_TRACE("Received fetch_object subscribe_id: {} priority: {} "
                         "group_id: {} subgroup_id: {} object_id: {} data size: {}",
                         *GetSubscribeId(),
                         *resolved->headers.priority,
                         resolved->headers.group_id,
                         resolved->headers.subgroup_id,
                         resolved->headers.object_id,
                         resolved->payload.size());

            subscribe_track_metrics_.objects_received++;
            subscribe_track_metrics_.bytes_received += resolved->payload.size();

            try {
                ObjectReceived(resolved->headers, resolved->payload);
            } catch (const std::exception& e) {
                SPDLOG_ERROR("Caught exception trying to receive Fetch object. (error={})", e.what());
            }

            stream.buffer.ResetAnyB();
        }
    }
}
