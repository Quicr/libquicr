// SPDX-FileCopyrightText: Copyright (c) 2025 Cisco Systems
// SPDX-License-Identifier: BSD-2-Clause

#include "quicr/handlers/joining_fetch_handler.h"
#include "quicr/utilities/format.h"

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

            std::exception_ptr error;
            try {
                joining_subscribe_->ObjectReceived(resolved->headers, resolved->payload);
            } catch (...) {
                error = std::current_exception();
            }

            stream.buffer.ResetAnyB();

            if (error) {
                std::rethrow_exception(error);
            }
        }
    }
}
