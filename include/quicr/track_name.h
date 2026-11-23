// SPDX-FileCopyrightText: Copyright (c) 2024 Cisco Systems
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

#include "quicr/utilities/hash.h"

#include <algorithm>
#include <cstdint>
#include <optional>
#include <span>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace quicr {
    /**
     * @brief An N-tuple representation of a MOQ namespace.
     */
    class TrackNamespace
    {
      public:
        /// @brief Construct an empty namespace.
        TrackNamespace() = default;

        /**
         * @brief Constructs a namespace from a variadic number of Bytes parameters.
         *
         * @tparam B Byte-vector argument types; their common type must be `std::vector<uint8_t>`.
         * @param entries Namespace tuple entries in order.
         * @pre Between 1 and 31 entries must be supplied.
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
         * @tparam S String argument types; their common type must be `std::string`.
         * @param entries Namespace tuple entries in order.
         * @pre Between 1 and 31 entries must be supplied.
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

        /**
         * @brief Construct a namespace by copying byte-span entries.
         * @param entries Namespace tuple entries in order.
         * @throws std::invalid_argument if more than 32 entries are supplied.
         */
        TrackNamespace(std::span<const std::span<const uint8_t>> entries)
          : entries_(entries.size())
        {
            if (entries.size() > 32) {
                throw std::invalid_argument("TrackNamespace requires a number of entries in the range of [0, 32]");
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

        /**
         * @brief Construct a namespace by copying byte-vector entries.
         * @param entries Namespace tuple entries in order.
         * @throws std::invalid_argument if more than 32 entries are supplied.
         */
        TrackNamespace(const std::vector<std::vector<uint8_t>>& entries)
          : entries_(entries.size())
        {
            if (entries.size() > 32) {
                throw std::invalid_argument("TrackNamespace requires a number of entries in the range of [0, 32]");
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

        /**
         * @brief Construct a namespace by copying string entries as bytes.
         * @param entries Namespace tuple entries in order.
         * @throws std::invalid_argument if more than 32 entries are supplied.
         */
        TrackNamespace(const std::vector<std::string>& entries)
          : entries_(entries.size())
        {
            if (entries.size() > 32) {
                throw std::invalid_argument("TrackNamespace requires a number of entries in the range of [0, 32]");
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

        /// @brief Copy a namespace and rebind its entry views to the new object's storage.
        /// @param other Namespace to copy.
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

        /// @brief Move a namespace and rebind its entry views to the moved storage.
        /// @param other Namespace to move; its entry list is cleared.
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

        /// @brief Copy-assign a namespace and rebind its entry views.
        /// @param other Namespace to copy.
        /// @return This namespace.
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

        /// @brief Move-assign a namespace and rebind its entry views.
        /// @param other Namespace to move.
        /// @return This namespace.
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

        /**
         * @brief Access the namespace tuple entries.
         * @return Views into storage owned by this namespace; invalidated by assignment or destruction.
         */
        const std::vector<std::span<const uint8_t>>& GetEntries() const noexcept { return entries_; }

        /// @brief Return the per-entry hashes in tuple order.
        /// @return Reference valid until this namespace is assigned or destroyed.
        const auto& GetHashes() const noexcept { return hashes_; }

        /**
         * @brief Render entries as slash-separated text.
         * @return A string containing the raw entry bytes separated by `/`.
         *
         * @note The result is for diagnostics; arbitrary entry bytes need not be valid text and embedded slash
         *      bytes are not escaped.
         */
        auto Str() const noexcept
        {
            std::stringstream out;
            bool add_delimiter{ false };
            for (auto& entry : entries_) {
                if (add_delimiter) {
                    out << '/';
                }

                out << std::string_view(reinterpret_cast<const char*>(entry.data()), entry.size());

                add_delimiter = true;
            }

            return out.str();
        }

        /**
         * @brief Copy the first @p length tuple entries.
         * @param length Number of leading entries to include.
         * @return The requested prefix, or an empty namespace when @p length is zero.
         * @throws std::invalid_argument if @p length exceeds the entry count.
         */
        TrackNamespace GetPrefix(std::size_t length) const
        {
            if (length > entries_.size()) {
                throw std::invalid_argument("Prefix cannot be larger than the namespace");
            }

            if (length == 0) {
                return TrackNamespace();
            }

            return TrackNamespace(std::span{ entries_ }.subspan(0, length));
        }

        /**
         * @brief Copy the last @p length tuple entries.
         * @param length Number of trailing entries to include.
         * @return The requested suffix, or an empty namespace when @p length is zero.
         * @throws std::invalid_argument if @p length exceeds the entry count.
         */
        TrackNamespace GetSuffix(std::size_t length) const
        {
            if (length > entries_.size()) {
                throw std::invalid_argument("Suffix cannot be larger than the namespace");
            }

            if (length == 0) {
                return TrackNamespace();
            }

            return TrackNamespace(std::span{ entries_ }.subspan(entries_.size() - length));
        }

        // NOLINTBEGIN(readability-identifier-naming)
        /// @brief Return an iterator to the first byte in the flattened namespace.
        /// @return Mutable byte iterator.
        auto begin() noexcept { return bytes_.begin(); }

        /// @brief Return an iterator past the last byte in the flattened namespace.
        /// @return Past-the-end mutable byte iterator.
        auto end() noexcept { return bytes_.end(); }

        /// @brief Return a const iterator to the first byte in the flattened namespace.
        /// @return Const byte iterator.
        auto begin() const noexcept { return bytes_.begin(); }

        /// @brief Return a const iterator past the last byte in the flattened namespace.
        /// @return Past-the-end const byte iterator.
        auto end() const noexcept { return bytes_.end(); }

        /// @brief Return the flattened namespace byte storage.
        /// @return Pointer valid until assignment or destruction; it may be null when empty.
        auto data() const noexcept { return bytes_.data(); }

        /// @brief Return the flattened namespace size in bytes.
        /// @return Number of flattened bytes.
        auto size() const noexcept { return bytes_.size(); }

        /// @brief Return whether the flattened namespace contains no bytes.
        /// @return `true` when there are no flattened bytes.
        bool empty() const noexcept { return bytes_.empty(); }
        // NOLINTEND(readability-identifier-naming)

        /// @brief Compare flattened namespace bytes for equality.
        /// @param lhs Left namespace.
        /// @param rhs Right namespace.
        /// @return `true` when the flattened bytes are equal.
        friend bool operator==(const TrackNamespace& lhs, const TrackNamespace& rhs) noexcept
        {
            return lhs.bytes_ == rhs.bytes_;
        }

        /// @brief Compare flattened namespace bytes for inequality.
        /// @param lhs Left namespace.
        /// @param rhs Right namespace.
        /// @return `true` when the flattened bytes differ.
        friend bool operator!=(const TrackNamespace& lhs, const TrackNamespace& rhs) noexcept { return !(lhs == rhs); }

        /// @brief Compare flattened namespace bytes lexicographically.
        /// @param lhs Left namespace.
        /// @param rhs Right namespace.
        /// @return `true` when @p lhs sorts before @p rhs.
        friend bool operator<(const TrackNamespace& lhs, const TrackNamespace& rhs) noexcept
        {
            return lhs.bytes_ < rhs.bytes_;
        }

        /// @brief Compare flattened namespace bytes lexicographically.
        /// @param lhs Left namespace.
        /// @param rhs Right namespace.
        /// @return `true` when @p lhs sorts after @p rhs.
        friend bool operator>(const TrackNamespace& lhs, const TrackNamespace& rhs) noexcept
        {
            return lhs.bytes_ > rhs.bytes_;
        }

        /// @brief Compare flattened namespace bytes lexicographically.
        /// @param lhs Left namespace.
        /// @param rhs Right namespace.
        /// @return `true` when @p lhs does not sort after @p rhs.
        friend bool operator<=(const TrackNamespace& lhs, const TrackNamespace& rhs) noexcept { return !(lhs > rhs); }

        /// @brief Compare flattened namespace bytes lexicographically.
        /// @param lhs Left namespace.
        /// @param rhs Right namespace.
        /// @return `true` when @p lhs does not sort before @p rhs.
        friend bool operator>=(const TrackNamespace& lhs, const TrackNamespace& rhs) noexcept { return !(lhs < rhs); }

        /**
         * @brief Compare this namespace with @p rhs by tuple-entry prefix.
         * @param rhs Namespace to compare.
         * @return `less` when this is a proper prefix, `greater` when @p rhs is a proper prefix,
         *      `equivalent` when their per-entry hashes match, and `unordered` when neither is a prefix.
         */
        std::partial_ordering IsPrefixOf(const TrackNamespace& rhs) const noexcept
        {
            if (!HasSamePrefix(rhs)) {
                return std::partial_ordering::unordered;
            }

            if (entries_.size() < rhs.entries_.size()) {
                return std::partial_ordering::less;
            }

            if (entries_.size() > rhs.entries_.size()) {
                return std::partial_ordering::greater;
            }

            return std::partial_ordering::equivalent;
        }

        /**
         * @brief Test whether all per-entry hashes in the shorter namespace match the longer namespace's prefix.
         * @param other Namespace to compare.
         * @return `true` when either namespace is a prefix of the other.
         */
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

/// @brief Standard-library hash specialization for `quicr::TrackNamespace`.
template<>
struct std::hash<quicr::TrackNamespace>
{
    /// @brief Hash a namespace's flattened bytes.
    /// @param value Namespace to hash.
    /// @return Hash value compatible with `TrackNamespace::operator==`.
    constexpr std::uint64_t operator()(const quicr::TrackNamespace& value) const { return quicr::hash(value); }
};

namespace quicr {
    /**
     * @brief Full track name struct
     *
     * @details Struct of the full track name, which includes the namespace tuple, name, and track alias
     */
    struct FullTrackName
    {
        TrackNamespace name_space; ///< Namespace tuple containing the track.

        std::vector<uint8_t> name; ///< Track name as opaque bytes.

        /// @brief View the track-name bytes as text without copying.
        /// @return View valid until @ref name is modified or this object is destroyed.
        auto NameStr() const noexcept
        {
            return std::string_view(reinterpret_cast<const char*>(name.data()), name.size());
        }

        /// @brief Render the namespace as slash-separated text.
        /// @return Newly allocated diagnostic string.
        auto NamespaceStr() const noexcept { return name_space.Str(); }
    };
}

/// @brief Standard-library hash specialization for `quicr::FullTrackName`.
template<>
struct std::hash<quicr::FullTrackName>
{
    /// @brief Hash both the namespace and track-name bytes.
    /// @param ftn Full track name to hash.
    /// @return Combined hash value.
    constexpr std::uint64_t operator()(const quicr::FullTrackName& ftn) const
    {
        std::uint64_t h = 0;
        quicr::hash_combine(h, std::hash<quicr::TrackNamespace>{}(ftn.name_space));
        quicr::hash_combine(h, std::hash<std::span<const std::uint8_t>>{}(ftn.name));

        return h;
    }
};

namespace quicr {

    /// @brief Cached hashes for a full track name and its components.
    struct TrackHash
    {
        std::uint64_t track_namespace_hash = 0; ///< Hash of the namespace bytes.
        std::uint64_t track_name_hash = 0;      ///< Hash of the track-name bytes.

        uint64_t track_fullname_hash = 0; ///< Combined namespace and track-name hash.

        /// @brief Construct from precomputed component hashes.
        /// @param name_space Namespace hash.
        /// @param name Track-name hash.
        TrackHash(const uint64_t name_space, const uint64_t name) noexcept
          : track_namespace_hash{ name_space }
          , track_name_hash{ name }
        {
            hash_combine(track_fullname_hash, track_namespace_hash);
            hash_combine(track_fullname_hash, track_name_hash);
        }

        /// @brief Compute component and combined hashes for a full track name.
        /// @param ftn Full track name to hash.
        TrackHash(const FullTrackName& ftn) noexcept
          : track_namespace_hash{ hash(ftn.name_space) }
          , track_name_hash{ hash(ftn.name) }
          , track_fullname_hash(std::hash<FullTrackName>{}(ftn))
        {
        }

        /// @brief Copy cached track hashes.
        /// @param other Hash object to copy.
        TrackHash(const TrackHash& other) = default;

        /// @brief Copy-assign cached track hashes.
        /// @param other Hash object to copy.
        /// @return This hash object.
        TrackHash& operator=(const TrackHash& other) = default;
    };
}
