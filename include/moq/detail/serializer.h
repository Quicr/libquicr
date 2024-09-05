// SPDX-FileCopyrightText: Copyright (c) 2024 Cisco Systems
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

#include "moq/common.h"
#include "moq/detail/span.h"

#include <cstdint>
#include <type_traits>
#include <vector>

namespace moq {
    namespace {
        constexpr bool is_big_endian()
        {
#if __cplusplus >= 202002L
            return std::endian::native == std::endian::big;
#else
            return (const std::uint8_t&)0x0001 == 0x00;
#endif
        }

        constexpr std::uint16_t swap_bytes(std::uint16_t value)
        {
            if constexpr (is_big_endian())
                return value;

            return ((value >> 8) & 0x00ff) | ((value << 8) & 0xff00);
        }

        constexpr std::uint32_t swap_bytes(std::uint32_t value)
        {
            if constexpr (is_big_endian())
                return value;

            return ((value >> 24) & 0x000000ff) | ((value >> 8) & 0x0000ff00) | ((value << 8) & 0x00ff0000) |
                   ((value << 24) & 0xff000000);
        }

        constexpr std::uint64_t swap_bytes(std::uint64_t value)
        {
            if constexpr (is_big_endian())
                return value;

            return ((value >> 56) & 0x00000000000000ff) | ((value >> 40) & 0x000000000000ff00) |
                   ((value >> 24) & 0x0000000000ff0000) | ((value >> 8) & 0x00000000ff000000) |
                   ((value << 8) & 0x000000ff00000000) | ((value << 24) & 0x0000ff0000000000) |
                   ((value << 40) & 0x00ff000000000000) | ((value << 56) & 0xff00000000000000);
        }
    }

    class Serializer
    {
      public:
        Serializer() = default;
        Serializer(std::size_t reserve_size);

        BytesSpan View() const noexcept { return buffer_; }
        Bytes&& Take() noexcept { return std::move(buffer_); }

        void Push(Byte data);
        void Push(BytesSpan data);
        void PushLengthBytes(BytesSpan data);
        void Clear() noexcept { return buffer_.clear(); }

        inline Serializer& operator<<(Byte value)
        {
            Push(std::move(value));
            return *this;
        }

        template<typename T, typename std::enable_if_t<std::is_standard_layout_v<T>, bool> = true>
        inline Serializer& operator<<(T value)
        {
            if constexpr (std::is_integral_v<T>) {
                value = swap_bytes(value);
            }

            const auto length = buffer_.size();
            buffer_.resize(length + sizeof(T));
            std::memcpy(buffer_.data() + length, &value, sizeof(T));

            return *this;
        }

      private:
        Bytes buffer_;
    };
}
