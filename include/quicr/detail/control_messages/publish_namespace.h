// SPDX-FileCopyrightText: Copyright (c) 2026 Cisco Systems
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

#include "quicr/detail/control_messages/message_reader.h"

namespace quicr::messages::control {

    struct PublishNamespace
    {
        static constexpr std::uint64_t kType = 0x6;

        const RequestID request_id;
        const TrackNamespace track_namespace;
        const Parameters parameters;

        explicit PublishNamespace(BytesSpan payload)
          : PublishNamespace(MessageReader{ payload })
        {
        }

      private:
        explicit PublishNamespace(MessageReader reader)
          : request_id(reader.Read<RequestID>())
          , track_namespace(reader.Read<TrackNamespace>())
          , parameters(reader.Read<Parameters>())
        {
            reader.ExpectDone();
        }
    };

} // namespace quicr::messages::control
