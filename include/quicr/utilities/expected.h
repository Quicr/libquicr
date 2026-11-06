// SPDX-FileCopyrightText: Copyright (c) 2026 Cisco Systems
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

#include <memory>
#include <optional>
#include <type_traits>
#include <utility>
#include <variant>

namespace quicr {
    /**
     * @brief Wrapper that explicitly marks a value as an error.
     * @tparam E Stored error type.
     */
    template<typename E>
    class Unexpected
    {
      public:
        /** @brief Copies an error wrapper. @param other Wrapper to copy. */
        Unexpected(const Unexpected& other) = default;

        /** @brief Moves an error wrapper. @param other Wrapper to move. */
        Unexpected(Unexpected&& other) = default;

        /**
         * @brief Constructs the error from a forwarded value.
         * @tparam T Source type accepted by `E`.
         * @param value Value used to construct the error.
         */
        template<typename T>
        Unexpected(T&& value)
          : error_{ std::forward<T>(value) }
        {
        }

        /**
         * @brief Constructs the error in place from arguments.
         * @tparam Args Constructor argument types for `E`.
         * @param args Arguments forwarded to `E`.
         */
        template<typename... Args>
        Unexpected(Args&&... args)
          : error_{ std::forward<Args>(args)... }
        {
        }

        /** @return A reference to the stored error, valid for the lifetime of this wrapper. */
        const E& error() const { return error_; }

      private:
        E error_;
    };

    /**
     * @brief Holds either a success value or an error value.
     * @tparam T Success type.
     * @tparam E Error type.
     * @note This class is not internally synchronized. References and pointers returned by accessors are invalidated
     * when the `Expected` object is destroyed or moved.
     */
    template<typename T, typename E>
    class Expected
    {
      public:
        /** @brief Copies the contained alternative. @param other Result to copy. */
        Expected(const Expected& other) = default;

        /** @brief Moves the contained alternative. @param other Result to move. */
        Expected(Expected&& other) = default;

        /**
         * @brief Constructs the variant alternative selected by `std::variant<T, E>`.
         * @tparam U Type convertible to `T` or `E`.
         * @param value Value forwarded to the variant.
         * @throws Any exception thrown while constructing the selected alternative.
         */
        template<typename U>
            requires std::is_convertible_v<U, T> || std::is_convertible_v<U, E>
        Expected(U&& value)
          : data_(std::forward<U>(value))
        {
        }

        /**
         * @brief Constructs an error result from an unexpected value.
         * @tparam U Error source type convertible to `E`.
         * @param unex Wrapper whose error is copied into this result.
         */
        template<typename U>
            requires std::is_convertible_v<U, E>
        Expected(Unexpected<U>&& unex)
          : data_(unex.error())
        {
        }

        /** @return `true` when the success alternative is active. */
        constexpr bool has_value() const noexcept { return data_.index() == 0; }

        /** @return The same state reported by has_value(). */
        constexpr explicit operator bool() const noexcept { return has_value(); }

        /**
         * @return A reference to the success value.
         * @throws std::bad_variant_access If this object contains an error.
         */
        const T& value() const { return std::get<0>(data_); }

        /**
         * @return A reference to the error value.
         * @throws std::bad_variant_access If this object contains a success value.
         */
        const E& error() const { return std::get<1>(data_); }

        /**
         * @return A pointer to the success value.
         * @warning Calling this while the object contains an error terminates the process because the function is
         * `noexcept` and its internal value access throws.
         */
        constexpr const T* operator->() const noexcept { return std::addressof(value()); }

      private:
        std::variant<T, E> data_;
    };

    /**
     * @brief `void` specialization that represents success by an empty optional.
     * @tparam E Error type.
     */
    template<typename E>
    class Expected<void, E>
    {
      public:
        /** @brief Constructs a successful result. */
        Expected() = default;

        /**
         * @brief Constructs an error result.
         * @tparam U Type accepted by `E`.
         * @param value Value forwarded to the stored error.
         */
        template<typename U>
        Expected(U&& value)
          : error_(std::forward<U>(value))
        {
        }

        /**
         * @brief Constructs an error result from an unexpected value.
         * @tparam U Error source type convertible to `E`.
         * @param unex Wrapper whose error is copied into this result.
         */
        template<typename U>
            requires std::is_convertible_v<U, E>
        Expected(Unexpected<U>&& unex)
          : error_(unex.error())
        {
        }

        /** @return `true` when no error is stored. */
        bool has_value() const noexcept { return !error_.has_value(); }

        /** @return The same state reported by has_value(). */
        explicit operator bool() const noexcept { return has_value(); }

        /**
         * @return A reference to the stored error.
         * @throws std::bad_optional_access If this object represents success.
         */
        const E& error() const { return error_.value(); }

      private:
        std::optional<E> error_;
    };
}
