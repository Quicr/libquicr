// SPDX-FileCopyrightText: Copyright (c) 2026 Cisco Systems
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

#include "quicr/detail/control_messages/message_reader.h"
#include "quicr/detail/control_messages/parameters.h"

namespace quicr::messages::control {

    struct RequestUpdate
    {
        static constexpr std::uint64_t kType = 0x2;

        const RequestID request_id;
        const std::vector<Token> auth_tokens;
        const std::optional<std::uint64_t> object_delivery_timeout;
        const std::optional<std::uint64_t> subgroup_delivery_timeout;
        const std::optional<std::uint8_t> subscriber_priority;
        const std::optional<Filter> subscription_filter; // Absent means unchanged (§10.2.9).
        const std::optional<bool> forward;               // Absent means unchanged (§10.2.12).
        const std::optional<std::uint64_t> new_group_request;
        const std::optional<TrackNamespace> track_namespace_prefix;

        static RequestUpdate Decode(BytesSpan payload)
        {
            MessageReader reader{ payload };
            auto request_id = reader.Read<RequestID>();
            const auto params = reader.Read<Parameters>();
            reader.ExpectDone();

            ValidateParameters(params,
                               { ParameterType::kAuthorizationToken,
                                 ParameterType::kDeliveryTimeout,
                                 ParameterType::kSubgroupDeliveryTimeout,
                                 ParameterType::kSubscriberPriority,
                                 ParameterType::kLocationFilter,
                                 ParameterType::kSubgroupFilter,
                                 ParameterType::kObjectFilter,
                                 ParameterType::kPriorityFilter,
                                 ParameterType::kPropertyFilter,
                                 ParameterType::kTrackFilter,
                                 ParameterType::kForward,
                                 ParameterType::kNewGroupRequest,
                                 ParameterType::kTrackNamespacePrefix });

            return RequestUpdate{
                .request_id = request_id,
                .auth_tokens = CollectAuthTokens(params),
                .object_delivery_timeout = params.GetOptional<std::uint64_t>(ParameterType::kDeliveryTimeout),
                .subgroup_delivery_timeout = params.GetOptional<std::uint64_t>(ParameterType::kSubgroupDeliveryTimeout),
                .subscriber_priority = params.GetOptional<std::uint8_t>(ParameterType::kSubscriberPriority),
                .subscription_filter =
                  ContainsAnyFilter(params) ? std::optional<Filter>{ ResolveFilter(params) } : std::nullopt,
                .forward = params.Contains(ParameterType::kForward)
                             ? std::optional<bool>{ ResolveForward(params, true) }
                             : std::nullopt,
                .new_group_request = params.GetOptional<std::uint64_t>(ParameterType::kNewGroupRequest),
                .track_namespace_prefix = params.GetOptional<TrackNamespace>(ParameterType::kTrackNamespacePrefix),
            };
        }

        [[nodiscard]] Bytes Encode() const
        {
            auto params = Parameters{};
            for (const auto& token : auth_tokens) {
                params.Add(ParameterType::kAuthorizationToken, token);
            }
            params.AddOptional(ParameterType::kDeliveryTimeout, object_delivery_timeout);
            params.AddOptional(ParameterType::kSubgroupDeliveryTimeout, subgroup_delivery_timeout);
            params.AddOptional(ParameterType::kSubscriberPriority, subscriber_priority);
            if (subscription_filter.has_value()) {
                params.Add(GetFilterParameterType(subscription_filter.value()), subscription_filter.value());
            }
            if (forward.has_value()) {
                params.Add(ParameterType::kForward, static_cast<std::uint8_t>(forward.value()));
            }
            params.AddOptional(ParameterType::kNewGroupRequest, new_group_request);
            params.AddOptional(ParameterType::kTrackNamespacePrefix, track_namespace_prefix);

            Bytes out;
            out << request_id << params;
            return out;
        }
    };

} // namespace quicr::messages::control
