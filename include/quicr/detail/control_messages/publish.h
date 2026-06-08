// SPDX-FileCopyrightText: Copyright (c) 2026 Cisco Systems
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

#include <quicr/detail/ctrl_message_types.h>

namespace quicr::messages {

    struct Publish
    {
        static constexpr std::uint64_t kType = 0x1D;

        const FullTrackName full_track_name;
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

} // namespace quicr::messages
