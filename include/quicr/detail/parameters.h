// SPDX-FileCopyrightText: Copyright (c) 2026 Cisco Systems
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

#include "quicr/detail/ctrl_message_types.h"

#include <algorithm>
#include <initializer_list>
#include <vector>

namespace quicr::messages {

    /// All parameters must be valid for the message type. Duplicates are impossible by
    /// construction since ParameterList keys by type.
    inline void ValidateParameters(const Parameters& params, std::initializer_list<ParameterType> allowed)
    {
        for (const auto& [key, value] : params) {
            if (std::ranges::find(allowed, static_cast<ParameterType>(key)) == allowed.end()) {
                throw ProtocolViolationException("Parameter not valid for this message type");
            }
        }
    }

    // BROKEN: ParameterList stores parameters keyed by type in a map, so multiple
    // AUTHORIZATION_TOKEN parameters collapse into a single concatenated value and only
    // the first token is recovered here. To be fixed when the storage supports repeats.
    inline std::vector<Token> CollectAuthTokens(const Parameters& params)
    {
        std::vector<Token> tokens;
        const BytesSpan span{ params.Find(ParameterType::kAuthorizationToken) };
        if (!span.empty()) {
            Token token{};
            span >> token;
            tokens.push_back(std::move(token));
        }
        return tokens;
    }

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

    inline std::optional<std::uint64_t> ResolveExpires(const Parameters& params)
    {
        const auto expires = params.GetOptional<std::uint64_t>(ParameterType::kExpires);
        return (expires.has_value() && expires.value() != 0) ? expires : std::nullopt;
    }

    inline std::uint8_t ResolveSubscriberPriority(const Parameters& params)
    {
        return params.GetOptional<std::uint8_t>(ParameterType::kSubscriberPriority).value_or(128);
    }

    inline std::optional<std::uint64_t> ResolveRendezvousTimeout(const Parameters& params)
    {
        return params.GetOptional<std::uint64_t>(ParameterType::kRendezvousTimeout);
    }

    inline std::optional<std::uint64_t> ResolveNewGroupRequest(const Parameters& params)
    {
        return params.GetOptional<std::uint64_t>(ParameterType::kNewGroupRequest);
    }

    // Filters.
    inline constexpr FilterType kFilterTypes[] = {
        FilterType::kSubscriptionFilter, FilterType::kSubgroupFilter, FilterType::kObjectFilter,
        FilterType::kPriorityFilter,     FilterType::kPropertyFilter, FilterType::kTrackFilter,
    };
    inline bool ContainsAnyFilter(const Parameters& params)
    {
        return std::ranges::any_of(
          kFilterTypes, [&](const auto filter_type) { return params.Contains(ToParameterFilterType(filter_type)); });
    }
    inline Filter ResolveFilter(const Parameters& params)
    {
        for (const auto filter_type : kFilterTypes) {
            if (params.Contains(ToParameterFilterType(filter_type))) {
                return params.GetFilter(filter_type);
            }
        }
        return std::monostate{};
    }

} // namespace quicr::messages
