// SPDX-FileCopyrightText: Copyright (c) 2026 Cisco Systems
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

#include "quicr/detail/control_messages/message_reader.h"
#include "quicr/detail/control_messages/parameters.h"

namespace quicr::messages::control {

    // Every REQUEST_OK is a parameter block optionally followed by Track Properties (§10.5).
    // Only TRACK_STATUS_OK carries properties; the rest MUST be empty, so their Decode reads the
    // parameters and then expects the message to be done -- trailing property bytes are a violation.
    inline Parameters ReadOkParameters(MessageReader& reader)
    {
        auto parameters = reader.Read<Parameters>();
        reader.ExpectDone();
        return parameters;
    }

    struct PublishOk
    {
        static constexpr std::uint64_t kType = 0x7;

        const std::optional<std::uint64_t> object_delivery_timeout;
        const std::optional<std::uint64_t> subgroup_delivery_timeout;
        const std::uint8_t subscriber_priority;      // Absent on the wire means 128 (§10.2.7).
        const std::optional<GroupOrder> group_order; // Absent means the Track's preference (§10.2.8).
        const Filter subscription_filter;            // monostate means unfiltered (§10.2.9).
        const std::optional<std::uint64_t> expires;  // Absent or 0 both mean "no expiry" (§10.2.10).
        const bool forward;                          // Absent on the wire means true (§10.2.12).
        const std::optional<std::uint64_t> new_group_request;

        static PublishOk Decode(BytesSpan payload)
        {
            MessageReader reader{ payload };
            const auto params = ReadOkParameters(reader);

            ValidateParameters(params,
                               { ParameterType::kDeliveryTimeout,
                                 ParameterType::kSubgroupDeliveryTimeout,
                                 ParameterType::kSubscriberPriority,
                                 ParameterType::kGroupOrder,
                                 ParameterType::kLocationFilter,
                                 ParameterType::kSubgroupFilter,
                                 ParameterType::kObjectFilter,
                                 ParameterType::kPriorityFilter,
                                 ParameterType::kPropertyFilter,
                                 ParameterType::kTrackFilter,
                                 ParameterType::kExpires,
                                 ParameterType::kForward,
                                 ParameterType::kNewGroupRequest });

            return PublishOk{
                .object_delivery_timeout = params.GetOptional<std::uint64_t>(ParameterType::kDeliveryTimeout),
                .subgroup_delivery_timeout = params.GetOptional<std::uint64_t>(ParameterType::kSubgroupDeliveryTimeout),
                .subscriber_priority =
                  params.GetOptional<std::uint8_t>(ParameterType::kSubscriberPriority).value_or(128),
                .group_order = ResolveGroupOrder(params),
                .subscription_filter = ResolveFilter(params),
                .expires = ResolveExpires(params),
                .forward = ResolveForward(params, true),
                .new_group_request = params.GetOptional<std::uint64_t>(ParameterType::kNewGroupRequest),
            };
        }

        [[nodiscard]] Bytes Encode() const
        {
            auto params = Parameters{};
            params.AddOptional(ParameterType::kDeliveryTimeout, object_delivery_timeout);
            params.AddOptional(ParameterType::kSubgroupDeliveryTimeout, subgroup_delivery_timeout);
            if (subscriber_priority != 128) {
                params.Add(ParameterType::kSubscriberPriority, subscriber_priority);
            }
            params.AddOptional(ParameterType::kGroupOrder, group_order);
            if (const auto filter_type = GetFilterParameterType(subscription_filter);
                filter_type != ParameterType::kInvalid) {
                params.Add(filter_type, subscription_filter);
            }
            if (expires.has_value() && expires.value() != 0) {
                params.Add(ParameterType::kExpires, expires.value());
            }
            if (!forward) {
                params.Add(ParameterType::kForward, std::uint8_t{ 0 });
            }
            params.AddOptional(ParameterType::kNewGroupRequest, new_group_request);

            Bytes out;
            out << params;
            return out;
        }
    };

    struct RequestUpdateOk
    {
        static constexpr std::uint64_t kType = 0x7;

        const std::optional<std::uint64_t> expires; // Absent or 0 both mean "no expiry" (§10.2.10).
        const std::optional<Location> largest_object;

        static RequestUpdateOk Decode(BytesSpan payload)
        {
            MessageReader reader{ payload };
            const auto params = ReadOkParameters(reader);

            ValidateParameters(params, { ParameterType::kExpires, ParameterType::kLargestObject });

            return RequestUpdateOk{
                .expires = ResolveExpires(params),
                .largest_object = params.GetOptional<Location>(ParameterType::kLargestObject),
            };
        }

        [[nodiscard]] Bytes Encode() const
        {
            auto params = Parameters{};
            if (expires.has_value() && expires.value() != 0) {
                params.Add(ParameterType::kExpires, expires.value());
            }
            params.AddOptional(ParameterType::kLargestObject, largest_object);

            Bytes out;
            out << params;
            return out;
        }
    };

    // The only REQUEST_OK that carries Track Properties (§10.5): they follow the parameter block
    // and consume the remaining bytes of the message.
    struct TrackStatusOk
    {
        static constexpr std::uint64_t kType = 0x7;

        const std::optional<Location> largest_object;
        const TrackExtensions track_properties;

        static TrackStatusOk Decode(BytesSpan payload)
        {
            MessageReader reader{ payload };
            const auto params = reader.Read<Parameters>();
            auto track_properties = reader.Read<TrackExtensions>();
            reader.ExpectDone();

            ValidateParameters(params, { ParameterType::kLargestObject });

            return TrackStatusOk{
                .largest_object = params.GetOptional<Location>(ParameterType::kLargestObject),
                .track_properties = std::move(track_properties),
            };
        }

        [[nodiscard]] Bytes Encode() const
        {
            auto params = Parameters{};
            params.AddOptional(ParameterType::kLargestObject, largest_object);

            Bytes out;
            out << params << track_properties;
            return out;
        }
    };

    // No-parameter, no-properties OK messages: any parameter or trailing property byte is a violation.
    struct EmptyOk
    {
        static EmptyOk Decode(BytesSpan payload)
        {
            MessageReader reader{ payload };
            const auto params = ReadOkParameters(reader);
            ValidateParameters(params, std::initializer_list<ParameterType>{});
            return EmptyOk{};
        }

        [[nodiscard]] Bytes Encode() const
        {
            Bytes out;
            out << Parameters{};
            return out;
        }
    };

    struct PublishNamespaceOk : EmptyOk
    {
        static constexpr std::uint64_t kType = 0x7;
        static PublishNamespaceOk Decode(BytesSpan payload) { return PublishNamespaceOk{ EmptyOk::Decode(payload) }; }
    };

    struct SubscribeNamespaceOk : EmptyOk
    {
        static constexpr std::uint64_t kType = 0x7;
        static SubscribeNamespaceOk Decode(BytesSpan payload)
        {
            return SubscribeNamespaceOk{ EmptyOk::Decode(payload) };
        }
    };

    struct SubscribeTracksOk : EmptyOk
    {
        static constexpr std::uint64_t kType = 0x7;
        static SubscribeTracksOk Decode(BytesSpan payload) { return SubscribeTracksOk{ EmptyOk::Decode(payload) }; }
    };

} // namespace quicr::messages::control
