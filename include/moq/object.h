/*
 *  Copyright (C) 2024
 *  Cisco Systems, Inc.
 *  All Rights Reserved
 */

#pragma once

#include <cstdint>
#include <optional>

namespace moq {
    /**
     * @brief Object headers struct
     *
     * @details Object headers are passed when sending and receiving an object. The object headers describe the object.
     */
    struct ObjectHeaders
    {
        uint64_t group_id;               ///< Object group ID - Should be derived using time in microseconds
        uint64_t object_id;              ///< Object ID - Start at zero and increment for each object in group
        uint64_t payload_length;         ///< Length of payload of the object data
        std::optional<uint8_t> priority; ///< Priority of the object, lower value is better
        std::optional<uint16_t> ttl;     ///< Object time to live in milliseconds
    };


}
// namespace moq
