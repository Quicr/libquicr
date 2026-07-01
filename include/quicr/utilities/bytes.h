#pragma once

#include <bit>
#include <cstdint>
#include <span>
#include <type_traits>
#include <vector>

namespace quicr {
    using Byte = uint8_t;
    using Bytes = std::vector<Byte>;
    using BytesSpan = std::span<const Byte>;

    template<class T>
        requires std::is_standard_layout_v<T>
    inline std::span<const uint8_t> AsBytes(const T& value)
    {
        return std::span{ reinterpret_cast<const std::uint8_t*>(&value), sizeof(T) };
    }

    template<>
    inline std::span<const uint8_t> AsBytes<std::string>(const std::string& value)
    {
        return std::span{ reinterpret_cast<const std::uint8_t*>(value.data()), value.size() };
    }

    template<class T>
        requires std::is_standard_layout_v<T>
    inline Bytes AsOwnedBytes(const T& value)
    {
        auto bytes = AsBytes<T>(value);
        return Bytes{ bytes.begin(), bytes.end() };
    }

    template<>
    inline Bytes AsOwnedBytes<std::string>(const std::string& value)
    {
        auto bytes = AsBytes<std::string>(value);
        return Bytes{ bytes.begin(), bytes.end() };
    }

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

    inline void AppendBytes(Bytes& buffer, BytesSpan bytes)
    {
        buffer.reserve(buffer.size() + bytes.size());
        buffer.insert(buffer.end(), bytes.begin(), bytes.end());
    }

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
}
