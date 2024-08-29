// SPDX-FileCopyrightText: Copyright (c) 2024 Cisco Systems
// SPDX-License-Identifier: BSD-2-Clause


#pragma once

#include <cstdint>
#include <optional>
#include <map>
#include <moq/detail/base_track_handler.h>

namespace moq {
    using Extensions = std::map<uint64_t, std::vector<uint8_t>>;

    /**
     * @brief Object headers struct
     *
     * @details Object headers are passed when sending and receiving an object. The object headers describe the object.
     */
    struct ObjectHeaders
    {
        uint64_t group_id;                   ///< Object group ID - Should be derived using time in microseconds
        uint64_t object_id;                  ///< Object ID - Start at zero and increment for each object in group
        uint64_t payload_length;             ///< Length of payload of the object data
        std::optional<uint8_t> priority;     ///< Priority of the object, lower value is better
        std::optional<uint16_t> ttl;         ///< Object time to live in milliseconds
        std::optional<TrackMode> track_mode; ///< Track Mode of how the object was received or mode to use when sending
        std::optional<Extensions> extensions;
    };

}
// namespace moq
