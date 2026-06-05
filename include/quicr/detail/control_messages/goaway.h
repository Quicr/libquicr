// SPDX-FileCopyrightText: Copyright (c) 2026 Cisco Systems
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

#include "quicr/detail/control_messages/message_reader.h"

namespace quicr::messages::control {

    struct RequestGoaway
    {
        static constexpr std::uint64_t kType = 0x10;

        const Bytes new_session_uri;
        const std::uint64_t timeout;

        explicit RequestGoaway(BytesSpan payload)
          : RequestGoaway(MessageReader{ payload })
        {
        }

      private:
        explicit RequestGoaway(MessageReader reader)
          : new_session_uri(reader.Read<Bytes>())
          , timeout(reader.Read<std::uint64_t>())
        {
            reader.ExpectDone();
        }
    };

    struct ControlGoaway
    {
        static constexpr std::uint64_t kType = 0x10;

        const Bytes new_session_uri;
        const std::uint64_t timeout;
        const RequestID request_id;

        explicit ControlGoaway(BytesSpan payload)
          : ControlGoaway(MessageReader{ payload })
        {
        }

      private:
        explicit ControlGoaway(MessageReader reader)
          : new_session_uri(reader.Read<Bytes>())
          , timeout(reader.Read<std::uint64_t>())
          , request_id(reader.Read<RequestID>())
        {
            reader.ExpectDone();
        }
    };

} // namespace quicr::messages::control
