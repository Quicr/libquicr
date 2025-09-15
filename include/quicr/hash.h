// SPDX-FileCopyrightText: Copyright (c) 2024 Cisco Systems
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

#include <cstdint>
#include <span>
#include <string_view>

namespace quicr {

    /**
     * @brief Hash a span of bytes to a 64bit number.
     * @param bytes The bytes to hash.
     * @returns The 64bit hash of the given given bytes.
     */
    static inline std::uint64_t hash(std::span<const std::uint8_t> bytes)
    {
        return std::hash<std::string_view>{}({ reinterpret_cast<const char*>(bytes.data()), bytes.size() });
    }

    /**
     * @brief Combine (aka add) hash to existing hash
     *
     * @details Adds/combines new hash to existing hash. Existing hash will
     *       be updated.
     *
     * @param[in,out]   existing_hash   Existing hash to update
     * @param[in]       add_hash        New hash to add to the existing (combine)
     */
    inline void hash_combine(uint64_t& existing_hash, const uint64_t& add_hash)
    {
        existing_hash ^= add_hash + 0x9e3779b9 + (existing_hash << 6) + (add_hash >> 2);
    }

}
