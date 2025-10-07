// SPDX-FileCopyrightText: Copyright (c) 2024 Cisco Systems
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

#include "hash.h"

#include <algorithm>
#include <cstdint>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <tuple>
#include <vector>

namespace quicr {
    /**
     * @brief An N-tuple representation of a MOQ namespace.
     */
    class TrackNamespace
    {
      public:
        TrackNamespace() = default;

        /**
         * @brief Constructs a namespace from a variadic number of Bytes parameters.
         *
         * @tparam B MUST be std::vector<uint8_t>
         * @param entries The variadic amount of Bytes arguments.
         */
        template<typename... B,
                 std::enable_if_t<std::is_same_v<std::common_type_t<B...>, std::vector<uint8_t>>, bool> = true>
        explicit TrackNamespace(B&&... entries)
        {
            static_assert(sizeof...(B) >= 1, "Track namespace must have at least 1 entry");
            static_assert(sizeof...(B) <= 32 - 1, "Track namespace can only have a maximum of 32 entries");

            std::size_t offset = 0;
            const auto add_entry = [&](auto&& e) {
                const auto& entry = entries_.emplace_back(std::span{ bytes_ }.subspan(offset, e.size()));
                hashes_.emplace_back(quicr::hash({ entry.begin(), entry.end() }));

                offset += e.size();
            };

            (bytes_.insert(bytes_.end(), entries.begin(), entries.end()), ...);
            (add_entry(entries), ...);
        }

        /**
         * @brief Constructs a namespace from a variadic number of string parameters.
         *
         * @tparam S MUST be std::string
         * @param entries The variadic amount of string arguments.
         */
        template<typename... S, std::enable_if_t<std::is_same_v<std::common_type_t<S...>, std::string>, bool> = true>
        explicit TrackNamespace(S&&... entries)
        {
            static_assert(sizeof...(S) >= 1, "Track namespace must have at least 1 entry");
            static_assert(sizeof...(S) <= 32 - 1, "Track namespace can only have a maximum of 32 entries");

            std::size_t offset = 0;
            const auto add_entry = [&](auto&& e) {
                const auto& entry = entries_.emplace_back(std::span{ bytes_ }.subspan(offset, e.size()));
                hashes_.emplace_back(quicr::hash({ entry.begin(), entry.end() }));
                offset += e.size();
            };

            (bytes_.insert(bytes_.end(), entries.begin(), entries.end()), ...);
            (add_entry(entries), ...);
        }

        TrackNamespace(const std::vector<std::vector<uint8_t>>& entries)
          : entries_(entries.size())
        {
            if (entries.size() > 32 || entries.size() == 0) {
                throw std::invalid_argument("TrackNamespace requires a number of entries in the range of [1, 32]");
            }

            for (auto& entry : entries) {
                bytes_.insert(bytes_.end(), entry.begin(), entry.end());
            }

            std::size_t offset = 0;
            std::size_t i = 0;
            for (auto& entry : entries) {
                entries_[i] = std::span{ bytes_ }.subspan(offset, entry.size());
                hashes_.emplace_back(quicr::hash(entry));
                offset += entry.size();
                ++i;
            }
        }

        TrackNamespace(const std::vector<std::string>& entries)
          : entries_(entries.size())
        {
            if (entries.size() > 32 || entries.size() == 0) {
                throw std::invalid_argument("TrackNamespace requires a number of entries in the range of [1, 32]");
            }

            for (auto& entry : entries) {
                bytes_.insert(bytes_.end(), entry.begin(), entry.end());
            }

            std::size_t offset = 0;
            std::size_t i = 0;
            for (auto& entry : entries) {
                entries_[i] = std::span{ bytes_ }.subspan(offset, entry.size());
                hashes_.emplace_back(quicr::hash(entries_[i]));
                offset += entry.size();
                ++i;
            }
        }

        TrackNamespace(const TrackNamespace& other)
          : bytes_(other.bytes_)
          , entries_(other.entries_)
          , hashes_(other.hashes_)
        {
            std::size_t offset = 0;
            std::size_t i = 0;
            for (auto& entry : entries_) {
                entries_[i++] = std::span{ bytes_ }.subspan(offset, entry.size());
                offset += entry.size();
            }
        }

