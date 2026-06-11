// SPDX-FileCopyrightText: Copyright (c) 2026 Cisco Systems
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

#include "quicr/detail/control_messages/message_reader.h"

namespace quicr::messages::control {

    struct RequestUpdate
    {
        static constexpr std::uint64_t kType = 0x2;

        const std::uint64_t request_id;
        const Parameters parameters;

        explicit RequestUpdate(BytesSpan payload)
          : RequestUpdate(MessageReader{ payload })
        {
        }

      private:
        explicit RequestUpdate(MessageReader reader)
          : request_id(reader.Read<std::uint64_t>())
          , parameters(reader.Read<Parameters>())
        {
            reader.ExpectDone();
        }
    };

} // namespace quicr::messages::control
