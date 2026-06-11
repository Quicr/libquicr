// SPDX-FileCopyrightText: Copyright (c) 2026 Cisco Systems
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

#include "quicr/detail/control_messages/message_reader.h"

namespace quicr::messages::control {

    struct SubscribeTracks
    {
        static constexpr std::uint64_t kType = 0x51;

        const std::uint64_t request_id;
        const TrackNamespace track_namespace_prefix;
        const Parameters parameters;

        explicit SubscribeTracks(BytesSpan payload)
          : SubscribeTracks(MessageReader{ payload })
        {
        }

      private:
        explicit SubscribeTracks(MessageReader reader)
          : request_id(reader.Read<std::uint64_t>())
          , track_namespace_prefix(reader.Read<TrackNamespace>())
          , parameters(reader.Read<Parameters>())
        {
            reader.ExpectDone();
        }
    };

} // namespace quicr::messages::control
