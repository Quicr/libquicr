// SPDX-FileCopyrightText: Copyright (c) 2026 Cisco Systems
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

#include "quicr/detail/control_messages/message_reader.h"

namespace quicr::messages::control {

    struct PublishBlocked
    {
        static constexpr std::uint64_t kType = 0xF;

        const TrackNamespace track_namespace_suffix;
        const TrackName track_name;

        static PublishBlocked Decode(BytesSpan payload)
        {
            MessageReader reader{ payload };
            auto track_namespace_suffix = reader.Read<TrackNamespace>();
            auto track_name = reader.Read<TrackName>();
            reader.ExpectDone();

            return PublishBlocked{
                .track_namespace_suffix = std::move(track_namespace_suffix),
                .track_name = std::move(track_name),
            };
        }

        [[nodiscard]] Bytes Encode() const
        {
            Bytes out;
            out << track_namespace_suffix << track_name;
            return out;
        }
    };

} // namespace quicr::messages::control
