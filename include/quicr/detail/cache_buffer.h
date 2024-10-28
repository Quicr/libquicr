// SPDX-FileCopyrightText: Copyright (c) 2024 Cisco Systems
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

#include "span.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <type_traits>
#include <vector>

namespace quicr {
    template<class T, std::enable_if_t<std::is_standard_layout_v<T>, bool> = true>
    inline Span<const uint8_t> AsBytes(const T& value)
    {
        return Span{ reinterpret_cast<const std::uint8_t*>(&value), sizeof(T) };
    }

    class CacheBuffer
    {
        template<class T>
        struct IsRange
        {
            static constexpr bool kValue = noexcept(std::declval<T>().begin())&& noexcept(std::declval<T>().end());
        };

        template<class It, class SpanIt>
        class IteratorImpl
        {
          public:
            constexpr IteratorImpl() noexcept = default;
            constexpr IteratorImpl(const It& it, const It& end_it) noexcept
              : it_(it)
              , end_it_(end_it)
              , span_it_(it == end_it_ ? std::nullopt : std::optional<SpanIt>{ it_->begin() })
            {
            }

            constexpr IteratorImpl(const IteratorImpl&) = default;

            IteratorImpl& operator=(const IteratorImpl&) = default;

            constexpr const std::uint8_t& operator*() const noexcept { return **span_it_; }
            constexpr const std::uint8_t* operator->() const noexcept { return span_it_->operator->(); }

            constexpr IteratorImpl& operator++() noexcept
            {
                if (++*span_it_ == it_->end()) {
                    span_it_ = ++it_ == end_it_ ? std::nullopt : std::optional<SpanIt>{ it_->begin() };
                }

                return *this;
            }

            friend constexpr bool operator==(const IteratorImpl& lhs, const IteratorImpl& rhs)
            {
                return lhs.it_ == rhs.it_ && lhs.span_it_ == rhs.span_it_;
            }

            friend constexpr bool operator!=(const IteratorImpl& lhs, const IteratorImpl& rhs) { return !(lhs == rhs); }

          private:
            It it_;
            It end_it_;
            std::optional<SpanIt> span_it_;
        };

        using BufferType = std::vector<Span<const uint8_t>>;

      public:
        CacheBuffer() = default;

        void Push(Span<const uint8_t> bytes) { buffer_.push_back(std::move(bytes)); }

        friend CacheBuffer& operator<<(CacheBuffer& buffer, Span<const uint8_t> value)
        {
            buffer.Push(value);
            return buffer;
        }

        template<typename T, std::enable_if_t<std::is_standard_layout_v<T> && !IsRange<T>::kValue, bool> = true>
        friend CacheBuffer& operator<<(CacheBuffer& buffer, const T& value)
        {
            buffer.Push(AsBytes(value));
            return buffer;
        }

        // NOLINTBEGIN(readability-identifier-naming)
        using iterator = IteratorImpl<BufferType::iterator, BufferType::value_type::iterator>;
        using const_iterator = IteratorImpl<BufferType::const_iterator, BufferType::value_type::iterator>;

        iterator begin() noexcept { return iterator(buffer_.begin(), buffer_.end()); }
        iterator end() noexcept { return iterator(buffer_.end(), buffer_.end()); }
        const_iterator begin() const noexcept { return const_iterator(buffer_.begin(), buffer_.end()); }
        const_iterator end() const noexcept { return const_iterator(buffer_.end(), buffer_.end()); }
        // NOLINTEND(readability-identifier-naming)

      private:
        BufferType buffer_;
    };
}
