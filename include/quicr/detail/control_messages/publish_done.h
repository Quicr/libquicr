// SPDX-FileCopyrightText: Copyright (c) 2026 Cisco Systems
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

#include "quicr/detail/control_messages/message_reader.h"

namespace quicr::messages::control {

    struct PublishDone
    {
        static constexpr std::uint64_t kType = 0xB;

        const PublishDoneStatus status_code;
        const std::uint64_t stream_count;
        const Bytes error_reason;

        explicit PublishDone(BytesSpan payload)
          : PublishDone(MessageReader{ payload })
        {
        }

      private:
        explicit PublishDone(MessageReader reader)
          : status_code(static_cast<PublishDoneStatus>(reader.Read<std::uint64_t>()))
          , stream_count(reader.Read<std::uint64_t>())
          , error_reason(reader.Read<Bytes>())
        {
            reader.ExpectDone();
        }
    };

} // namespace quicr::messages::control
