// SPDX-FileCopyrightText: Copyright (c) 2026 Cisco Systems
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

#include "quicr/detail/control_messages/message_reader.h"
#include "quicr/detail/control_messages/parameters.h"

namespace quicr::messages::control {

    struct Subscribe
    {
        static constexpr std::uint64_t kType = 0x3;

        const RequestID request_id;
        const TrackNamespace track_namespace;
        const TrackName track_name;
        const std::vector<Token> auth_tokens;
        const std::optional<std::uint64_t> object_delivery_timeout;
        const std::optional<std::uint64_t> subgroup_delivery_timeout;
        const std::uint64_t rendezvous_timeout;      // Absent on the wire means 0 (§10.2.6).
        const std::uint8_t subscriber_priority;      // Absent on the wire means 128 (§10.2.7).
        const std::optional<GroupOrder> group_order; // Absent means the Track's preference (§10.2.8).
        const Filter subscription_filter;            // monostate means unfiltered (§10.2.9).
        const bool forward;                          // Absent on the wire means true (§10.2.12).
        const std::optional<std::uint64_t> new_group_request;

        /// Parse and validate a SUBSCRIBE off the wire, applying the §10.2 receive-side defaults.
        static Subscribe Decode(BytesSpan payload)
        {
            MessageReader reader{ payload };
            auto request_id = reader.Read<RequestID>();
            auto track_namespace = reader.Read<TrackNamespace>();
            auto track_name = reader.Read<TrackName>();

            const auto params = reader.Read<Parameters>();
            reader.ExpectDone();

            ValidateParameters(params,
                               { ParameterType::kAuthorizationToken,
                                 ParameterType::kDeliveryTimeout,
                                 ParameterType::kSubgroupDeliveryTimeout,
                                 ParameterType::kRendezvousTimeout,
                                 ParameterType::kSubscriberPriority,
                                 ParameterType::kGroupOrder,
                                 ParameterType::kLocationFilter,
                                 ParameterType::kSubgroupFilter,
                                 ParameterType::kObjectFilter,
                                 ParameterType::kPriorityFilter,
                                 ParameterType::kPropertyFilter,
                                 ParameterType::kTrackFilter,
                                 ParameterType::kForward,
                                 ParameterType::kNewGroupRequest });

            return Subscribe{
                .request_id = request_id,
                .track_namespace = std::move(track_namespace),
                .track_name = std::move(track_name),
                .auth_tokens = CollectAuthTokens(params),
                .object_delivery_timeout = params.GetOptional<std::uint64_t>(ParameterType::kDeliveryTimeout),
                .subgroup_delivery_timeout = params.GetOptional<std::uint64_t>(ParameterType::kSubgroupDeliveryTimeout),
                .rendezvous_timeout = params.GetOptional<std::uint64_t>(ParameterType::kRendezvousTimeout).value_or(0),
                .subscriber_priority =
                  params.GetOptional<std::uint8_t>(ParameterType::kSubscriberPriority).value_or(128),
                .group_order = ResolveGroupOrder(params),
                .subscription_filter = ResolveFilter(params),
                .forward = ResolveForward(params, true),
                .new_group_request = params.GetOptional<std::uint64_t>(ParameterType::kNewGroupRequest),
            };
        }

        /// Serialise to the wire, omitting any parameter whose value equals its §10.2 default.
        [[nodiscard]] Bytes Encode() const
        {
            auto params = Parameters{};
            for (const auto& token : auth_tokens) {
                params.Add(ParameterType::kAuthorizationToken, token);
            }
            params.AddOptional(ParameterType::kDeliveryTimeout, object_delivery_timeout);
            params.AddOptional(ParameterType::kSubgroupDeliveryTimeout, subgroup_delivery_timeout);
            if (rendezvous_timeout != 0) {
                params.Add(ParameterType::kRendezvousTimeout, rendezvous_timeout);
            }
            if (subscriber_priority != 128) {
                params.Add(ParameterType::kSubscriberPriority, subscriber_priority);
            }
            params.AddOptional(ParameterType::kGroupOrder, group_order);
            if (!forward) {
                params.Add(ParameterType::kForward, std::uint8_t{ 0 });
            }
            params.AddOptional(ParameterType::kNewGroupRequest, new_group_request);
            if (const auto filter_type = GetFilterParameterType(subscription_filter);
                filter_type != ParameterType::kInvalid) {
                params.Add(filter_type, subscription_filter);
            }

            Bytes out;
            out << request_id << track_namespace << track_name << params;
            return out;
        }
    };

} // namespace quicr::messages::control
