// SPDX-FileCopyrightText: Copyright (c) 2026 Cisco Systems
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

#include <bit>
#include <cstdint>
#include <span>
#include <string>
#include <type_traits>
#include <vector>

namespace quicr {
    /** @brief One octet of binary data. */
    using Byte = uint8_t;

    /** @brief Owning contiguous byte buffer. */
    using Bytes = std::vector<Byte>;

    /** @brief Non-owning, read-only view of contiguous bytes. */
    using BytesSpan = std::span<const Byte>;

    /**
     * @brief Views a value's in-memory object representation as bytes.
     * @tparam T A standard-layout type, or `std::string`.
     * @param value Value whose storage is viewed.
     * @return A non-owning span valid until `value` is destroyed, moved, or otherwise invalidated.
     * @details For non-string values the bytes are the native object representation, including native endianness and
     * possible padding; this is not a portable wire encoding. For strings, the span covers the character data only
     * and excludes any null terminator. Mutating `value` may change the viewed bytes.
     */
    template<class T>
        requires std::is_standard_layout_v<T> || std::is_same_v<std::decay_t<T>, std::string>
    inline std::span<const uint8_t> AsBytes(const T& value)
    {
        return std::span{ reinterpret_cast<const std::uint8_t*>(&value), sizeof(T) };
    }

    /**
     * @brief Views a string's character data as bytes without copying.
     * @param value String to view.
     * @return Non-owning span excluding any null terminator, valid until the string is mutated, moved, or destroyed.
     */
    template<>
    inline std::span<const uint8_t> AsBytes<std::string>(const std::string& value)
    {
        return std::span{ reinterpret_cast<const std::uint8_t*>(value.data()), value.size() };
    }

    /**
     * @brief Copies a value's native object representation into an owning byte buffer.
     * @tparam T A standard-layout type, or `std::string`.
     * @param value Value to copy.
     * @return Owned copy of the bytes returned by `AsBytes(value)`.
     * @details Non-string values use native endianness and include any padding; this is not a portable wire encoding.
     * Strings contribute only their character bytes, without a null terminator.
     */
    template<class T>
        requires std::is_standard_layout_v<T> || std::is_same_v<std::decay_t<T>, std::string>
    inline Bytes AsOwnedBytes(const T& value)
    {
        auto bytes = AsBytes<T>(value);
        return Bytes{ bytes.begin(), bytes.end() };
    }

    /**
     * @brief Constructs a value from bytes.
     * @tparam T Destination type.
     * @param bytes Source bytes.
     * @return A default-initialized `T` when `bytes` is empty; a string containing all bytes for `std::string`;
     * otherwise a zero-initialized `T` with the supplied bytes copied into the start of its native object
     * representation.
     * @pre For non-string `T`, `bytes.size() <= sizeof(T)`.
     * @warning For non-string types this uses native object representation and endianness, is not portable
     * serialization, and may produce a value representation that is invalid for `T`.
     */
    template<class T>
    T FromBytes(std::span<const uint8_t> bytes)
    {
        if (bytes.empty()) {
            return {};
        }

        if constexpr (std::is_same_v<std::string, std::decay_t<T>>) {
            return std::string(bytes.begin(), bytes.end());
        }

        T value{};
        std::copy(bytes.begin(), bytes.end(), reinterpret_cast<std::uint8_t*>(&value));
        return value;
    }

    /**
     * @brief Appends bytes to an owning buffer.
     * @param buffer Buffer to extend.
     * @param bytes Bytes copied into `buffer`; the span must not be invalidated by `buffer` reallocation.
     * @throws Any exception thrown by `std::vector::reserve` or `std::vector::insert`.
     */
    inline void AppendBytes(Bytes& buffer, BytesSpan bytes)
    {
        buffer.reserve(buffer.size() + bytes.size());
        buffer.insert(buffer.end(), bytes.begin(), bytes.end());
    }

    /** @brief Converts a 16-bit value between native and big-endian byte order. */
    constexpr std::uint16_t SwapBytes(const std::uint16_t value)
    {
        if constexpr (std::endian::native == std::endian::big) {
            return value;
        }

        return ((value >> 8) & 0x00FF) | ((value << 8) & 0xFF00);
    }

    /** @brief Converts a 32-bit value between native and big-endian byte order. */
    constexpr std::uint32_t SwapBytes(const std::uint32_t value)
    {
        if constexpr (std::endian::native == std::endian::big) {
            return value;
        }

        return ((value >> 24) & 0x000000FF) | ((value >> 8) & 0x0000FF00) | ((value << 8) & 0x00FF0000) |
               ((value << 24) & 0xFF000000);
    }

    /** @brief Converts a 64-bit value between native and big-endian byte order. */
    constexpr std::uint64_t SwapBytes(const std::uint64_t value)
    {
        if constexpr (std::endian::native == std::endian::big) {
            return value;
        }

        return ((value >> 56) & 0x00000000000000FF) | ((value >> 40) & 0x000000000000FF00) |
               ((value >> 24) & 0x0000000000FF0000) | ((value >> 8) & 0x00000000FF000000) |
               ((value << 8) & 0x000000FF00000000) | ((value << 24) & 0x0000FF0000000000) |
               ((value << 40) & 0x00FF000000000000) | ((value << 56) & 0xFF00000000000000);
    }
}
