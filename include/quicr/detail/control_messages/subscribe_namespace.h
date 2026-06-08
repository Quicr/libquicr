// SPDX-FileCopyrightText: Copyright (c) 2026 Cisco Systems
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

#include "quicr/detail/control_messages/message_reader.h"
#include "quicr/detail/control_messages/parameters.h"

namespace quicr::messages::control {

    struct SubscribeNamespace
    {
        static constexpr std::uint64_t kType = 0x50;

        const RequestID request_id;
        const TrackNamespace track_namespace_prefix;
        const std::vector<Token> auth_tokens;

        static SubscribeNamespace Decode(BytesSpan payload)
        {
            MessageReader reader{ payload };
            auto request_id = reader.Read<RequestID>();
            auto track_namespace_prefix = reader.Read<TrackNamespace>();

            const auto params = reader.Read<Parameters>();
            reader.ExpectDone();

            ValidateParameters(params, { ParameterType::kAuthorizationToken });

            return SubscribeNamespace{
                .request_id = request_id,
                .track_namespace_prefix = std::move(track_namespace_prefix),
                .auth_tokens = CollectAuthTokens(params),
            };
        }

        [[nodiscard]] Bytes Encode() const
        {
            auto params = Parameters{};
            for (const auto& token : auth_tokens) {
                params.Add(ParameterType::kAuthorizationToken, token);
            }

            Bytes out;
            out << request_id << track_namespace_prefix << params;
            return out;
        }
    };

} // namespace quicr::messages::control
