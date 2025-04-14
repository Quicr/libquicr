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
        constexpr std::uint16_t ToBigEndian(const std::uint16_t value)
        {
            if constexpr (std::endian::native == std::endian::big)
                return value;

            return ((value >> 8) & 0x00FF) | ((value << 8) & 0xFF00);
        }

        constexpr std::uint32_t ToBigEndian(const std::uint32_t value)
        {
            if constexpr (std::endian::native == std::endian::big)
                return value;

            return ((value >> 24) & 0x000000FF) | ((value >> 8) & 0x0000FF00) | ((value << 8) & 0x00FF0000) |
                   ((value << 24) & 0xFF000000);
        }

        constexpr std::uint64_t ToBigEndian(const std::uint64_t value)
        {
            if constexpr (std::endian::native == std::endian::big)
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
          : be_value_{ std::bit_cast<std::array<std::uint8_t, sizeof(std::uint64_t)>>(ToBigEndian(value)) }
        {
            constexpr uint64_t k14bitLength = (static_cast<uint64_t>(-1) << (64 - 6) >> (64 - 6));
            constexpr uint64_t k30bitLength = (static_cast<uint64_t>(-1) << (64 - 14) >> (64 - 14));
            constexpr uint64_t k62bitLength = (static_cast<uint64_t>(-1) << (64 - 30) >> (64 - 30));

            if (be_value_.front() & 0xC0u) { // Check if invalid
                throw std::invalid_argument("Value greater than uintvar maximum");
            }

            std::uint64_t be_v = std::bit_cast<std::uint64_t>(be_value_);
            if (value > k62bitLength) { // 62 bit encoding (8 bytes)
                be_v |= 0xC0ull;
            } else if (value > k30bitLength) { // 30 bit encoding (4 bytes)
                be_v >>= 32;
                be_v |= 0x80ull;
            } else if (value > k14bitLength) { // 14 bit encoding (2 bytes)
                be_v >>= 48;
                be_v |= 0x40ull;
            } else {
                be_v >>= 56;
            }

            be_value_ = std::bit_cast<std::array<std::uint8_t, sizeof(std::uint64_t)>>(be_v);
        }

        constexpr UintVar(std::span<const std::uint8_t> bytes)
          : be_value_{ 0 }
        {
            if (bytes.empty() || bytes.size() < Size(bytes.front())) {
                throw std::invalid_argument("Invalid bytes for uintvar");
            }

            const std::size_t size = Size(bytes.front());
            if (std::is_constant_evaluated()) {
                for (std::size_t i = 0; i < size; ++i) {
                    be_value_[i] = bytes.data()[i];
                }
            } else {
                std::memcpy(&be_value_, bytes.data(), size);
            }
        }

        constexpr UintVar(const UintVar&) noexcept = default;
        constexpr UintVar(UintVar&&) noexcept = default;
        constexpr UintVar& operator=(const UintVar&) noexcept = default;
        constexpr UintVar& operator=(UintVar&&) noexcept = default;

        constexpr UintVar& operator=(std::uint64_t value)
        {
            UintVar t(value);
            this->be_value_ = t.be_value_;
            return *this;
        }

        constexpr std::uint64_t Get() const noexcept
        {
            return ToBigEndian((std::bit_cast<std::uint64_t>(be_value_) & ToBigEndian(uint64_t(~(~0x3Full << 56))))
                               << (sizeof(uint64_t) - Size()) * 8);
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

        constexpr std::size_t Size() const noexcept { return UintVar::Size(be_value_.front()); }

        // NOLINTBEGIN(readability-identifier-naming)
        constexpr const std::uint8_t* data() const noexcept { return be_value_.data(); }
        constexpr std::size_t size() const noexcept { return Size(); }
        constexpr auto begin() const noexcept { return data(); }
        constexpr auto end() const noexcept { return data() + Size(); }
        // NOLINTEND(readability-identifier-naming)

        explicit constexpr operator uint64_t() const noexcept { return Get(); }

        constexpr auto operator<=>(const UintVar&) const noexcept = default;

      private:
        std::array<std::uint8_t, sizeof(std::uint64_t)> be_value_;
    };
}
