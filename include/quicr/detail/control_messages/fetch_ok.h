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

        explicit FetchOk(BytesSpan payload)
          : FetchOk(MessageReader{ payload })
        {
        }

      private:
        explicit FetchOk(MessageReader reader)
          : end_of_track(reader.Read<std::uint8_t>())
          , end_location(reader.Read<Location>())
          , parameters(reader.Read<Parameters>())
          , track_properties(reader.Read<TrackExtensions>())
        {
            reader.ExpectDone();
        }
    };

} // namespace quicr::messages::control
