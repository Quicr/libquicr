#pragma once

#include <bit>
#include <cstdint>
#include <span>
#include <vector>

namespace quicr {
    using Byte = std::uint8_t;
    using Bytes = std::vector<std::uint8_t>;
    using UnownedBytes = std::span<const Byte>;
    using UnownedMutableBytes = std::span<Byte>;
    using BytesSpan [[deprecated]] = UnownedBytes;

    constexpr std::uint16_t SwapBytes(const std::uint16_t value)
    {
        return ((value >> 8) & 0x00FF) | ((value << 8) & 0xFF00);
    }

    constexpr std::uint32_t SwapBytes(const std::uint32_t value)
    {
        return ((value >> 24) & 0x000000FF) | ((value >> 8) & 0x0000FF00) | ((value << 8) & 0x00FF0000) |
               ((value << 24) & 0xFF000000);
    }

    constexpr std::uint64_t SwapBytes(const std::uint64_t value)
    {
        return ((value >> 56) & 0x00000000000000FF) | ((value >> 40) & 0x000000000000FF00) |
               ((value >> 24) & 0x0000000000FF0000) | ((value >> 8) & 0x00000000FF000000) |
               ((value << 8) & 0x000000FF00000000) | ((value << 24) & 0x0000FF0000000000) |
               ((value << 40) & 0x00FF000000000000) | ((value << 56) & 0xFF00000000000000);
    }

    /**
     * @brief Get a span of bytes of a given value.
     * @tparam T The type of the value to get the bytes for.
     * @param value The value to get the bytes of.
     * @returns An unowned span of bytes of \p value.
     */
    template<class T, std::enable_if_t<std::is_standard_layout_v<T>, bool> = true>
    inline UnownedBytes AsBytes(const T& value)
    {
        return std::span{ reinterpret_cast<const Byte*>(&value), sizeof(T) };
    }

    /**
     * @brief String specialization of AsBytes, gets a string as a span of bytes.
     */
    template<>
    inline UnownedBytes AsBytes<std::string>(const std::string& value)
    {
        return std::span{ reinterpret_cast<const Byte*>(value.data()), value.size() };
    }

    /**
     * @brief Get a span of mutable bytes of a given value.
     * @tparam T The type of the value to get the bytes for.
     * @param value The value to get the bytes of.
     * @returns An unowned span of mutable bytes of \p value.
     *
     * @note Modifying any byte in the span will modify the corresponding byte in the value.
     */
    template<class T, std::enable_if_t<std::is_standard_layout_v<T>, bool> = true>
    inline UnownedMutableBytes AsMutableBytes(T& value)
    {
        return std::span{ reinterpret_cast<Byte*>(&value), sizeof(T) };
    }

    /**
     * @brief String specialization of \p AsMutableBytes, gets a string as a span of mutable bytes.
     */
    template<>
    inline UnownedMutableBytes AsMutableBytes<std::string>(std::string& value)
    {
        return std::span{ reinterpret_cast<Byte*>(value.data()), value.size() };
    }
}
