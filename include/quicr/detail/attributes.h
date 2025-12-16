// SPDX-FileCopyrightText: Copyright (c) 2025 Cisco Systems
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

#include "quicr/detail/ctrl_messages.h"

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
        std::uint8_t priority;                        ///< Subscriber priority
        GroupOrder group_order;                       ///< Subscriber group order
        std::chrono::milliseconds delivery_timeout;   ///< Subscriber delivery timeout
        FilterType filter_type;                       /// Subscriber filter type
        std::uint8_t forward;                         ///< True to Resume/forward data, False to pause/stop data
        std::optional<uint64_t> new_group_request_id; ///< Indicates new group id is requested
        bool is_publisher_initiated;                  ///< True will not send SUBSCRIBE_OK.
        Location start_location;                      ///< Start location of group and object
    };

    struct PublishAttributes : SubscribeAttributes
    {
        FullTrackName track_full_name;
        TrackAlias track_alias;
        bool dynamic_groups = false;
    };

    struct StandaloneFetchAttributes
    {
        std::uint8_t priority;   ///< Fetch priority
        GroupOrder group_order;  ///< Fetch group order
        Location start_location; ///< Fetch starting location in range
        Location end_location;   ///< Fetch final group and object id
    };

    struct JoiningFetchAttributes
    {
        std::uint8_t priority;        ///< Fetch priority
        GroupOrder group_order;       ///< Fetch group order
        RequestID joining_request_id; ///< Fetch joining request_id
        bool relative{ false };       ///< True indicates relative to largest, False indicates absolute
        std::uint64_t joining_start;  ///< Fetch joining start
    };
}
