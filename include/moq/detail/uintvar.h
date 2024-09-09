// SPDX-FileCopyrightText: Copyright (c) 2024 Cisco Systems
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

#include <cstring>
#include <vector>

namespace moq {
    namespace {
        constexpr bool is_big_endian()
        {
#if __cplusplus >= 202002L
            return std::endian::native == std::endian::big;
#else
            return static_cast<const std::uint8_t&>(0x0001) == 0x00;
#endif
        }

        constexpr std::uint16_t SwapBytes(const std::uint16_t value)
        {
            if constexpr (is_big_endian())
                return value;

            return ((value >> 8) & 0x00FF) | ((value << 8) & 0xFF00);
        }

        constexpr std::uint32_t SwapBytes(const std::uint32_t value)
        {
            if constexpr (is_big_endian())
                return value;

            return ((value >> 24) & 0x000000FF) | ((value >> 8) & 0x0000FF00) | ((value << 8) & 0x00FF0000) |
                   ((value << 24) & 0xFF000000);
        }

        constexpr std::uint64_t SwapBytes(const std::uint64_t value)
        {
            if constexpr (is_big_endian())
                return value;

            return ((value >> 56) & 0x00000000000000FF) | ((value >> 40) & 0x000000000000FF00) |
                   ((value >> 24) & 0x0000000000FF0000) | ((value >> 8) & 0x00000000FF000000) |
                   ((value << 8) & 0x000000FF00000000) | ((value << 24) & 0x0000FF0000000000) |
                   ((value << 40) & 0x00FF000000000000) | ((value << 56) & 0xFF00000000000000);
        }

        constexpr std::uint64_t ToNetByteOrder(const std::uint64_t value)
        {
            return SwapBytes(value);
        }

        constexpr std::uint64_t ToHostByteOrder(const std::uint64_t value)
        {
            return SwapBytes(value);
        }
    }

    using UintV = std::vector<uint8_t>;

    /**
     * @brief Get the byte size from variable length-integer
     *
     * @param uint_v_msbbyte     MSB byte of the variable length integer
     *
     * @returns the size in bytes of the variable length integer
     */
    inline uint8_t SizeofUintV(const uint8_t uint_v_msbbyte)
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
    inline UintV ToUintV(uint64_t value)
    {
        constexpr uint64_t kLen1 = (static_cast<uint64_t>(-1) << (64 - 6) >> (64 - 6));
        constexpr uint64_t kLen2 = (static_cast<uint64_t>(-1) << (64 - 14) >> (64 - 14));
        constexpr uint64_t kLen4 = (static_cast<uint64_t>(-1) << (64 - 30) >> (64 - 30));

        value = ToNetByteOrder(value);

        std::vector<uint8_t> net_bytes(sizeof(uint64_t), 0);
        std::memcpy(net_bytes.data(), &value, sizeof(uint64_t));

        if (net_bytes[0] & 0xC0) { // Check if invalid
            return {};
        }

        if (value > kLen4) { // 62 bit encoding (8 bytes)
            net_bytes[0] |= 0xC0;
        } else if (value > kLen2) { // 30 bit encoding (4 bytes)
            net_bytes[0] |= 0x80;
            net_bytes.resize(4);
        } else if (value > kLen1) { // 14 bit encoding (2 bytes)
            net_bytes[0] |= 0x40;
            net_bytes.resize(2);
        } else {
            net_bytes.resize(1);
        }

        return net_bytes;
    }

    /**
     * @brief Convert Variable-Length Integer to uint64_t
     *
     * @param uintV Encoded variable-Length integer
     *
     * @returns uint64_t value of the variable length integer
     */
    inline uint64_t ToUint64(const UintV& uint_v)
    {
        if (uint_v.empty()) {
            return 0;
        }

        uint64_t value{ 0 };
        uint8_t* byte_value = reinterpret_cast<uint8_t*>(&value);
        std::memcpy(byte_value, uint_v.data(), uint_v.size());

        byte_value[0] &= 0x3f; // Zero MSB length bits

        return ToHostByteOrder(value);
    }
}
