// SPDX-FileCopyrightText: Copyright (c) 2025 Cisco Systems
// SPDX-License-Identifier: BSD-2-Clause

#include "quicr/handlers/fetch_track_handler.h"
#include "quicr/utilities/format.h"

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

            subscribe_track_metrics_.objects_received++;
            subscribe_track_metrics_.bytes_received += resolved->payload.size();

            std::exception_ptr error;
            try {
                ObjectReceived(resolved->headers, resolved->payload);
            } catch (const std::exception& e) {
                error = std::make_exception_ptr(e);
            }

            stream.buffer.ResetAnyB();

            if (error) {
                std::rethrow_exception(error);
            }
        }
    }
}
