#pragma once

#include <bit>
#include <cstdint>

namespace quicr {

    constexpr std::uint16_t SwapBytes(const std::uint16_t value)
    {
        if constexpr (std::endian::native == std::endian::big)
            return value;

        return ((value >> 8) & 0x00FF) | ((value << 8) & 0xFF00);
    }

    constexpr std::uint32_t SwapBytes(const std::uint32_t value)
    {
        if constexpr (std::endian::native == std::endian::big)
            return value;

        return ((value >> 24) & 0x000000FF) | ((value >> 8) & 0x0000FF00) | ((value << 8) & 0x00FF0000) |
               ((value << 24) & 0xFF000000);
    }

    constexpr std::uint64_t SwapBytes(const std::uint64_t value)
    {
        if constexpr (std::endian::native == std::endian::big)
            return value;

        return ((value >> 56) & 0x00000000000000FF) | ((value >> 40) & 0x000000000000FF00) |
               ((value >> 24) & 0x0000000000FF0000) | ((value >> 8) & 0x00000000FF000000) |
               ((value << 8) & 0x000000FF00000000) | ((value << 24) & 0x0000FF0000000000) |
               ((value << 40) & 0x00FF000000000000) | ((value << 56) & 0xFF00000000000000);
    }
}
