// SPDX-FileCopyrightText: Copyright (c) 2024 Cisco Systems
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

#include <cstdint>
#include <optional>
#include <vector>

namespace quicr {
    using TrackNamespace = std::vector<uint8_t>;
    using TrackNamespaceHash = uint64_t;
    using TrackNameHash = uint64_t;

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
        TrackNamespaceHash track_namespace_hash = 0; // 64bit hash of namespace
        TrackNameHash track_name_hash = 0;           // 64bit hash of name

        uint64_t track_fullname_hash = 0; // 62bit of namespace+name

        constexpr TrackHash(const uint64_t name_space, const uint64_t name) noexcept
          : track_namespace_hash{ name_space }
          , track_name_hash{ name }
          , track_fullname_hash{ (name_space ^ (name << 1)) << 1 >> 2 }
        {
        }

        TrackHash(const FullTrackName& ftn) noexcept
          : track_namespace_hash{ std::hash<std::string_view>{}(
              { reinterpret_cast<const char*>(ftn.name_space.data()), ftn.name_space.size() }) }
          , track_name_hash{ std::hash<std::string_view>{}(
              { reinterpret_cast<const char*>(ftn.name.data()), ftn.name.size() }) }
        {
            track_fullname_hash = (track_namespace_hash ^ (track_name_hash << 1)) << 1 >> 2;
        }
    };

}
// namespace moq
