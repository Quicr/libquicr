// SPDX-FileCopyrightText: Copyright (c) 2026 Cisco Systems
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

#include "quicr/detail/control_messages/message_reader.h"

namespace quicr::messages::control {

    struct SubscribeOk
    {
        static constexpr std::uint64_t kType = 0x4;

        const TrackAlias track_alias;
        const Parameters parameters;
        const TrackExtensions track_properties;

        explicit SubscribeOk(BytesSpan payload)
          : SubscribeOk(MessageReader{ payload })
        {
        }

      private:
        explicit SubscribeOk(MessageReader reader)
          : track_alias(reader.Read<TrackAlias>())
          , parameters(reader.Read<Parameters>())
          , track_properties(reader.Read<TrackExtensions>())
        {
            reader.ExpectDone();
        }
    };

} // namespace quicr::messages::control
