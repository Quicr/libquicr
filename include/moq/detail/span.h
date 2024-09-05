// SPDX-FileCopyrightText: Copyright (c) 2024 Cisco Systems
// SPDX-License-Identifier: BSD-2-Clause

// NOLINTBEGIN(readability-identifier-naming)

#pragma once

#if __cplusplus >= 202002L
#include <span>

template<class T, std::size_t N = std::dynamic_extent>
using Span = std::span<T, N>;
#else
#include <array>
#include <cstdint>
#include <iterator>
#include <limits>
#include <string>
#include <type_traits>
#include <vector>

template<class T, std::size_t Extent = std::numeric_limits<std::size_t>::max()>
class Span
{
    static constexpr std::size_t dynamic_extent = std::numeric_limits<std::size_t>::max();

    template<typename Ptr>
    class IteratorImpl : public std::iterator_traits<Ptr>
    {
        using it_traits = std::iterator_traits<Ptr>;
        using it_impl = IteratorImpl;

      public:
        using value_type = typename it_traits::value_type;
        using reference = typename it_traits::reference;
        using pointer = typename it_traits::pointer;
        using difference_type = typename it_traits::difference_type;
        using iterator_category = typename it_traits::iterator_category;

        constexpr IteratorImpl() noexcept
          : _ptr(Ptr())
        {
        }
        constexpr IteratorImpl(const pointer& ptr) noexcept
          : _ptr(ptr)
        {
        }
        constexpr IteratorImpl(const IteratorImpl&) = default;

        template<typename OtherIt>
        IteratorImpl(const IteratorImpl<OtherIt>& other)
          : _ptr(other.base())
        {
        }

        IteratorImpl& operator=(const IteratorImpl&) = default;

        constexpr reference operator*() const noexcept { return *_ptr; }
        constexpr pointer operator->() const noexcept { return _ptr; }

        constexpr it_impl& operator++() noexcept
        {
            ++_ptr;
            return *this;
        }
        constexpr it_impl operator++(int) noexcept { return it_impl(_ptr++); }
        constexpr it_impl& operator--() noexcept
        {
            --_ptr;
            return *this;
        }
        constexpr it_impl operator--(int) noexcept { return it_impl(_ptr--); }

        constexpr it_impl& operator+=(difference_type n) noexcept
        {
            _ptr += n;
            return *this;
        }
        constexpr it_impl& operator-=(difference_type n) noexcept
        {
            _ptr -= n;
            return *this;
        }
        constexpr it_impl operator+(difference_type n) const noexcept { return it_impl(_ptr + n); }
        constexpr it_impl operator-(difference_type n) const noexcept { return it_impl(_ptr - n); }

        friend constexpr difference_type operator-(const it_impl& lhs, const it_impl& rhs) noexcept
        {
            return lhs.base() - rhs.base();
        }

        constexpr reference operator[](difference_type pos) noexcept { return _ptr[pos]; }

        friend constexpr bool operator==(const it_impl& lhs, const it_impl& rhs) { return lhs._ptr == rhs._ptr; }
        friend constexpr bool operator!=(const it_impl& lhs, const it_impl& rhs) { return !(lhs == rhs); }

        friend constexpr bool operator<(const it_impl& lhs, const it_impl& rhs) { return lhs._ptr < rhs._ptr; }
        friend constexpr bool operator>(const it_impl& lhs, const it_impl& rhs) { return lhs > rhs; }
        friend constexpr bool operator<=(const it_impl& lhs, const it_impl& rhs) { return !(lhs > rhs); }
        friend constexpr bool operator>=(const it_impl& lhs, const it_impl& rhs) { return !(lhs._ptr < rhs._ptr); }

        constexpr const Ptr& base() const noexcept { return _ptr; }

      private:
        Ptr _ptr;
    };

    template<std::size_t Offset, std::size_t Count>
    static constexpr std::size_t subspan_extent()
    {
        if constexpr (Count != dynamic_extent) {
            return Count;
        } else if constexpr (Extent != dynamic_extent) {
            return Extent - Offset;
        }

        return dynamic_extent;
    }

