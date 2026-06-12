// SPDX-FileCopyrightText: Copyright (c) 2026 Cisco Systems
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

#include "quicr/detail/ctrl_message_types.h"

#include <cstdint>
#include <optional>

namespace quicr::messages {

    inline GroupOrder ResolveDefaultPublisherGroupOrder(const TrackExtensions& properties)
    {
        const auto value = properties.GetOptional<std::uint8_t>(ExtensionType::kDefaultPublisherGroupOrder);
        if (!value.has_value()) {
            return GroupOrder::kAscending;
        }
        if (*value != static_cast<std::uint8_t>(GroupOrder::kAscending) &&
            *value != static_cast<std::uint8_t>(GroupOrder::kDescending)) {
            throw ProtocolViolationException("DEFAULT_PUBLISHER_GROUP_ORDER must be Ascending or Descending");
        }
        return static_cast<GroupOrder>(*value);
    }

    inline bool ResolveDynamicGroups(const TrackExtensions& properties)
    {
        const auto value = properties.GetOptional<std::uint8_t>(ExtensionType::kDynamicGroups);
        if (!value.has_value()) {
            return false;
        }
        if (*value > 1) {
            throw ProtocolViolationException("DYNAMIC_GROUPS must be 0 or 1");
        }
        return *value == 1;
    }

    inline std::uint8_t ResolveDefaultPublisherPriority(const TrackExtensions& properties)
    {
        return properties.GetOptional<std::uint8_t>(ExtensionType::kDefaultPublisherPriority).value_or(128);
    }

    inline std::optional<std::uint64_t> ResolveMaxCacheDuration(const TrackExtensions& properties)
    {
        return properties.GetOptional<std::uint64_t>(ExtensionType::kMaxCacheDuration);
    }

    inline std::optional<std::uint64_t> ResolveDeliveryTimeout(const TrackExtensions& properties)
    {
        return properties.GetOptional<std::uint64_t>(ExtensionType::kDeliveryTimeout);
    }

} // namespace quicr::messages
