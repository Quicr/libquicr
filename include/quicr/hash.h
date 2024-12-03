// SPDX-FileCopyrightText: Copyright (c) 2024 Cisco Systems
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

#include "quicr/detail/span.h"
#include <array>
#include <string_view>
#include <vector>

namespace quicr {
    /// The generated CRC-64 table.
    static const std::array<std::uint64_t, 256> crc_table = [] {
        std::array<std::uint64_t, 256> table;
        for (std::uint64_t c = 0; c < 256; ++c) {
            std::uint64_t crc = c;
            for (std::uint64_t i = 0; i < 8; ++i) {
                std::uint64_t b = (crc & 1);
                crc >>= 1;
                crc ^= (0 - b) & 0xc96c5795d7870f42ull;
            }
            table[c] = crc;
        }

        return table;
    }();

    /**
     * @brief Compute CRC-64-ECMA hash of string.
     *
     * @param bytes  Bytes to hash
     * @returns The hash of the given bytes.
     */
    static constexpr std::uint64_t hash(const Span<const uint8_t> bytes)
    {

        constexpr size_t word_len = sizeof(std::uint64_t);
        std::uint64_t crc = 0;

        if (bytes.size() <= word_len) {
            std::memcpy(&crc, bytes.data(), bytes.size());
            return crc;
        }

        const auto word_count = bytes.size() / word_len;

        /*
        for (const auto& b : bytes) {
            crc = crc_table[(crc & 0xFF) ^ b] ^ (crc >> 8);
        }*/

        auto start_it = bytes.begin();
        for (size_t i = 0; i < word_count; ++i) {
            uint64_t* word = (uint64_t*)&*start_it;

            crc = crc_table[(crc ^ *word) & 0xFF] ^ (crc >> 8);

            start_it += word_len;
        }

        for (size_t i = word_count* word_len; i < bytes.size(); i++) {
            crc = crc_table[(crc & 0xFF) ^ bytes[i]] ^ (crc >> 8);
        }

        return crc;
    }

    /**
     * @brief Compute CRC-64-ECMA hash of string.
     *
     * @param str The string to hash
     *
     * @returns The hash of the given string.
     */
    static std::uint64_t hash(const std::string_view& str)
    {
        return hash(std::vector<uint8_t>{ str.begin(), str.end() });
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
