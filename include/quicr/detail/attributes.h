// SPDX-FileCopyrightText: Copyright (c) 2025 Cisco Systems
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

#include "ctrl_message_types.h"

#include <chrono>
#include <optional>

namespace quicr::messages {

    // TODO: Maybe split base attributes out from SUBSCRIBE / PUBLISH?
    // TODO: E.g priority, new_group_request_id.

    /**
     * @brief Subscribe attributes
     */
    struct SubscribeAttributes
    {
        std::uint8_t priority{ 0 };                                         ///< Subscriber priority
        std::optional<GroupOrder> group_order;                              ///< Subscriber group order
        GroupOrder publisher_default_group_order{ GroupOrder::kAscending }; ///< Publisher track default group order
        std::chrono::milliseconds delivery_timeout{ 0 };                    ///< Subscriber delivery timeout
        std::chrono::milliseconds expires{ 0 };                             ///< Subscriber expiry in ms
        Filter filter{ std::monostate{} };                                  /// Subscriber filter
        std::uint8_t forward{ 0 };                    ///< True to Resume/forward data, False to pause/stop data
        std::optional<uint64_t> new_group_request_id; ///< Indicates new group id is requested
        bool is_publisher_initiated{ false };         ///< True will not send SUBSCRIBE_OK.
        Location start_location{};                    ///< Start location of group and object
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
