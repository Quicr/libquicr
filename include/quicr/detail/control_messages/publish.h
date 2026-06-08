// SPDX-FileCopyrightText: Copyright (c) 2026 Cisco Systems
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

#include "quicr/detail/control_messages/message_reader.h"
#include "quicr/detail/control_messages/parameters.h"

namespace quicr::messages::control {

    struct Publish
    {
        static constexpr std::uint64_t kType = 0x1D;

        const RequestID request_id;
        const TrackNamespace track_namespace;
        const TrackName track_name;
        const TrackAlias track_alias;
        const std::vector<Token> auth_tokens;
        const std::optional<std::uint64_t> expires;
        const std::optional<Location> largest_object;
        const bool forward;
        const TrackExtensions track_properties;

        static Publish Decode(BytesSpan payload)
        {
            MessageReader reader{ payload };
            auto request_id = reader.Read<RequestID>();
            auto track_namespace = reader.Read<TrackNamespace>();
            auto track_name = reader.Read<TrackName>();
            auto track_alias = reader.Read<TrackAlias>();

            const auto params = reader.Read<Parameters>();
            auto track_properties = reader.Read<TrackExtensions>();
            reader.ExpectDone();

            ValidateParameters(params,
                               { ParameterType::kAuthorizationToken,
                                 ParameterType::kExpires,
                                 ParameterType::kLargestObject,
                                 ParameterType::kForward });

            return Publish{
                .request_id = request_id,
                .track_namespace = std::move(track_namespace),
                .track_name = std::move(track_name),
                .track_alias = track_alias,
                .auth_tokens = CollectAuthTokens(params),
                .expires = ResolveExpires(params),
                .largest_object = params.GetOptional<Location>(ParameterType::kLargestObject),
                .forward = ResolveForward(params, true),
                .track_properties = std::move(track_properties),
            };
        }

        [[nodiscard]] Bytes Encode() const
        {
            auto params = Parameters{};
            for (const auto& token : auth_tokens) {
                params.Add(ParameterType::kAuthorizationToken, token);
            }
            if (expires.has_value() && expires.value() != 0) {
                params.Add(ParameterType::kExpires, expires.value());
            }
            params.AddOptional(ParameterType::kLargestObject, largest_object);
            if (!forward) {
                params.Add(ParameterType::kForward, std::uint8_t{ 0 });
            }

            Bytes out;
            out << request_id << track_namespace << track_name << track_alias << params << track_properties;
            return out;
        }
    };

} // namespace quicr::messages::control