  public:
    using element_type = T;
    using value_type = typename std::remove_cv_t<T>;
    using size_type = std::size_t;
    using difference_type = std::ptrdiff_t;
    using pointer = T*;
    using const_pointer = const T*;
    using reference = T&;
    using const_reference = const T&;
    using iterator = IteratorImpl<pointer>;
    using const_iterator = IteratorImpl<const_pointer>;
    using reverse_iterator = std::reverse_iterator<iterator>;
    using const_reverse_iterator = std::reverse_iterator<const_iterator>;

    static constexpr size_type extent = Extent;

    template<typename std::enable_if_t<(Extent + 1u) <= 1u, bool> = true>
    constexpr Span() noexcept
      : _ptr(nullptr)
      , _extent(0)
    {
    }

    template<typename It>
    constexpr Span(It first, size_type count)
      : _ptr(std::addressof(*first))
      , _extent(count)
    {
        if constexpr (extent != dynamic_extent) {
            if (__builtin_is_constant_evaluated() && !bool(count == extent))
                __builtin_unreachable();
        }
    }

    template<typename It, typename End>
    constexpr Span(It first, End last)
      : _ptr(std::addressof(*first))
      , _extent(static_cast<size_type>(last - first))
    {
        if constexpr (extent != dynamic_extent) {
            if (__builtin_is_constant_evaluated() && !bool((last - first) == extent))
                __builtin_unreachable();
        }
    }

    template<size_type N>
    constexpr Span(element_type (&arr)[N]) noexcept
      : Span(static_cast<pointer>(arr), N)
    {
    }

    template<class U, size_type N, typename std::enable_if_t<std::is_convertible_v<U, T>, bool> = true>
    constexpr Span(std::array<U, N>& arr) noexcept
      : Span(static_cast<pointer>(arr.data()), N)
    {
    }

    template<class U, size_type N, typename std::enable_if_t<std::is_convertible_v<U, T>, bool> = true>
    constexpr Span(const std::array<U, N>& arr) noexcept
      : Span(static_cast<pointer>(arr.data()), N)
    {
    }

    template<class R>
    constexpr Span(R&& range)
      : Span(range.data(), range.size())
    {
    }

    template<class U,
             size_type N,
             typename std::enable_if_t<(Extent == dynamic_extent || N == dynamic_extent || Extent == N) &&
                                         std::is_convertible_v<U, T>,
                                       bool> = true>
    constexpr Span(const Span<U, N>& other) noexcept
      : _ptr(other.data())
      , _extent(other.size())
    {
    }

    constexpr Span(const Span&) noexcept = default;

    constexpr Span& operator=(const Span&) noexcept = default;

    constexpr size_type size() const noexcept { return _extent; }

    constexpr size_type size_bytes() const noexcept { return _extent * sizeof(element_type); }

    [[nodiscard]] constexpr bool empty() const noexcept { return size() == 0; }

    constexpr reference front() const noexcept
    {
        if (__builtin_is_constant_evaluated() && !bool(!empty()))
            __builtin_unreachable();
        return *_ptr;
    }

    constexpr reference back() const noexcept
    {
        if (__builtin_is_constant_evaluated() && !bool(!empty()))
            __builtin_unreachable();
        return *(_ptr + (size() - 1));
    }

    constexpr reference operator[](size_type index) const noexcept
    {
        if (__builtin_is_constant_evaluated() && !bool(index < size()))
            __builtin_unreachable();
        return *(_ptr + index);
    }

    constexpr pointer data() const noexcept { return _ptr; }

    constexpr iterator begin() const noexcept { return iterator(_ptr); }
    constexpr iterator end() const noexcept { return iterator(_ptr + size()); }
    constexpr const_iterator cbegin() const noexcept { return const_iterator(_ptr); }
    constexpr const_iterator cend() const noexcept { return const_iterator(_ptr + size()); }
    constexpr reverse_iterator rbegin() const noexcept { return reverse_iterator(end()); }
    constexpr reverse_iterator rend() const noexcept { return reverse_iterator(begin()); }

