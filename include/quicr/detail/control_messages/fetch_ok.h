// SPDX-FileCopyrightText: Copyright (c) 2026 Cisco Systems
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

#include "quicr/detail/control_messages/message_reader.h"

namespace quicr::messages::control {

    struct FetchOk
    {
        static constexpr std::uint64_t kType = 0x18;

        const std::uint8_t end_of_track;
        const Location end_location;
        const Parameters parameters;
        const TrackExtensions track_properties;

        static FetchOk Decode(BytesSpan payload)
        {
            MessageReader reader{ payload };
            auto end_of_track = reader.Read<std::uint8_t>();
            auto end_location = reader.Read<Location>();

            auto parameters = reader.Read<Parameters>();
            auto track_properties = reader.Read<TrackExtensions>();
            reader.ExpectDone();

            return FetchOk{
                .end_of_track = end_of_track,
                .end_location = end_location,
                .parameters = std::move(parameters),
                .track_properties = std::move(track_properties),
            };
        }

        [[nodiscard]] Bytes Encode() const
        {
            Bytes out;
            out << end_of_track << end_location << parameters << track_properties;
            return out;
        }
    };

} // namespace quicr::messages::control
