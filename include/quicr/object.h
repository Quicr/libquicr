// SPDX-FileCopyrightText: Copyright (c) 2024 Cisco Systems
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

#include <quicr/detail/base_track_handler.h>
#include <quicr/detail/messages.h>

#include <cstdint>
#include <map>
#include <optional>

namespace quicr {
    struct ExtensionMap : public std::map<uint64_t, Bytes>
    {
        using KeyType = std::uint64_t;
        using ValueType = Bytes;

        using base = std::map<KeyType, ValueType>;

        bool CompareExtension(const base::value_type& lhs, const base::value_type& rhs) const
        {
            const auto& [lhs_type, lhs_value] = lhs;
            const auto& [rhs_type, rhs_value] = rhs;

            if (lhs_type != rhs_type) {
                return false;
            }

            if (static_cast<std::uint64_t>(lhs_type) % 2 != 0) {
                // Odd types are byte equality.
                return lhs_value == rhs_value;
            }

            // Even types are numeric equality.
            if (lhs_value.size() > sizeof(std::uint64_t) || rhs_value.size() > sizeof(std::uint64_t)) {
                throw std::invalid_argument("Even KVPs must be <= 8 bytes");
            }

            // Compare numeric values.
            const auto smaller = std::min(lhs_value.size(), rhs_value.size());
            if (memcmp(lhs_value.data(), rhs_value.data(), smaller) != 0) {
                return false;
            }

            // Are there left over bytes to check?
            const auto larger = std::max(lhs_value.size(), rhs_value.size());
            if (larger == smaller) {
                return true;
            }

            // Any remaining bytes could be 0, but nothing else.
            const auto& longer = (lhs_value.size() > rhs_value.size()) ? lhs_value : rhs_value;
            const auto remaining = larger - smaller;
            static constexpr std::uint8_t kZero[sizeof(std::uint64_t)] = { 0 };
            return memcmp(longer.data() + smaller, kZero, remaining) == 0;
        }

      public:
        using iterator = base::iterator;

        using base::base;

        template<typename T>
        std::pair<iterator, bool> try_emplace(const KeyType& k, const T& value)
        {
            if (k % 2 == 0) {
                if constexpr (!(std::is_integral_v<T> && std::is_unsigned_v<T>)) {
                    if (value.size() > sizeof(std::uint64_t)) {
                        throw std::invalid_argument("Value too large to encode as uint64_t.");
                    }
                }

                const auto* value_ptr = reinterpret_cast<const std::uint8_t*>(&value);
                return base::try_emplace(k, value_ptr, value_ptr + sizeof(T));
            }

            auto byte_ptr = reinterpret_cast<const Byte*>(&value);
            return base::try_emplace(k, byte_ptr, byte_ptr + sizeof(T));
        }

        bool operator==(const ExtensionMap& rhs) const
        {
            return std::equal(begin(), end(), rhs.begin(), [this](const auto& lhs, const auto& rhs) {
                return CompareExtension(lhs, rhs);
            });
        }
    };

    using Extensions = ExtensionMap;

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
        std::optional<Extensions> immutable_extensions;
    };
}
// namespace moq
