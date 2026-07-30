// SPDX-FileCopyrightText: Copyright (c) 2025 Cisco Systems
// SPDX-License-Identifier: BSD-2-Clause

#include "quicr/handlers/joining_fetch_handler.h"
#include "quicr/log.h"

namespace quicr {
    void JoiningFetchHandler::TryParseStreamBufferData(StreamContext& stream)
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
                stream.buffer.ResetAnyB();
                continue;
            }

            QUICR_TRACE("Received fetch_object subscribe_id: {} priority: {} "
                        "group_id: {} subgroup_id: {} object_id: {} data size: {}",
                        *GetSubscribeId(),
                        *resolved->headers.priority,
                        resolved->headers.group_id,
                        resolved->headers.subgroup_id,
                        resolved->headers.object_id,
                        resolved->payload.size());
            try {
                joining_subscribe_->ObjectReceived(resolved->headers, resolved->payload);
            } catch (const std::exception& e) {
                QUICR_ERROR("Caught exception trying to receive Joining Fetch object. (error={})", e.what());
            }

            stream.buffer.ResetAnyB();
        }
    }
}
