// SPDX-FileCopyrightText: Copyright (c) 2024 Cisco Systems
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

#include "quicr/utilities/bytes.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cstdint>
#include <source_location>
#include <span>
#include <stdexcept>
#include <string>

namespace quicr {

    /**
     * Reports an invalid or incomplete variable-length unsigned integer encoding.
     */
    struct UintVarInvalidArgException : std::invalid_argument
    {
        const std::string reason; ///< Caller-oriented explanation without the formatted source location.

        /**
         * Constructs an invalid-argument error.
         * @param reason Explanation of the invalid value or byte sequence.
         * @param location Source location attributed to the caller.
         */
        UintVarInvalidArgException(const std::string& reason,
                                   const std::source_location location = std::source_location::current())
          : std::invalid_argument("Invalid argument: " + reason + " (line " + std::to_string(location.line()) +
                                  ", file " + location.file_name() + ")")
          , reason(reason)
        {
        }
    };

    /**
     * MoQ variable-length encoding of an unsigned 64-bit integer.
     *
     * The encoded width is one through nine bytes. Leading one bits in the
     * first byte identify the width; `0xFF` introduces the nine-byte form.
     * Construction from an integer produces the shortest encoding, while
     * construction from bytes preserves the encoded width supplied by the
     * caller (including valid non-minimal encodings).
     */
    class UintVar
    {
      public:
        /**
         * Encodes a value using its shortest representation.
         * @param value Unsigned value to encode.
         */
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

        /**
         * Copies one encoded value from the beginning of a byte span.
         * Trailing bytes are ignored.
         * @param bytes Span beginning with an encoded value.
         * @param caller Source location used when reporting malformed input.
         * @throws UintVarInvalidArgException if the span is empty or shorter
         * than the width indicated by its first byte.
         */
        constexpr UintVar(std::span<const std::uint8_t> bytes,
                          const std::source_location caller = std::source_location::current())
          : be_value_{ 0 }
        {
            if (bytes.empty() || bytes.size() < Size(bytes.front())) {
                throw UintVarInvalidArgException("Invalid bytes for uintvar", caller);
            }
            std::copy_n(bytes.data(), Size(bytes.front()), be_value_.begin());
        }

        /// Copies an encoded value.
        /// @param other Value to copy.
        constexpr UintVar(const UintVar& other) noexcept = default;

        /// Moves an encoded value.
        /// @param other Value to move.
        constexpr UintVar(UintVar&& other) noexcept = default;

        /// Copy-assigns an encoded value.
        /// @param other Value to copy.
        /// @return This object.
        constexpr UintVar& operator=(const UintVar& other) noexcept = default;

        /// Move-assigns an encoded value.
        /// @param other Value to move.
        /// @return This object.
        constexpr UintVar& operator=(UintVar&& other) noexcept = default;

        /**
         * Replaces this encoding with the shortest encoding of a value.
         * @param value Unsigned value to encode.
         * @return This object.
         */
        constexpr UintVar& operator=(std::uint64_t value)
        {
            UintVar t(value);
            this->be_value_ = t.be_value_;
            return *this;
        }

        /// @return The decoded unsigned value.
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

        /**
         * Determines an encoded width from its first byte.
         * @param msb_bytes First byte of an encoding.
         * @return Required width in bytes, from one through nine.
         */
        static constexpr std::size_t Size(uint8_t msb_bytes) noexcept
        {
            const auto leading_ones = std::countl_one(msb_bytes);
            return leading_ones == 8 ? kMaxEncodedSize : leading_ones + 1;
        }

        /// @return Number of significant encoded bytes stored by this object.
        constexpr std::size_t Size() const noexcept { return UintVar::Size(be_value_.front()); }

        // NOLINTBEGIN(readability-identifier-naming)
        /// @return Pointer to the first encoded byte; ownership remains with this object.
        constexpr const std::uint8_t* data() const noexcept { return be_value_.data(); }

        /// @return Number of bytes in the encoded range.
        constexpr std::size_t size() const noexcept { return Size(); }

        /// @return Iterator to the first encoded byte.
        constexpr auto begin() const noexcept { return data(); }

        /// @return Iterator one past the last encoded byte.
        constexpr auto end() const noexcept { return data() + Size(); }
        // NOLINTEND(readability-identifier-naming)

        /// @return The decoded unsigned value.
        explicit constexpr operator uint64_t() const noexcept { return Get(); }

        /// Compares the complete stored encoding, including encoded width.
        /// @param other Encoding to compare.
        /// @return Lexicographic comparison of the stored bytes.
        constexpr auto operator<=>(const UintVar& other) const noexcept = default;

      private:
        static constexpr std::size_t kMaxEncodedSize = sizeof(std::uint64_t) + 1;
        std::array<std::uint8_t, kMaxEncodedSize> be_value_;
    };
}
