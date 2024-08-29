// SPDX-FileCopyrightText: Copyright (c) 2024 Cisco Systems
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

#include <vector>

namespace qtransport {

    using UintVT = std::vector<uint8_t>;

    /**
     * @brief Get the byte size from variable length-integer
     *
     * @param uintV_msbbyte     MSB byte of the variable length integer
     *
     * @returns the size in bytes of the variable length integer
     */
    inline uint8_t UintVSize(const uint8_t uint_v_msbbyte)
    {
        if ((uint_v_msbbyte & 0xC0) == 0xC0) {
            return 8;
        } else if ((uint_v_msbbyte & 0x80) == 0x80) {
            return 4;
        } else if ((uint_v_msbbyte & 0x40) == 0x40) {
            return 2;
        } else {
            return 1;
        }
    }

    /**
     * @brief Convert uint64_t to Variable-Length Integer
     *
     * @details Encode unsigned 64bit value to shorten wrire format per RFC9000 Section 16 (Variable-Length Integer
     * Encoding)
     *
     * @param value         64bit value to convert
     *
     * @returns vector of encoded bytes or empty vector if value is invalid
     */
    inline UintVT ToUintV(uint64_t value)
    {
        static constexpr uint64_t kLen1 = (static_cast<uint64_t>(-1) << (64 - 6) >> (64 - 6));
        static constexpr uint64_t kLen2 = (static_cast<uint64_t>(-1) << (64 - 14) >> (64 - 14));
        static constexpr uint64_t kLen4 = (static_cast<uint64_t>(-1) << (64 - 30) >> (64 - 30));

        uint8_t net_bytes[8]{ 0 }; // Network order bytes
        uint8_t len{ 0 };          // Length of bytes encoded

        uint8_t* byte_value = reinterpret_cast<uint8_t*>(&value);

#if __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
        constexpr std::array<uint8_t, sizeof(uint64_t)> host_order{ 0, 1, 2, 3, 4, 5, 6, 7 };
#else
        constexpr std::array<uint8_t, sizeof(uint64_t)> kHostOrder{ 7, 6, 5, 4, 3, 2, 1, 0 };
#endif

        if (byte_value[kHostOrder[0]] & 0xC0) { // Check if invalid
            return {};
        }

        if (value > kLen4) { // 62 bit encoding (8 bytes)
            for (int i = 0; i < 8; i++) {
                net_bytes[i] = byte_value[kHostOrder[i]];
            }
            net_bytes[0] |= 0xC0;
            len = 8;
        } else if (value > kLen2) { // 30 bit encoding (4 bytes)
            for (int i = 0; i < 4; i++) {
                net_bytes[i] = byte_value[kHostOrder[i + 4]];
            }
            net_bytes[0] |= 0x80;
            len = 4;
        } else if (value > kLen1) { // 14 bit encoding (2 bytes)
            net_bytes[0] = byte_value[kHostOrder[6]] | 0x40;
            net_bytes[1] = byte_value[kHostOrder[7]];
            len = 2;
        } else {
            net_bytes[0] = byte_value[kHostOrder[7]];
            len = 1;
        }

        std::vector<uint8_t> encoded_bytes(net_bytes, net_bytes + len);
        return encoded_bytes;
    }

    /**
     * @brief Convert Variable-Length Integer to uint64_t
     *
     * @param uintV             Encoded variable-Length integer
     *
     * @returns uint64_t value of the variable length integer
     */
    inline uint64_t ToUint64(const UintVT& uint_v)
    {
        if (uint_v.empty()) {
            return 0;
        }

#if __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
        constexpr std::array<uint8_t, sizeof(uint64_t)> host_order{ 0, 1, 2, 3, 4, 5, 6, 7 };
#else
        constexpr std::array<uint8_t, sizeof(uint64_t)> kHostOrder{ 7, 6, 5, 4, 3, 2, 1, 0 };
#endif
        uint64_t value{ 0 };
        uint8_t* byte_value = reinterpret_cast<uint8_t*>(&value);

        const auto offset = 8 - uint_v.size();

        for (size_t i = 0; i < uint_v.size(); i++) {
            byte_value[kHostOrder[i + offset]] = uint_v[i];
        }

        byte_value[kHostOrder[offset]] = uint_v[0] & 0x3f; // Zero MSB length bits

        return value;
    }

} // namespace qtransport
