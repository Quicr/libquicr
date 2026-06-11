// SPDX-FileCopyrightText: Copyright (c) 2026 Cisco Systems
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

#include "quicr/detail/control_messages/message_reader.h"

namespace quicr::messages::control {

    struct Redirect
    {
        const Bytes connect_uri;
        const TrackNamespace track_namespace;
        const Bytes track_name;
    };

    struct RequestError
    {
        static constexpr std::uint64_t kType = 0x5;
        static constexpr std::uint64_t kRedirectErrorCode = 0x34;

        const ErrorCode error_code;
        const std::uint64_t retry_interval;
        const Bytes error_reason;
        const std::optional<Redirect> redirect;

        explicit RequestError(BytesSpan payload)
          : RequestError(MessageReader{ payload })
        {
        }

      private:
        explicit RequestError(MessageReader reader)
          : error_code(static_cast<ErrorCode>(reader.Read<std::uint64_t>()))
          , retry_interval(reader.Read<std::uint64_t>())
          , error_reason(reader.Read<Bytes>())
          , redirect(ReadRedirect(reader, error_code))
        {
            reader.ExpectDone();
        }

        static std::optional<Redirect> ReadRedirect(MessageReader& reader, ErrorCode error_code)
        {
            if (static_cast<std::uint64_t>(error_code) != kRedirectErrorCode) {
                return std::nullopt;
            }

            return Redirect{
                .connect_uri = reader.Read<Bytes>(),
                .track_namespace = reader.Read<TrackNamespace>(),
                .track_name = reader.Read<Bytes>(),
            };
        }
    };

} // namespace quicr::messages::control