    template<size_t Count>
    constexpr Span<element_type, Count> first() const noexcept
    {
        if constexpr (Extent == dynamic_extent) {
            if (__builtin_is_constant_evaluated() && !bool(Count <= size()))
                __builtin_unreachable();
        } else {
            static_assert(Count <= extent);
        }

        return Span<element_type, Count>{ this->data(), Count };
    }

    constexpr Span<element_type, dynamic_extent> first(size_type count) const noexcept
    {
        if (__builtin_is_constant_evaluated() && !bool(count <= size()))
            __builtin_unreachable();
        return Span<element_type, dynamic_extent>{ this->data(), count };
    }

    template<size_type Count>
    constexpr Span<element_type, Count> last() const noexcept
    {
        if constexpr (Extent == dynamic_extent) {
            if (__builtin_is_constant_evaluated() && !bool(Count <= size()))
                __builtin_unreachable();
        } else {
            static_assert(Count <= extent);
        }

        return Span<element_type, Count>{ this->data() + (size() - Count), Count };
    }

    constexpr Span<element_type, dynamic_extent> last(size_type count) const noexcept
    {
        if (__builtin_is_constant_evaluated() && !bool(count <= size()))
            __builtin_unreachable();
        return Span<element_type, dynamic_extent>{ this->data() + (size() - count), count };
    }

    template<size_type Offset, size_type Count>
    constexpr auto subspan() const noexcept -> Span<element_type, subspan_extent<Offset, Count>>
    {
        if constexpr (Extent == dynamic_extent) {
            if (__builtin_is_constant_evaluated() && !bool(Offset <= size()))
                __builtin_unreachable();
        } else {
            static_assert(Offset <= extent);
        }

        if constexpr (Count == dynamic_extent) {
            return Span<element_type, subspan_extent<Offset, Count>>{ this->data() + Offset, this->size() - Offset };
        }
        if (Extent == dynamic_extent) {
            if (__builtin_is_constant_evaluated() && !bool(Count <= size()))
                __builtin_unreachable();
            if (__builtin_is_constant_evaluated() && !bool(Count <= (size() - Offset)))
                __builtin_unreachable();
        } else {
            static_assert(Count <= extent);
            static_assert(Count <= (extent - Offset));
        }

        return Span<element_type, subspan_extent<Offset, Count>>{ this->data() + Offset, Count };
    }

    constexpr Span<element_type, dynamic_extent> subspan(size_type offset,
                                                         size_type count = dynamic_extent) const noexcept
    {
        if (__builtin_is_constant_evaluated() && !bool(offset <= size()))
            __builtin_unreachable();

        if (count == dynamic_extent) {
            count = size() - offset;
        } else {
            if (__builtin_is_constant_evaluated() && !bool(count <= size()))
                __builtin_unreachable();
            if (__builtin_is_constant_evaluated() && !bool(offset + count <= size()))
                __builtin_unreachable();
        }

        return Span<element_type, dynamic_extent>{ this->data() + offset, count };
    }

  private:
    T* _ptr;
    size_type _extent;
};

template<class It, class EndOrSize>
Span(It, EndOrSize) -> Span<std::remove_reference_t<decltype(*std::declval<It&>())>>;

template<class T, std::size_t N>
Span(T (&)[N]) -> Span<T, N>;

template<class T, std::size_t N>
Span(std::array<T, N>&) -> Span<T, N>;

template<class T, std::size_t N>
Span(const std::array<T, N>&) -> Span<const T, N>;

template<class R>
Span(R&&) -> Span<std::remove_reference_t<decltype(*std::declval<decltype(std::begin(std::declval<R&>()))&>())>>;
#endif

// NOLINTEND(readability-identifier-naming)
