// SPDX-FileCopyrightText: Copyright (c) 2026 Cisco Systems
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

#include "quicr/detail/ctrl_message_types.h"

#include <algorithm>
#include <initializer_list>
#include <vector>

namespace quicr::messages::control {

    /// Enforce the §10.2 receipt rules: every present type must be in `allowed`,
    /// and no type may repeat except AUTHORIZATION_TOKEN.
    inline void ValidateParameters(const Parameters& params, std::initializer_list<ParameterType> allowed)
    {
        std::vector<ParameterType> seen;
        for (const auto& kvp : params) {
            if (std::find(allowed.begin(), allowed.end(), kvp.type) == allowed.end()) {
                throw ProtocolViolationException("Parameter not valid for this message type");
            }

            if (kvp.type != ParameterType::kAuthorizationToken) {
                if (std::find(seen.begin(), seen.end(), kvp.type) != seen.end()) {
                    throw ProtocolViolationException("Unexpected duplicate parameter");
                }
                seen.push_back(kvp.type);
            }
        }
    }

    /// Collect every AUTHORIZATION_TOKEN parameter as a parsed Token.
    inline std::vector<Token> CollectAuthTokens(const Parameters& params)
    {
        std::vector<Token> tokens;
        for (const auto& kvp : params) {
            if (kvp.type != ParameterType::kAuthorizationToken) {
                continue;
            }
            Token token{};
            BytesSpan span{ kvp.value };
            span >> token;
            tokens.push_back(std::move(token));
        }
        return tokens;
    }

    /// Resolve the FORWARD parameter (0x10). Default 1 (true). Throws on a value outside {0,1}.
    inline bool ResolveForward(const Parameters& params, bool default_value)
    {
        if (!params.Contains(ParameterType::kForward)) {
            return default_value;
        }
        const auto value = params.Get<std::uint8_t>(ParameterType::kForward);
        if (value > 1) {
            throw ProtocolViolationException("FORWARD parameter must be 0 or 1");
        }
        return value == 1;
    }

    /// Resolve the GROUP_ORDER parameter (0x22). Throws on a value outside {1,2}.
    inline std::optional<GroupOrder> ResolveGroupOrder(const Parameters& params)
    {
        if (!params.Contains(ParameterType::kGroupOrder)) {
            return std::nullopt;
        }
        const auto value = params.Get<std::uint8_t>(ParameterType::kGroupOrder);
        if (value != static_cast<std::uint8_t>(GroupOrder::kAscending) &&
            value != static_cast<std::uint8_t>(GroupOrder::kDescending)) {
            throw ProtocolViolationException("GROUP_ORDER parameter must be Ascending or Descending");
        }
        return static_cast<GroupOrder>(value);
    }

    /// Resolve the EXPIRES parameter (0x08). Absent or 0 both mean "no expiry" (nullopt).
    inline std::optional<std::uint64_t> ResolveExpires(const Parameters& params)
    {
        const auto expires = params.GetOptional<std::uint64_t>(ParameterType::kExpires);
        return (expires.has_value() && expires.value() != 0) ? expires : std::nullopt;
    }

    /// The §10.2 parameters common to both standalone and joining FETCH messages.
    struct FetchParameters
    {
        std::vector<Token> auth_tokens;
        std::optional<std::uint64_t> fill_timeout;
        std::uint8_t subscriber_priority;
        GroupOrder group_order;
    };

    /// Validate and resolve the parameters carried by a FETCH message, applying the
    /// §10.2 defaults (subscriber priority 128, group order Ascending).
    inline FetchParameters ResolveFetchParameters(const Parameters& params)
    {
        ValidateParameters(params,
                           { ParameterType::kAuthorizationToken,
                             ParameterType::kFillTimeout,
                             ParameterType::kSubscriberPriority,
                             ParameterType::kGroupOrder });

        return FetchParameters{
            .auth_tokens = CollectAuthTokens(params),
            .fill_timeout = params.GetOptional<std::uint64_t>(ParameterType::kFillTimeout),
            .subscriber_priority = params.GetOptional<std::uint8_t>(ParameterType::kSubscriberPriority).value_or(128),
            .group_order = ResolveGroupOrder(params).value_or(GroupOrder::kAscending),
        };
    }

    /// The SUBSCRIPTION_FILTER variants; at most one may appear in a message.
    inline constexpr FilterType kFilterTypes[] = {
        FilterType::kLocationFilter, FilterType::kSubgroupFilter, FilterType::kObjectFilter,
        FilterType::kPriorityFilter, FilterType::kPropertyFilter, FilterType::kTrackFilter,
    };

    /// True if any SUBSCRIPTION_FILTER variant is present.
    inline bool ContainsAnyFilter(const Parameters& params)
    {
        return std::any_of(std::begin(kFilterTypes), std::end(kFilterTypes), [&](const auto filter_type) {
            return params.Contains(ToParameterFilterType(filter_type));
        });
    }

    /// Resolve whichever SUBSCRIPTION_FILTER parameter is present, else monostate (unfiltered).
    inline Filter ResolveFilter(const Parameters& params)
    {
        for (const auto filter_type : kFilterTypes) {
            if (params.Contains(ToParameterFilterType(filter_type))) {
                return params.GetFilter(filter_type);
            }
        }
        return std::monostate{};
    }

} // namespace quicr::messages::control
