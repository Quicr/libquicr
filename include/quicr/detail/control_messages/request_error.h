// SPDX-FileCopyrightText: Copyright (c) 2026 Cisco Systems
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

#include "quicr/detail/control_messages/message_reader.h"

namespace quicr::messages::control {

    struct Redirect
    {
        const Bytes connect_uri;
        const TrackNamespace track_namespace;
        const TrackName track_name;
    };

    struct RequestError
    {
        static constexpr std::uint64_t kType = 0x5;
        static constexpr std::uint64_t kRedirectErrorCode = 0x34;

        const ErrorCode error_code;
        const std::uint64_t retry_interval;
        const ReasonPhrase error_reason;
        // Present iff error_code is the redirect code (§10.6.1); the two travel together on the wire.
        const std::optional<Redirect> redirect;

        static RequestError Decode(BytesSpan payload)
        {
            MessageReader reader{ payload };
            auto error_code = static_cast<ErrorCode>(reader.Read<std::uint64_t>());
            auto retry_interval = reader.Read<std::uint64_t>();
            auto error_reason = reader.Read<ReasonPhrase>();
            auto redirect = ReadRedirect(reader, error_code);
            reader.ExpectDone();

            return RequestError{
                .error_code = error_code,
                .retry_interval = retry_interval,
                .error_reason = std::move(error_reason),
                .redirect = std::move(redirect),
            };
        }

        [[nodiscard]] Bytes Encode() const
        {
            Bytes out;
            out << static_cast<std::uint64_t>(error_code) << retry_interval << error_reason;
            if (static_cast<std::uint64_t>(error_code) == kRedirectErrorCode && redirect.has_value()) {
                out << redirect->connect_uri << redirect->track_namespace << redirect->track_name;
            }
            return out;
        }

      private:
        static std::optional<Redirect> ReadRedirect(MessageReader& reader, ErrorCode error_code)
        {
            if (static_cast<std::uint64_t>(error_code) != kRedirectErrorCode) {
                return std::nullopt;
            }

            return Redirect{
                .connect_uri = reader.Read<Bytes>(),
                .track_namespace = reader.Read<TrackNamespace>(),
                .track_name = reader.Read<TrackName>(),
            };
        }
    };

} // namespace quicr::messages::control
