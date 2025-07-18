// SPDX-FileCopyrightText: Copyright (c) 2025 Cisco Systems
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

#include <optional>
#include <quicr/detail/ctrl_messages.h>

namespace quicr::messages {
    /**
     * @brief Subscribe attributes
     */
    struct SubscribeAttributes
    {
        std::uint8_t priority;                 ///< Subscriber priority
        GroupOrder group_order;                ///< Subscriber group order
        std::uint8_t forward;                  ///< True to Resume/forward data, False to pause/stop data
        std::optional<TrackAlias> track_alias; ///< Track alias for subscribe
    };

    /**
     * @brief Fetch attributes
     */
    struct FetchAttributes
    {
        std::uint8_t priority;              ///< Fetch priority
        GroupOrder group_order;             ///< Fetch group order
        Location start_location;            ///< Fetch starting location in range
        GroupId end_group;                  ///< Fetch final group in range
        std::optional<ObjectId> end_object; ///< Fetch final object in group
    };
}
