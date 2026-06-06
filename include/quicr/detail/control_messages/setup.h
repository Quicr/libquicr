// SPDX-FileCopyrightText: Copyright (c) 2026 Cisco Systems
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

#include "quicr/detail/control_messages/message_reader.h"

namespace quicr::messages::control {

    struct Setup
    {
        static constexpr std::uint64_t kType = 0x2F00;

        const KeyValueAttributes setup_options;

        explicit Setup(BytesSpan payload)
          : Setup(MessageReader{ payload })
        {
        }

      private:
        explicit Setup(MessageReader reader)
          : setup_options(reader.Read<KeyValueAttributes>())
        {
            reader.ExpectDone();
        }
    };

} // namespace quicr::messages::control
