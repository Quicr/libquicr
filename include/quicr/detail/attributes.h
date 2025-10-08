// SPDX-FileCopyrightText: Copyright (c) 2025 Cisco Systems
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

#include "quicr/detail/ctrl_messages.h"

#include <chrono>
#include <optional>

namespace quicr::messages {
    /**
     * @brief Subscribe attributes
     */
    struct SubscribeAttributes
    {
        std::uint8_t priority;                      ///< Subscriber priority
        GroupOrder group_order;                     ///< Subscriber group order
        std::chrono::milliseconds delivery_timeout; ///< Subscriber delivery timeout
        std::uint8_t forward;                       ///< True to Resume/forward data, False to pause/stop data
        bool new_group_request{ false };            ///< Indicates new group is requested
    };

    struct PublishAttributes : SubscribeAttributes
    {
        TrackAlias track_alias;
    };

    struct StandaloneFetchAttributes
    {
        std::uint8_t priority;              ///< Fetch priority
        GroupOrder group_order;             ///< Fetch group order
        Location start_location;            ///< Fetch starting location in range
        GroupId end_group;                  ///< Fetch final group in range
        std::optional<ObjectId> end_object; ///< Fetch final object in group
    };

    struct JoiningFetchAttributes
    {
        std::uint8_t priority;        ///< Fetch priority
        GroupOrder group_order;       ///< Fetch group order
        RequestID joining_request_id; ///< Fetch joining request_id
        std::uint64_t joining_start;  ///< Fetch joining start
    };
}
