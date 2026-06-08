// SPDX-FileCopyrightText: Copyright (c) 2026 Cisco Systems
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

#include "quicr/detail/control_messages/message_reader.h"

namespace quicr::messages::control {

    // A GOAWAY sent on a request stream: no Request ID (§10.4).
    struct RequestGoaway
    {
        static constexpr std::uint64_t kType = 0x10;

        const Bytes new_session_uri;
        const std::uint64_t timeout;

        static RequestGoaway Decode(BytesSpan payload)
        {
            MessageReader reader{ payload };
            auto new_session_uri = reader.Read<Bytes>();
            auto timeout = reader.Read<std::uint64_t>();
            reader.ExpectDone();

            return RequestGoaway{
                .new_session_uri = std::move(new_session_uri),
                .timeout = timeout,
            };
        }

        [[nodiscard]] Bytes Encode() const
        {
            Bytes out;
            out << new_session_uri << timeout;
            return out;
        }
    };

    // A GOAWAY sent on the control stream: carries the trailing Request ID (§10.4).
    struct ControlGoaway
    {
        static constexpr std::uint64_t kType = 0x10;

        const Bytes new_session_uri;
        const std::uint64_t timeout;
        const RequestID request_id;

        static ControlGoaway Decode(BytesSpan payload)
        {
            MessageReader reader{ payload };
            auto new_session_uri = reader.Read<Bytes>();
            auto timeout = reader.Read<std::uint64_t>();
            auto request_id = reader.Read<RequestID>();
            reader.ExpectDone();

            return ControlGoaway{
                .new_session_uri = std::move(new_session_uri),
                .timeout = timeout,
                .request_id = request_id,
            };
        }

        [[nodiscard]] Bytes Encode() const
        {
            Bytes out;
            out << new_session_uri << timeout << request_id;
            return out;
        }
    };

} // namespace quicr::messages::control
