// SPDX-FileCopyrightText: Copyright (c) 2026 Cisco Systems
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

#include "quicr/detail/control_messages/message_reader.h"

namespace quicr::messages::control {

    struct Setup
    {
        static constexpr std::uint64_t kType = 0x2F00;

        const KeyValuePairs setup_options;

        static Setup Decode(BytesSpan payload)
        {
            MessageReader reader{ payload };
            auto setup_options = reader.Read<KeyValuePairs>();
            reader.ExpectDone();

            return Setup{ .setup_options = std::move(setup_options) };
        }

        [[nodiscard]] Bytes Encode() const
        {
            Bytes out;
            out << setup_options;
            return out;
        }
    };

} // namespace quicr::messages::control
