// SPDX-FileCopyrightText: Copyright (c) 2026 Cisco Systems
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

#include "quicr/detail/control_messages/message_reader.h"
#include "quicr/detail/control_messages/parameters.h"
#include "quicr/detail/ctrl_message_types.h"
#include "quicr/track_name.h"

#include <variant>

namespace quicr::messages::control {

    namespace detail {
        inline Parameters EncodeFetchParameters(const std::vector<Token>& auth_tokens,
                                                std::optional<std::uint64_t> fill_timeout,
                                                std::uint8_t subscriber_priority,
                                                GroupOrder group_order)
        {
            auto params = Parameters{};
            for (const auto& token : auth_tokens) {
                params.Add(ParameterType::kAuthorizationToken, token);
            }
            params.AddOptional(ParameterType::kFillTimeout, fill_timeout);
            if (subscriber_priority != 128) {
                params.Add(ParameterType::kSubscriberPriority, subscriber_priority);
            }
            if (group_order != GroupOrder::kAscending) {
                params.Add(ParameterType::kGroupOrder, group_order);
            }
            return params;
        }
    } // namespace detail

    struct StandaloneFetch
    {
        static constexpr std::uint64_t kType = 0x16;

        const RequestID request_id;
        const FetchType fetch_type;
        const TrackNamespace track_namespace;
        const TrackName track_name;
        const Location start;
        const Location end;
        const std::vector<Token> auth_tokens;
        const std::optional<std::uint64_t> fill_timeout;
        const std::uint8_t subscriber_priority;
        const GroupOrder group_order;

        static StandaloneFetch Decode(BytesSpan payload)
        {
            MessageReader reader{ payload };
            auto request_id = reader.Read<RequestID>();
            auto fetch_type = ReadFetchType(reader);
            auto track_namespace = reader.Read<TrackNamespace>();
            auto track_name = reader.Read<TrackName>();
            auto start = reader.Read<Location>();
            auto end = reader.Read<Location>();

            const auto params = reader.Read<Parameters>();
            reader.ExpectDone();
            auto fetch_params = ResolveFetchParameters(params);

            return StandaloneFetch{
                .request_id = request_id,
                .fetch_type = fetch_type,
                .track_namespace = std::move(track_namespace),
                .track_name = std::move(track_name),
                .start = start,
                .end = end,
                .auth_tokens = std::move(fetch_params.auth_tokens),
                .fill_timeout = fetch_params.fill_timeout,
                .subscriber_priority = fetch_params.subscriber_priority,
                .group_order = fetch_params.group_order,
            };
        }

        [[nodiscard]] Bytes Encode() const
        {
            const auto params =
              detail::EncodeFetchParameters(auth_tokens, fill_timeout, subscriber_priority, group_order);
            Bytes out;
            out << request_id << static_cast<std::uint64_t>(fetch_type) << track_namespace << track_name << start << end
                << params;
            return out;
        }

      private:
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

        const RequestID request_id;
        const FetchType fetch_type;
        const RequestID joining_request_id;
        const std::uint64_t joining_start;
        const std::vector<Token> auth_tokens;
        const std::optional<std::uint64_t> fill_timeout;
        const std::uint8_t subscriber_priority;
        const GroupOrder group_order;

        static JoiningFetch Decode(BytesSpan payload)
        {
            MessageReader reader{ payload };
            auto request_id = reader.Read<RequestID>();
            auto fetch_type = ReadJoiningFetchType(reader);
            auto joining_request_id = reader.Read<RequestID>();
            auto joining_start = reader.Read<std::uint64_t>();

            const auto params = reader.Read<Parameters>();
            reader.ExpectDone();
            auto fetch_params = ResolveFetchParameters(params);

            return JoiningFetch{
                .request_id = request_id,
                .fetch_type = fetch_type,
                .joining_request_id = joining_request_id,
                .joining_start = joining_start,
                .auth_tokens = std::move(fetch_params.auth_tokens),
                .fill_timeout = fetch_params.fill_timeout,
                .subscriber_priority = fetch_params.subscriber_priority,
                .group_order = fetch_params.group_order,
            };
        }

        [[nodiscard]] Bytes Encode() const
        {
            const auto params =
              detail::EncodeFetchParameters(auth_tokens, fill_timeout, subscriber_priority, group_order);

            Bytes out;
            out << request_id << static_cast<std::uint64_t>(fetch_type) << joining_request_id << joining_start
                << params;
            return out;
        }

      private:
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
    inline Fetch ReadFetch(BytesSpan payload)
    {
        auto reader = MessageReader{ payload };
        [[maybe_unused]] const auto request_id = reader.Read<RequestID>();
        const auto fetch_type = static_cast<FetchType>(reader.Read<std::uint64_t>());

        switch (fetch_type) {
            case FetchType::kStandalone:
                return StandaloneFetch::Decode(payload);
            case FetchType::kRelativeJoiningFetch:
            case FetchType::kAbsoluteJoiningFetch:
                return JoiningFetch::Decode(payload);
        }

        throw ProtocolViolationException("Invalid FETCH type");
    }

} // namespace quicr::messages::control
