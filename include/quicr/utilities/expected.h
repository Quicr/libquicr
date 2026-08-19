#pragma once

#include <memory>
#include <optional>
#include <type_traits>
#include <utility>
#include <variant>

namespace quicr {
    template<typename E>
    class Unexpected
    {
      public:
        Unexpected(const Unexpected&) = default;

        Unexpected(Unexpected&&) = default;

        template<typename T>
        Unexpected(T&& value)
          : error_(std::forward<T>(value))
        {
        }

        template<typename... Args>
        Unexpected(Args&&... args)
          : error_{ std::forward<Args>(args)... }
        {
        }

        const E& error() const { return error_; }

      private:
        E error_;
    };

    template<typename T, typename E>
    class Expected
    {
      public:
        Expected(const Expected&) = default;

        Expected(Expected&&) = default;

        template<typename U>
            requires std::is_convertible_v<U, T> || std::is_convertible_v<U, E>
        Expected(U&& value)
          : data_(std::forward<U>(value))
        {
        }

        template<typename U>
            requires std::is_convertible_v<U, E>
        Expected(Unexpected<U>&& unex)
          : data_(unex.error())
        {
        }

        constexpr bool has_value() const noexcept { return data_.index() == 0; }

        constexpr explicit operator bool() const noexcept { return has_value(); }

        const T& value() const { return std::get<0>(data_); }

        const E& error() const { return std::get<1>(data_); }

        constexpr const T* operator->() const noexcept { return std::addressof(value()); }

      private:
        std::variant<T, E> data_;
    };

    template<typename E>
    class Expected<void, E>
    {
      public:
        Expected() = default;

        template<typename U>
        Expected(U&& value)
          : error_(std::forward<U>(value))
        {
        }

        template<typename U>
            requires std::is_convertible_v<U, E>
        Expected(Unexpected<U>&& unex)
          : error_(unex.error())
        {
        }

        bool has_value() const noexcept { return !error_.has_value(); }

        explicit operator bool() const noexcept { return has_value(); }

        const E& error() const { return error_.value(); }

      private:
        std::optional<E> error_;
    };
}
