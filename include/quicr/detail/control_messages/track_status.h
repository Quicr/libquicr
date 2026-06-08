// SPDX-FileCopyrightText: Copyright (c) 2026 Cisco Systems
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

#include "quicr/detail/control_messages/message_reader.h"
#include "quicr/detail/control_messages/parameters.h"

namespace quicr::messages::control {

    struct TrackStatus
    {
        static constexpr std::uint64_t kType = 0xD;

        const RequestID request_id;
        const TrackNamespace track_namespace;
        const TrackName track_name;
        const std::vector<Token> auth_tokens;

        static TrackStatus Decode(BytesSpan payload)
        {
            MessageReader reader{ payload };
            auto request_id = reader.Read<RequestID>();
            auto track_namespace = reader.Read<TrackNamespace>();
            auto track_name = reader.Read<TrackName>();

            const auto params = reader.Read<Parameters>();
            reader.ExpectDone();

            ValidateParameters(params, { ParameterType::kAuthorizationToken });

            return TrackStatus{
                .request_id = request_id,
                .track_namespace = std::move(track_namespace),
                .track_name = std::move(track_name),
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
            out << request_id << track_namespace << track_name << params;
            return out;
        }
    };

} // namespace quicr::messages::control
