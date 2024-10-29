// SPDX-FileCopyrightText: Copyright (c) 2024 Cisco Systems
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

#include "detail/span.h"

#include <algorithm>
#include <cstdint>
#include <optional>
#include <tuple>
#include <vector>

namespace quicr {
    class TrackNamespace
    {
      public:
        TrackNamespace() = default;

        template<typename... B,
                 std::enable_if_t<std::is_same_v<std::common_type_t<B...>, std::vector<uint8_t>>, bool> = true>
        explicit TrackNamespace(B&&... entries)
        {
            static_assert(sizeof...(B) >= 1, "Track namespace must have at least 1 entry");
            static_assert(sizeof...(B) <= 32 - 1, "Track namespace can only have a maximum of 32 entries");

            std::size_t offset = 0;
            const auto add_entry = [&](auto&& e) {
                entries_.emplace_back(Span{ bytes_ }.subspan(offset, e.size()));
                offset += e.size();
            };

            (bytes_.insert(bytes_.end(), entries.begin(), entries.end()), ...);
            (add_entry(entries), ...);
        }

        TrackNamespace(const std::vector<std::vector<uint8_t>>& entries)
        {
            if (entries.size() > 32 || entries.size() == 0) {
                throw std::invalid_argument("TrackNamespace requires a number of entries in the range of [1, 32]");
            }

            for (auto& entry : entries) {
                bytes_.insert(bytes_.end(), entry.begin(), entry.end());
            }

            std::size_t offset = 0;
            for (auto& entry : entries) {
                entries_.emplace_back(Span{ bytes_ }.subspan(offset, entry.size()));
                offset += entry.size();
            }
        }

        TrackNamespace(const TrackNamespace&) = default;
        TrackNamespace(TrackNamespace&&) = default;

        TrackNamespace& operator=(const TrackNamespace&) = default;
        TrackNamespace& operator=(TrackNamespace&&) = default;

        const std::vector<Span<const uint8_t>>& GetEntries() const noexcept { return entries_; }

        // NOLINTBEGIN(readability-identifier-naming)
        auto begin() noexcept { return bytes_.begin(); }
        auto end() noexcept { return bytes_.end(); }
        auto begin() const noexcept { return bytes_.begin(); }
        auto end() const noexcept { return bytes_.end(); }
        auto data() const noexcept { return bytes_.data(); }
        auto size() const noexcept { return bytes_.size(); }
        bool empty() const noexcept { return bytes_.empty(); }
        // NOLINTEND(readability-identifier-naming)

        friend bool operator==(const TrackNamespace& lhs, const TrackNamespace& rhs) noexcept
        {
            return lhs.bytes_ == rhs.bytes_;
        }

        friend bool operator!=(const TrackNamespace& lhs, const TrackNamespace& rhs) noexcept { return !(lhs == rhs); }

        friend bool operator<(const TrackNamespace& lhs, const TrackNamespace& rhs) noexcept
        {
            return lhs.bytes_ < rhs.bytes_;
        }

        friend bool operator>(const TrackNamespace& lhs, const TrackNamespace& rhs) noexcept
        {
            return lhs.bytes_ > rhs.bytes_;
        }

        friend bool operator<=(const TrackNamespace& lhs, const TrackNamespace& rhs) noexcept { return !(lhs > rhs); }

        friend bool operator>=(const TrackNamespace& lhs, const TrackNamespace& rhs) noexcept { return !(lhs < rhs); }

      private:
        std::vector<uint8_t> bytes_;
        std::vector<Span<const uint8_t>> entries_;
    };
}

template<>
struct std::hash<quicr::TrackNamespace>
{
    std::size_t operator()(const quicr::TrackNamespace& value)
    {
        return std::hash<std::string_view>{}({ reinterpret_cast<const char*>(value.data()), value.size() });
    }
};

namespace quicr {

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
          : track_namespace_hash{ std::hash<TrackNamespace>{}(ftn.name_space) }
          , track_name_hash{ std::hash<std::string_view>{}(
              { reinterpret_cast<const char*>(ftn.name.data()), ftn.name.size() }) }
        {
            track_fullname_hash = (track_namespace_hash ^ (track_name_hash << 1)) << 1 >> 2;
        }
    };

}
