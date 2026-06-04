// SPDX-FileCopyrightText: Copyright (c) 2024 Cisco Systems
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

#include "utilities.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cstdint>
#include <source_location>
#include <span>
#include <stdexcept>
#include <string>

namespace quicr {

    struct UintVarInvalidArgException : std::invalid_argument
    {
        const std::string reason;
        UintVarInvalidArgException(const std::string& reason,
                                   const std::source_location location = std::source_location::current())
          : std::invalid_argument("Invalid argument: " + reason + " (line " + std::to_string(location.line()) +
                                  ", file " + location.file_name() + ")")
          , reason(reason)
        {
        }
    };

    class UintVar
    {
      public:
        constexpr UintVar(uint64_t value) noexcept
          : be_value_{ 0 }
        {
            // How many bytes do we need to store this value?
            const auto bits = std::bit_width(value);
            const std::size_t length = std::min<std::size_t>(kMaxEncodedSize, bits == 0 ? 1 : (bits + 6) / 7);

            // Max is 1 byte of 1s, 8 bytes of value.
            if (length == kMaxEncodedSize) {
                const auto bytes = std::bit_cast<std::array<std::uint8_t, sizeof(value)>>(SwapBytes(value));
                std::copy(bytes.begin(), bytes.end(), std::next(be_value_.begin()));
                be_value_.front() = 0xFF;
                return;
            }

            // Otherwise length signalled by leading 1s, followed by compressed value.
            const auto value_be = SwapBytes(value << ((sizeof(value) - length) * 8));
            const auto bytes = std::bit_cast<std::array<std::uint8_t, sizeof(value)>>(value_be);
            std::copy(bytes.begin(), bytes.end(), be_value_.begin());
            const auto prefix = length == 1 ? 0u : static_cast<std::uint8_t>(0xFFu << (kMaxEncodedSize - length));
            be_value_.front() |= static_cast<std::uint8_t>(prefix);
        }

        constexpr UintVar(std::span<const std::uint8_t> bytes,
                          const std::source_location caller = std::source_location::current())
          : be_value_{ 0 }
        {
            if (bytes.empty() || bytes.size() < Size(bytes.front())) {
                throw UintVarInvalidArgException("Invalid bytes for uintvar", caller);
            }
            std::copy_n(bytes.data(), Size(bytes.front()), be_value_.begin());
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
            // Read length of value in leading 1s, then value.
            const auto length = Size();
            const auto value_bits = length == kMaxEncodedSize ? 0 : 8 - length;
            const auto mask = value_bits == 0 ? 0 : static_cast<std::uint8_t>((1u << value_bits) - 1);
            std::uint64_t value = be_value_.front() & mask;
            for (std::size_t i = 1; i < length; ++i) {
                value = (value << 8) | be_value_[i];
            }
            return value;
        }

        static constexpr std::size_t Size(uint8_t msb_bytes) noexcept
        {
            const auto leading_ones = std::countl_one(msb_bytes);
            return leading_ones == 8 ? kMaxEncodedSize : leading_ones + 1;
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
        static constexpr std::size_t kMaxEncodedSize = sizeof(std::uint64_t) + 1;
        std::array<std::uint8_t, kMaxEncodedSize> be_value_;
    };
}
