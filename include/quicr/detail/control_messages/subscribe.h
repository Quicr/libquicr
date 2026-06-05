// SPDX-FileCopyrightText: Copyright (c) 2026 Cisco Systems
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

#include "quicr/detail/control_messages/message_reader.h"

namespace quicr::messages::control {

    struct Subscribe
    {
        static constexpr std::uint64_t kType = 0x3;

        const RequestID request_id;
        const TrackNamespace track_namespace;
        const TrackName track_name;
        const Parameters parameters;

        explicit Subscribe(BytesSpan payload)
          : Subscribe(MessageReader{ payload })
        {
        }

      private:
        explicit Subscribe(MessageReader reader)
          : request_id(reader.Read<RequestID>())
          , track_namespace(reader.Read<TrackNamespace>())
          , track_name(reader.Read<TrackName>())
          , parameters(reader.Read<Parameters>())
        {
            reader.ExpectDone();
        }
    };

} // namespace quicr::messages::control
