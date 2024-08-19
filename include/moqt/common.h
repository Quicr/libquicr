/*
 *  Copyright (C) 2024
 *  Cisco Systems, Inc.
 *  All Rights Reserved
 */

#pragma once

#include <transport/transport.h>
#include <string>

namespace moq::transport {

    constexpr uint64_t kMoqtVersion = 0xff000004;  ///< draft-ietf-moq-transport-04
    constexpr uint64_t kSubscribeExpires = 0; ///< Never expires
    constexpr int kReadLoopMaxPerStream = 60; ///< Support packet/frame bursts, but do not allow starving other streams

    using namespace qtransport;

    using Bytes = std::vector<uint8_t>;
    using BytesSpan = Span<uint8_t const>;

    /**
     * @brief Full track name struct
     *
     * @details Struct of the full track name, which includes the namespace tuple, name, and track alias
     *   Track alias will be set by the Transport.
     */
    struct FullTrackName {
        Span<uint8_t const> name_space;
        Span<uint8_t const> name;
        std::optional<uint64_t> track_alias;
    };

    struct TrackHash
    {
        uint64_t track_namespace_hash; // 64bit hash of namespace
        uint64_t track_name_hash;      // 64bit hash of name

        uint64_t track_fullname_hash; // 62bit of namespace+name

        TrackHash(const uint64_t name_space, const uint64_t name)
        {
            track_namespace_hash = name_space;
            track_name_hash = name;
        }

        TrackHash(const FullTrackName& ftn) noexcept
        {
            track_namespace_hash = std::hash<std::string_view>{}(
              { reinterpret_cast<const char*>(ftn.name_space.data()), ftn.name_space.size() });
            track_name_hash =
              std::hash<std::string_view>{}({ reinterpret_cast<const char*>(ftn.name.data()), ftn.name.size() });

            track_fullname_hash = (track_namespace_hash ^ (track_name_hash << 1)) << 1 >>
                                  2; // combine and convert to 62 bits for uintVar
        }
    };

    /**
     * @brief Object headers struct
     *
     * @details Object headers are passed when sending and receiving an object. The object headers describe the object.
     */
    struct ObjectHeaders
    {
        uint64_t group_id;                      ///< Object group ID - Should be derived using time in microseconds
        uint64_t object_id;                     ///< Object ID - Start at zero and increment for each object in group
        uint64_t payload_length;                ///< Length of payload of the object data
        std::optional<uint32_t> priority;       ///< Priority of the object, lower value is better
        std::optional<uint16_t> ttl;            ///< Object time to live in milliseconds

    };

} // namespace moq::transport
