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
        const ReasonPhrase error_reason;

        static PublishDone Decode(BytesSpan payload)
        {
            MessageReader reader{ payload };
            auto status_code = static_cast<PublishDoneStatus>(reader.Read<std::uint64_t>());
            auto stream_count = reader.Read<std::uint64_t>();
            auto error_reason = reader.Read<ReasonPhrase>();
            reader.ExpectDone();

            return PublishDone{
                .status_code = status_code,
                .stream_count = stream_count,
                .error_reason = std::move(error_reason),
            };
        }

        [[nodiscard]] Bytes Encode() const
        {
            Bytes out;
            out << static_cast<std::uint64_t>(status_code) << stream_count << error_reason;
            return out;
        }
    };

} // namespace quicr::messages::control
