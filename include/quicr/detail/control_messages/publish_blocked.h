// SPDX-FileCopyrightText: Copyright (c) 2026 Cisco Systems
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

#include "quicr/detail/control_messages/message_reader.h"

namespace quicr::messages::control {

    struct PublishBlocked
    {
        static constexpr std::uint64_t kType = 0xF;

        const TrackNamespace track_namespace_suffix;
        const Bytes track_name;

        explicit PublishBlocked(BytesSpan payload)
          : PublishBlocked(MessageReader{ payload })
        {
        }

      private:
        explicit PublishBlocked(MessageReader reader)
          : track_namespace_suffix(reader.Read<TrackNamespace>())
          , track_name(reader.Read<Bytes>())
        {
            reader.ExpectDone();
        }
    };

} // namespace quicr::messages::control
