// SPDX-FileCopyrightText: Copyright (c) 2026 Cisco Systems
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

#include "quicr/detail/control_messages/message_reader.h"

namespace quicr::messages::control {

    struct Publish
    {
        static constexpr std::uint64_t kType = 0x1D;

        const RequestID request_id;
        const TrackNamespace track_namespace;
        const TrackName track_name;
        const TrackAlias track_alias;
        const Parameters parameters;
        const TrackExtensions track_properties;

        explicit Publish(BytesSpan payload)
          : Publish(MessageReader{ payload })
        {
        }

      private:
        explicit Publish(MessageReader reader)
          : request_id(reader.Read<RequestID>())
          , track_namespace(reader.Read<TrackNamespace>())
          , track_name(reader.Read<TrackName>())
          , track_alias(reader.Read<TrackAlias>())
          , parameters(reader.Read<Parameters>())
          , track_properties(reader.Read<TrackExtensions>())
        {
            reader.ExpectDone();
        }
    };

} // namespace quicr::messages::control
