// SPDX-FileCopyrightText: Copyright (c) 2025 Cisco Systems
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

#include "ctrl_message_types.h"

#include <chrono>
#include <optional>
#include <vector>

namespace quicr::messages {

    // TODO: Maybe split base attributes out from SUBSCRIBE / PUBLISH?
    // TODO: E.g priority, new_group_request_id.

    /**
     * @brief Subscribe attributes carried on the wire in a SUBSCRIBE message.
     */
    struct SubscribeAttributes
    {
        const std::uint8_t priority;                             ///< Subscriber priority (128 if absent)
        const std::optional<GroupOrder> group_order;             ///< Requested order (absent => publisher default)
        const Filter filter;                                     ///< Subscription filter (monostate => unfiltered)
        const bool forward;                                      ///< Forwarding state (1 if absent)
        const std::optional<std::uint64_t> new_group_request_id; ///< NEW_GROUP_REQUEST value, if present
        const std::optional<std::uint64_t> rendezvous_timeout;   ///< RENDEZVOUS_TIMEOUT ms, if present
        const std::vector<Token> auth_tokens;                    ///< Collected AUTHORIZATION_TOKENs
        const bool is_publisher_initiated;                       ///< True => do not send SUBSCRIBE_OK
    };

    struct PublishAttributes
    {
        const FullTrackName track_full_name;
        const TrackAlias track_alias;
        const std::vector<Token> auth_tokens;
        const std::optional<std::uint64_t> expires;
        const std::optional<Location> largest_object;
        const bool forward;
        const GroupOrder default_publisher_group_order;
        const bool dynamic_groups;
        const std::uint8_t default_publisher_priority;
        const std::optional<std::uint64_t> max_cache_duration;
        const std::optional<std::uint64_t> delivery_timeout;
        const TrackExtensions track_properties;
    };

    struct FetchEndLocation
    {
        /// The group ID of the fetch's end location (inclusive).
        GroupId group{ 0 };
        /// The object ID of the fetch's end location (inclusive), or null for the whole group.
        std::optional<ObjectId> object;
    };

    struct StandaloneFetchAttributes
    {
        std::uint8_t priority{ 0 };                                         ///< Fetch priority
        std::optional<GroupOrder> group_order;                              ///< Fetch group order
        GroupOrder publisher_default_group_order{ GroupOrder::kAscending }; ///< Publisher track default group order
        Location start_location{};                                          ///< Fetch starting location in range
        FetchEndLocation end_location{};                                    ///< Fetch final location.
    };

    struct JoiningFetchAttributes
    {
        std::uint8_t priority{ 0 };                                         ///< Fetch priority
        std::optional<GroupOrder> group_order;                              ///< Fetch group order
        GroupOrder publisher_default_group_order{ GroupOrder::kAscending }; ///< Publisher track default group order
        RequestID joining_request_id{ 0 };                                  ///< Fetch joining request_id
        bool relative{ false };           ///< True indicates relative to largest, False indicates absolute
        std::uint64_t joining_start{ 0 }; ///< Fetch joining start
    };

    struct SubscribeNamespaceAttributes
    {
        uint64_t request_id{ 0 };
        FilterType filter_type{ FilterType::kTrackFilter };
        Filter filter{ std::monostate{} };
    };

}
