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
        std::uint8_t priority;  ///< Subscriber priority
        GroupOrder group_order; ///< Subscriber group order
    };

    /**
     * @brief Fetch attributes
     */
    struct FetchAttributes
    {
        std::uint8_t priority;               ///< Fetch priority
        GroupOrder group_order;              ///< Fetch group order
        StartGroup start_group;              ///< Fetch starting group in range
        StartObject start_object;            ///< Fetch starting object in group
        EndGroup end_group;                  ///< Fetch final group in range
        std::optional<EndObject> end_object; ///< Fetch final object in group
    };
}
