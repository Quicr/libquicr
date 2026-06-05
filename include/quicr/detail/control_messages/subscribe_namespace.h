// SPDX-FileCopyrightText: Copyright (c) 2026 Cisco Systems
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

#include "quicr/detail/control_messages/message_reader.h"

namespace quicr::messages::control {

    struct SubscribeNamespace
    {
        static constexpr std::uint64_t kType = 0x50;

        const RequestID request_id;
        const TrackNamespace track_namespace_prefix;
        const Parameters parameters;

        explicit SubscribeNamespace(BytesSpan payload)
          : SubscribeNamespace(MessageReader{ payload })
        {
        }

      private:
        explicit SubscribeNamespace(MessageReader reader)
          : request_id(reader.Read<RequestID>())
          , track_namespace_prefix(reader.Read<TrackNamespace>())
          , parameters(reader.Read<Parameters>())
        {
            reader.ExpectDone();
        }
    };

} // namespace quicr::messages::control
