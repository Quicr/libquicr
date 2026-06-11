// SPDX-FileCopyrightText: Copyright (c) 2026 Cisco Systems
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

#include "quicr/detail/control_messages/message_reader.h"

namespace quicr::messages::control {

    struct TrackStatus
    {
        static constexpr std::uint64_t kType = 0xD;

        const std::uint64_t request_id;
        const TrackNamespace track_namespace;
        const Bytes track_name;
        const Parameters parameters;

        explicit TrackStatus(BytesSpan payload)
          : TrackStatus(MessageReader{ payload })
        {
        }

      private:
        explicit TrackStatus(MessageReader reader)
          : request_id(reader.Read<std::uint64_t>())
          , track_namespace(reader.Read<TrackNamespace>())
          , track_name(reader.Read<Bytes>())
          , parameters(reader.Read<Parameters>())
        {
            reader.ExpectDone();
        }
    };

} // namespace quicr::messages::control
