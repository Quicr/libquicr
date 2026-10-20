// SPDX-FileCopyrightText: Copyright (c) 2024 Cisco Systems
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

#include <cstdint>
#include <map>
#include <optional>

namespace quicr {
    /**
     * Extension values grouped by absolute extension type.
     *
     * Multiple values preserve duplicate wire entries for the same type.
     * Even-numbered types contain the host-memory byte representation of a
     * decoded unsigned integer; odd-numbered types contain opaque bytes.
     */
    using Extensions = std::map<uint64_t, std::vector<std::vector<uint8_t>>>;

    /**
     * @brief Track mode object of object published or received
     *
     * @details QUIC stream handling mode used to send objects or how object was received
     */
    enum class TrackMode : uint8_t
    {
        kDatagram, ///< Object is or should be carried in a datagram.
        kStream,   ///< Object is or should be carried on a stream.
    };

    /**
     * @brief Status of object as reported by the publisher
     */
    enum struct ObjectStatus : uint8_t
    {
        kAvailable = 0x0,    ///< The object exists and has payload data.
        kDoesNotExist = 0x1, ///< The requested object does not exist.
        kEndOfGroup = 0x3,   ///< Marks the end of the group.
        kEndOfTrack = 0x4,   ///< Marks the end of the track.
        kEndOfSubGroup = 0x5 ///< Marks the end of the subgroup.
    };

    /**
     * @brief Object headers struct
     *
     * @details Object headers are passed when sending and receiving an object. The object headers describe the object.
     */
    struct ObjectHeaders
    {
        uint64_t group_id;                               ///< Object group ID - Application defined order of generation
        uint64_t object_id;                              ///< Object ID - Application defined order of generation
        uint64_t subgroup_id{ 0 };                       ///< Subgroup ID - Starts at 0, monotonically increases by 1
        uint64_t payload_length;                         ///< Length of payload of the object data
        ObjectStatus status{ ObjectStatus::kAvailable }; ///< Status of the object at the publisher
        std::optional<uint8_t> priority;                 ///< Priority of the object, lower value is better
        std::optional<uint16_t> ttl;                     ///< Object time to live in milliseconds
        std::optional<TrackMode> track_mode;  ///< Track Mode of how the object was received or mode to use when sending
        std::optional<Extensions> extensions; ///< Mutable object extensions, preserving duplicate values per type.
        std::optional<Extensions>
          immutable_extensions; ///< Immutable object extensions decoded from the immutable extension container.
    };

}
// namespace moq
