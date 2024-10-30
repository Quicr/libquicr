// SPDX-FileCopyrightText: Copyright (c) 2024 Cisco Systems
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

#include <cstdint>
#include <map>
#include <optional>
#include <quicr/detail/base_track_handler.h>

namespace quicr {
    using Extensions = std::map<uint64_t, std::vector<uint8_t>>;

    /**
     * @brief Status of object as reported by the publisher
     */
    enum struct ObjectStatus : uint8_t
    {
        kAvailable = 0x0,
        kDoesNotExist = 0x1,
        kEndOfGroup = 0x3,
        kEndOfTrack = 0x4,
        kEndOfSubGroup = 0x5
    };

    /**
     * @brief Object headers struct
     *
     * @details Object headers are passed when sending and receiving an object. The object headers describe the object.
     */
    struct ObjectHeaders
    {
        uint64_t group_id;                   ///< Object group ID - Application defined order of generation
        uint64_t object_id;                  ///< Object ID - Application defined order of generation
        uint64_t subgroup_id{ 0 };           ///< Subgroup ID - Starts at 0, monotonically increases by 1
        uint64_t payload_length;             ///< Length of payload of the object data
        ObjectStatus status;                 ///< Status of the object at the publisher
        std::optional<uint8_t> priority;     ///< Priority of the object, lower value is better
        std::optional<uint16_t> ttl;         ///< Object time to live in milliseconds
        std::optional<TrackMode> track_mode; ///< Track Mode of how the object was received or mode to use when sending
        std::optional<Extensions> extensions;
    };

}
// namespace moq
