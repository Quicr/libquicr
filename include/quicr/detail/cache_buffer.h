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

class CacheBuffer
{
    template<class It, class SpanIt>
    class IteratorImpl
    {
        using it_traits = std::iterator_traits<const std::uint8_t*>;
        using it_impl = IteratorImpl;

      public:
        using value_type = typename it_traits::value_type;
        using reference = typename it_traits::reference;
        using pointer = typename it_traits::pointer;
        using difference_type = typename it_traits::difference_type;
        using iterator_category = std::forward_iterator_tag;

        constexpr IteratorImpl() noexcept
          : _it(It())
        {
        }
        constexpr IteratorImpl(const It& it, const It& end_it) noexcept
          : _it(it)
          , _end_it(end_it)
          , _span_it(it == _end_it ? std::nullopt : std::optional<SpanIt>{ _it->begin() })
        {
        }

        constexpr IteratorImpl(const IteratorImpl&) = default;

        IteratorImpl& operator=(const IteratorImpl&) = default;

        constexpr reference operator*() const noexcept { return **_span_it; }
        constexpr pointer operator->() const noexcept { return _span_it->operator->(); }

        constexpr it_impl& operator++() noexcept
        {
            if (++*_span_it == _it->end()) {
                _span_it = ++_it == _end_it ? std::nullopt : std::optional<SpanIt>{ _it->begin() };
            }

            return *this;
        }

        friend constexpr bool operator==(const it_impl& lhs, const it_impl& rhs)
        {
            return lhs._it == rhs._it && lhs._span_it == rhs._span_it;
        }

        friend constexpr bool operator!=(const it_impl& lhs, const it_impl& rhs) { return !(lhs == rhs); }

      private:
        It _it;
        It _end_it;
        std::optional<SpanIt> _span_it;
    };

    using BufferType = std::vector<Span<const uint8_t>>;

  public:
    using iterator = IteratorImpl<BufferType::iterator, BufferType::value_type::iterator>;
    using const_iterator = IteratorImpl<BufferType::const_iterator, BufferType::value_type::iterator>;

    CacheBuffer() = default;

    void Push(Span<const uint8_t> bytes) { _buffer.push_back(std::move(bytes)); }

    std::vector<uint8_t> Copy() const { return { this->begin(), this->end() }; }

    friend CacheBuffer& operator<<(CacheBuffer& buffer, const Span<const uint8_t>& value)
    {
        buffer.Push(value);
        return buffer;
    }

    template<typename T, std::enable_if_t<std::is_integral_v<T>, bool> = true>
    friend CacheBuffer& operator<<(CacheBuffer& buffer, const T& value)
    {
        buffer.Push(Span{ reinterpret_cast<const std::uint8_t*>(&value), sizeof(T) });
        return buffer;
    }

    iterator begin() noexcept { return iterator(_buffer.begin(), _buffer.end()); }
    iterator end() noexcept { return iterator(_buffer.end(), _buffer.end()); }
    const_iterator begin() const noexcept { return const_iterator(_buffer.begin(), _buffer.end()); }
    const_iterator end() const noexcept { return const_iterator(_buffer.end(), _buffer.end()); }

  private:
    BufferType _buffer;
};
