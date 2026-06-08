// SPDX-FileCopyrightText: Copyright (c) 2026 Cisco Systems
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

#include "quicr/detail/control_messages/message_reader.h"
#include "quicr/detail/control_messages/parameters.h"

namespace quicr::messages::control {

    struct SubscribeOk
    {
        static constexpr std::uint64_t kType = 0x4;

        const TrackAlias track_alias;
        const std::optional<std::uint64_t> expires; // Absent or 0 both mean "no expiry" (§10.2.10).
        const std::optional<Location> largest_object;
        const TrackExtensions track_properties;

        static SubscribeOk Decode(BytesSpan payload)
        {
            MessageReader reader{ payload };
            auto track_alias = reader.Read<TrackAlias>();

            const auto params = reader.Read<Parameters>();
            auto track_properties = reader.Read<TrackExtensions>();
            reader.ExpectDone();

            ValidateParameters(params, { ParameterType::kExpires, ParameterType::kLargestObject });

            return SubscribeOk{
                .track_alias = track_alias,
                .expires = ResolveExpires(params),
                .largest_object = params.GetOptional<Location>(ParameterType::kLargestObject),
                .track_properties = std::move(track_properties),
            };
        }

        [[nodiscard]] Bytes Encode() const
        {
            auto params = Parameters{};
            if (expires.has_value() && expires.value() != 0) {
                params.Add(ParameterType::kExpires, expires.value());
            }
            params.AddOptional(ParameterType::kLargestObject, largest_object);

            Bytes out;
            out << track_alias << params << track_properties;
            return out;
        }
    };

} // namespace quicr::messages::control
