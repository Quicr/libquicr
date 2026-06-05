// SPDX-FileCopyrightText: Copyright (c) 2026 Cisco Systems
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

#include "quicr/detail/control_messages/message_reader.h"

namespace quicr::messages::control {

    struct NamespaceDone
    {
        static constexpr std::uint64_t kType = 0xE;

        const TrackNamespace track_namespace_suffix;

        explicit NamespaceDone(BytesSpan payload)
          : NamespaceDone(MessageReader{ payload })
        {
        }

      private:
        explicit NamespaceDone(MessageReader reader)
          : track_namespace_suffix(reader.Read<TrackNamespace>())
        {
            reader.ExpectDone();
        }
    };

} // namespace quicr::messages::control
