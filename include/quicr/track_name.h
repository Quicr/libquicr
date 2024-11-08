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
    template<class T>
    inline void hash_combine(std::size_t& seed, const T& v)
    {
        std::hash<T> hasher;
        seed ^= hasher(v) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
    }

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
                const auto& entry = entries_.emplace_back(Span{ bytes_ }.subspan(offset, e.size()));
                hash_.emplace_back(
                  std::hash<std::string_view>{}({ reinterpret_cast<const char*>(entry.data()), entry.size() }));
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
                const auto& entry = entries_.emplace_back(Span{ bytes_ }.subspan(offset, e.size()));
                hash_.emplace_back(
                  std::hash<std::string_view>{}({ reinterpret_cast<const char*>(entry.data()), entry.size() }));
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
                entries_[i] = Span{ bytes_ }.subspan(offset, entry.size());
                hash_.emplace_back(std::hash<std::string_view>{}(
                  { reinterpret_cast<const char*>(entries_[i].data()), entries_[i].size() }));
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
                entries_[i] = Span{ bytes_ }.subspan(offset, entry.size());
                hash_.emplace_back(std::hash<std::string_view>{}(
                  { reinterpret_cast<const char*>(entries_[i].data()), entries_[i].size() }));
                offset += entry.size();
                ++i;
            }
        }

        TrackNamespace(const TrackNamespace& other)
          : bytes_(other.bytes_)
          , entries_(other.entries_)
          , hash_(other.hash_)
        {
            std::size_t offset = 0;
            std::size_t i = 0;
            for (auto& entry : entries_) {
                entries_[i++] = Span{ bytes_ }.subspan(offset, entry.size());
                offset += entry.size();
            }
        }

        TrackNamespace(TrackNamespace&& other)
          : bytes_(std::move(other.bytes_))
          , entries_(std::move(other.entries_))
          , hash_(std::move(other.hash_))
        {
            other.entries_.clear();

            std::size_t offset = 0;
            std::size_t i = 0;
            for (auto& entry : entries_) {
                entries_[i++] = Span{ bytes_ }.subspan(offset, entry.size());
                offset += entry.size();
            }
        }

        TrackNamespace& operator=(const TrackNamespace& other)
        {
            this->bytes_ = other.bytes_;
            this->entries_ = other.entries_;
            this->hash_ = other.hash_;

            std::size_t offset = 0;
            std::size_t i = 0;
            for (auto& entry : entries_) {
                entries_[i++] = Span{ bytes_ }.subspan(offset, entry.size());
                offset += entry.size();
            }

            return *this;
        }

        TrackNamespace& operator=(TrackNamespace&& other)
        {
            this->bytes_ = std::move(other.bytes_);
            this->entries_ = std::move(other.entries_);
            this->hash_ = std::move(other.hash_);

            std::size_t offset = 0;
            std::size_t i = 0;
            for (auto& entry : entries_) {
                entries_[i++] = Span{ bytes_ }.subspan(offset, entry.size());
                offset += entry.size();
            }

            return *this;
        }

        const std::vector<Span<const uint8_t>>& GetEntries() const noexcept { return entries_; }
        const auto& GetHashes() const noexcept { return hash_; }

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
            if (this->size() > other.size()) {
                return false;
            }

            return this->hash() == other.hash(this->hash_.size());
        }

        bool HasSamePrefix(const TrackNamespace& other) const noexcept
        {
            const std::size_t min_size = std::min(this->hash_.size(), other.hash_.size());
            return this->hash(min_size) == other.hash(min_size);
        }

      private:
        std::size_t hash(std::size_t offset = -1) const noexcept
        {
            std::size_t value = 0;
            for (const auto& h : Span{ hash_ }.subspan(0, std::min(offset, hash_.size()))) {
                hash_combine(value, h);
            }
            return value;
        }

      private:
        std::vector<uint8_t> bytes_;
        std::vector<Span<const uint8_t>> entries_;
        std::vector<std::size_t> hash_;

        friend struct std::hash<quicr::TrackNamespace>;
    };
}

template<>
struct std::hash<quicr::TrackNamespace>
{
    std::size_t operator()(const quicr::TrackNamespace& value) const noexcept { return value.hash(); }
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

        size_t track_fullname_hash = 0; // 62bit of namespace+name

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
            hash_combine(track_fullname_hash, track_namespace_hash);
            hash_combine(track_fullname_hash, track_name_hash);
            track_fullname_hash = (track_fullname_hash << 2) >> 2;
        }
    };

}
