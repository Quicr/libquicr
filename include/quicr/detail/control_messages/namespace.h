// SPDX-FileCopyrightText: Copyright (c) 2026 Cisco Systems
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

#include "quicr/detail/control_messages/message_reader.h"

namespace quicr::messages::control {

    struct Namespace
    {
        static constexpr std::uint64_t kType = 0x8;

        const TrackNamespace track_namespace_suffix;

        static Namespace Decode(BytesSpan payload)
        {
            MessageReader reader{ payload };
            auto track_namespace_suffix = reader.Read<TrackNamespace>();
            reader.ExpectDone();

            return Namespace{ .track_namespace_suffix = std::move(track_namespace_suffix) };
        }

        [[nodiscard]] Bytes Encode() const
        {
            Bytes out;
            out << track_namespace_suffix;
            return out;
        }
    };

} // namespace quicr::messages::control
