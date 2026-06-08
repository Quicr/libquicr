// SPDX-FileCopyrightText: Copyright (c) 2026 Cisco Systems
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

#include "quicr/detail/control_messages/message_reader.h"
#include "quicr/detail/control_messages/parameters.h"

namespace quicr::messages::control {

    struct SubscribeTracks
    {
        static constexpr std::uint64_t kType = 0x51;

        const RequestID request_id;
        const TrackNamespace track_namespace_prefix;
        const std::vector<Token> auth_tokens;
        const bool forward; // Absent on the wire means true (§10.2.12).

        static SubscribeTracks Decode(BytesSpan payload)
        {
            MessageReader reader{ payload };
            auto request_id = reader.Read<RequestID>();
            auto track_namespace_prefix = reader.Read<TrackNamespace>();

            const auto params = reader.Read<Parameters>();
            reader.ExpectDone();

            ValidateParameters(params, { ParameterType::kAuthorizationToken, ParameterType::kForward });

            return SubscribeTracks{
                .request_id = request_id,
                .track_namespace_prefix = std::move(track_namespace_prefix),
                .auth_tokens = CollectAuthTokens(params),
                .forward = ResolveForward(params, true),
            };
        }

        [[nodiscard]] Bytes Encode() const
        {
            auto params = Parameters{};
            for (const auto& token : auth_tokens) {
                params.Add(ParameterType::kAuthorizationToken, token);
            }
            if (!forward) {
                params.Add(ParameterType::kForward, std::uint8_t{ 0 });
            }

            Bytes out;
            out << request_id << track_namespace_prefix << params;
            return out;
        }
    };

} // namespace quicr::messages::control