        TrackNamespace(TrackNamespace&& other)
          : bytes_(std::move(other.bytes_))
          , entries_(std::move(other.entries_))
          , hashes_(std::move(other.hashes_))
        {
            other.entries_.clear();

            std::size_t offset = 0;
            std::size_t i = 0;
            for (auto& entry : entries_) {
                entries_[i++] = std::span{ bytes_ }.subspan(offset, entry.size());
                offset += entry.size();
            }
        }

        TrackNamespace& operator=(const TrackNamespace& other)
        {
            this->bytes_ = other.bytes_;
            this->entries_ = other.entries_;
            this->hashes_ = other.hashes_;

            std::size_t offset = 0;
            std::size_t i = 0;
            for (auto& entry : entries_) {
                entries_[i++] = std::span{ bytes_ }.subspan(offset, entry.size());
                offset += entry.size();
            }

            return *this;
        }

        TrackNamespace& operator=(TrackNamespace&& other)
        {
            this->bytes_ = std::move(other.bytes_);
            this->entries_ = std::move(other.entries_);
            this->hashes_ = std::move(other.hashes_);

            std::size_t offset = 0;
            std::size_t i = 0;
            for (auto& entry : entries_) {
                entries_[i++] = std::span{ bytes_ }.subspan(offset, entry.size());
                offset += entry.size();
            }

            return *this;
        }

        const std::vector<std::span<const uint8_t>>& GetEntries() const noexcept { return entries_; }
        const auto& GetHashes() const noexcept { return hashes_; }

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

        bool IsPrefixOf(const TrackNamespace& other) const noexcept
        {
            if (this->hashes_.size() > other.hashes_.size()) {
                return false;
            }

            for (size_t i = 0; i < this->hashes_.size(); i++) {
                if (this->hashes_[i] != other.hashes_[i]) {
                    return false;
                }
            }

            return true;
        }

        bool HasSamePrefix(const TrackNamespace& other) const noexcept
        {
            const std::size_t prefix_size = std::min(this->hashes_.size(), other.hashes_.size());
            for (size_t i = 0; i < prefix_size; i++) {
                if (this->hashes_[i] != other.hashes_[i]) {
                    return false;
                }
            }

            return true;
        }

      private:
        std::vector<uint8_t> bytes_;
        std::vector<std::span<const uint8_t>> entries_;
        std::vector<std::size_t> hashes_;
    };
}

template<>
struct std::hash<quicr::TrackNamespace>
{
    constexpr std::uint64_t operator()(const quicr::TrackNamespace& value) const { return quicr::hash(value); }
};

namespace quicr {

    using TrackNamespaceHash = uint64_t;
    using TrackNameHash = uint64_t;
    using TrackFullNameHash = uint64_t;

    /**
     * @brief Full track name struct
     *
     * @details Struct of the full track name, which includes the namespace tuple, name, and track alias
     */
    struct FullTrackName
    {
        TrackNamespace name_space;
        std::vector<uint8_t> name;
    };
}

template<>
struct std::hash<quicr::FullTrackName>
{
    constexpr std::uint64_t operator()(const quicr::FullTrackName& ftn) const
    {
        std::uint64_t h = 0;
        quicr::hash_combine(h, std::hash<quicr::TrackNamespace>{}(ftn.name_space));
        quicr::hash_combine(h, std::hash<std::span<const std::uint8_t>>{}(ftn.name));

        // TODO(tievens): Evaluate; change hash to be more than 62 bits to avoid collisions
        return (h << 2) >> 2;
    }
};

namespace quicr {

    struct TrackHash
    {
        TrackNamespaceHash track_namespace_hash = 0; // 64bit hash of namespace
        TrackNameHash track_name_hash = 0;           // 64bit hash of name

        uint64_t track_fullname_hash = 0; // 62bit of namespace+name

        TrackHash(const uint64_t name_space, const uint64_t name) noexcept
          : track_namespace_hash{ name_space }
          , track_name_hash{ name }
        {
            hash_combine(track_fullname_hash, track_namespace_hash);
            hash_combine(track_fullname_hash, track_name_hash);

            // TODO(tievens): Evaluate; change hash to be more than 62 bits to avoid collisions
            track_fullname_hash = (track_fullname_hash << 2) >> 2;
        }

        TrackHash(const FullTrackName& ftn) noexcept
          : track_namespace_hash{ hash(ftn.name_space) }
          , track_name_hash{ hash(ftn.name) }
          , track_fullname_hash(std::hash<FullTrackName>{}(ftn))
        {
        }

        TrackHash(const TrackHash&) = default;
        TrackHash& operator=(const TrackHash&) = default;
    };
}
