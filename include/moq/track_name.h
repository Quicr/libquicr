/*
 *  Copyright (C) 2024
 *  Cisco Systems, Inc.
 *  All Rights Reserved
 */

#pragma once

#include <cstdint>
#include <optional>
#include <vector>

namespace moq {
    using TrackNamespace = std::vector<uint8_t>;

    /**
     * @brief Full track name struct
     *
     * @details Struct of the full track name, which includes the namespace tuple, name, and track alias
     *   Track alias will be set by the Transport.
     */
    struct FullTrackName
    {
        const TrackNamespace name_space;
        const std::vector<uint8_t> name;
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

            track_fullname_hash =
              (track_namespace_hash ^ (track_name_hash << 1)) << 1 >> 2; // combine and convert to 62 bits for uintVar
        }
    };

}
// namespace moq