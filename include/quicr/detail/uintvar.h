// SPDX-FileCopyrightText: Copyright (c) 2024 Cisco Systems
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

#include <bit>
#include <cstdint>
#include <cstring>
#include <span>
#include <stdexcept>
#include <vector>

namespace quicr {
    namespace {
        constexpr bool kIsBigEndian = std::endian::native == std::endian::big;

        constexpr std::uint16_t SwapBytes(const std::uint16_t value)
        {
            if constexpr (kIsBigEndian)
                return value;

            return ((value >> 8) & 0x00FF) | ((value << 8) & 0xFF00);
        }

        constexpr std::uint32_t SwapBytes(const std::uint32_t value)
        {
            if constexpr (kIsBigEndian)
                return value;

            return ((value >> 24) & 0x000000FF) | ((value >> 8) & 0x0000FF00) | ((value << 8) & 0x00FF0000) |
                   ((value << 24) & 0xFF000000);
        }

        constexpr std::uint64_t SwapBytes(const std::uint64_t value)
        {
            if constexpr (kIsBigEndian)
                return value;

            return ((value >> 56) & 0x00000000000000FF) | ((value >> 40) & 0x000000000000FF00) |
                   ((value >> 24) & 0x0000000000FF0000) | ((value >> 8) & 0x00000000FF000000) |
                   ((value << 8) & 0x000000FF00000000) | ((value << 24) & 0x0000FF0000000000) |
                   ((value << 40) & 0x00FF000000000000) | ((value << 56) & 0xFF00000000000000);
        }
    }

    class UintVar
    {
      public:
        constexpr UintVar(uint64_t value)
          : be_value_{ SwapBytes(value) }
        {
            constexpr uint64_t kLen1 = (static_cast<uint64_t>(-1) << (64 - 6) >> (64 - 6));
            constexpr uint64_t kLen2 = (static_cast<uint64_t>(-1) << (64 - 14) >> (64 - 14));
            constexpr uint64_t kLen4 = (static_cast<uint64_t>(-1) << (64 - 30) >> (64 - 30));

            if (static_cast<uint8_t>(be_value_) & 0xC0u) { // Check if invalid
                throw std::invalid_argument("Value greater than uintvar maximum");
            }

            if (value > kLen4) { // 62 bit encoding (8 bytes)
                be_value_ |= 0xC0ull;
            } else if (value > kLen2) { // 30 bit encoding (4 bytes)
                be_value_ >>= 32;
                be_value_ |= 0x80ull;
            } else if (value > kLen1) { // 14 bit encoding (2 bytes)
                be_value_ >>= 48;
                be_value_ |= 0x40ull;
            } else {
                be_value_ >>= 56;
            }
        }

        UintVar(std::span<const uint8_t> bytes)
          : be_value_{ 0 }
        {
            if (bytes.empty() || bytes.size() < Size(bytes[0])) {
                throw std::invalid_argument("Invalid bytes for uintvar");
            }

            std::memcpy(&be_value_, bytes.data(), Size(bytes[0]));
        }

        explicit constexpr operator uint64_t() const noexcept
        {
            return SwapBytes((be_value_ & SwapBytes(uint64_t(~(~0x3Full << 56)))) << (sizeof(uint64_t) - Size()) * 8);
        }

        static constexpr std::size_t Size(uint8_t msb_bytes) noexcept
        {
            if ((msb_bytes & 0xC0) == 0xC0) {
                return sizeof(uint64_t);
            } else if ((msb_bytes & 0x80) == 0x80) {
                return sizeof(uint32_t);
            } else if ((msb_bytes & 0x40) == 0x40) {
                return sizeof(uint16_t);
            }

            return sizeof(uint8_t);
        }

        constexpr std::size_t Size() const noexcept { return UintVar::Size(static_cast<uint8_t>(be_value_)); }

        // NOLINTBEGIN(readability-identifier-naming)
        auto data() const noexcept { return reinterpret_cast<const uint8_t*>(&be_value_); }
        constexpr std::size_t size() const noexcept { return Size(); }
        auto begin() const noexcept { return data(); }
        auto end() const noexcept { return data() + Size(); }
        // NOLINTEND(readability-identifier-naming)

      private:
        uint64_t be_value_;
    };
}
