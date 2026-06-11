// SPDX-FileCopyrightText: Copyright (c) 2026 Cisco Systems
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

#include "quicr/detail/control_messages/message_reader.h"
#include "quicr/detail/ctrl_message_types.h"
#include "quicr/track_name.h"

#include <variant>

namespace quicr::messages::control {

    struct StandaloneFetch
    {
        static constexpr std::uint64_t kType = 0x16;

        const std::uint64_t request_id;
        const FetchType fetch_type;
        const TrackNamespace track_namespace;
        const Bytes track_name;
        const Location start;
        const Location end;
        const Parameters parameters;

        explicit StandaloneFetch(BytesSpan payload)
          : StandaloneFetch(MessageReader{ payload })
        {
        }

      private:
        explicit StandaloneFetch(MessageReader reader)
          : request_id(reader.Read<std::uint64_t>())
          , fetch_type(ReadFetchType(reader))
          , track_namespace(reader.Read<TrackNamespace>())
          , track_name(reader.Read<Bytes>())
          , start(reader.Read<Location>())
          , end(reader.Read<Location>())
          , parameters(reader.Read<Parameters>())
        {
            reader.ExpectDone();
        }

        static FetchType ReadFetchType(MessageReader& reader)
        {
            const auto fetch_type = static_cast<FetchType>(reader.Read<std::uint64_t>());
            switch (fetch_type) {
                case FetchType::kStandalone:
                    return fetch_type;
                default:
                    throw ProtocolViolationException("Invalid standalone FETCH type");
            }
        }
    };

    struct JoiningFetch
    {
        static constexpr std::uint64_t kType = 0x16;

        const std::uint64_t request_id;
        const FetchType fetch_type;
        const std::uint64_t joining_request_id;
        const std::uint64_t joining_start;
        const Parameters parameters;

        explicit JoiningFetch(BytesSpan payload)
          : JoiningFetch(MessageReader{ payload })
        {
        }

      private:
        explicit JoiningFetch(MessageReader reader)
          : request_id(reader.Read<std::uint64_t>())
          , fetch_type(ReadJoiningFetchType(reader))
          , joining_request_id(reader.Read<std::uint64_t>())
          , joining_start(reader.Read<std::uint64_t>())
          , parameters(reader.Read<Parameters>())
        {
            reader.ExpectDone();
        }

        static FetchType ReadJoiningFetchType(MessageReader& reader)
        {
            const auto fetch_type = static_cast<FetchType>(reader.Read<std::uint64_t>());
            switch (fetch_type) {
                case FetchType::kRelativeJoiningFetch:
                    [[fallthrough]];
                case FetchType::kAbsoluteJoiningFetch:
                    return fetch_type;
                default:
                    throw ProtocolViolationException("Invalid joining FETCH type");
            }
        }
    };

    using Fetch = std::variant<StandaloneFetch, JoiningFetch>;
    Fetch ReadFetch(BytesSpan payload)
    {
        auto reader = MessageReader{ payload };
        // TODO: We don't need to re-read these after we read them here.
        [[maybe_unused]] const auto request_id = reader.Read<std::uint64_t>();
        const auto fetch_type = static_cast<FetchType>(reader.Read<std::uint64_t>());

        switch (fetch_type) {
            case FetchType::kStandalone:
                return StandaloneFetch{ payload };
            case FetchType::kRelativeJoiningFetch:
            case FetchType::kAbsoluteJoiningFetch:
                return JoiningFetch{ payload };
        }

        throw ProtocolViolationException("Invalid FETCH type");
    }

} // namespace quicr::messages::control
