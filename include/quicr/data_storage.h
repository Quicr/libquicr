// SPDX-FileCopyrightText: Copyright (c) 2024 Cisco Systems
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

#include "detail/span.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

namespace quicr {
    template<class T, std::enable_if_t<std::is_standard_layout_v<T>, bool> = true>
    inline Span<const uint8_t> AsBytes(const T& value)
    {
        return Span{ reinterpret_cast<const std::uint8_t*>(&value), sizeof(T) };
    }

    template<>
    inline Span<const uint8_t> AsBytes<std::string>(const std::string& value)
    {
        return Span{ reinterpret_cast<const std::uint8_t*>(value.data()), value.size() };
    }

    template<class Allocator = std::allocator<std::uint8_t>>
    class DataStorage
    {
        template<class It, class SpanIt>
        class IteratorImpl
        {
          public:
            // NOLINTBEGIN(readability-identifier-naming)
            using value_type = std::uint8_t;
            using difference_type = std::ptrdiff_t;
            using pointer = std::uint8_t*;
            using reference = std::uint8_t&;
            using iterator_category = std::forward_iterator_tag;
            // NOLINTEND(readability-identifier-naming)

            constexpr IteratorImpl() noexcept = default;
            constexpr IteratorImpl(const It& it, const It& end_it) noexcept
              : it_(it)
              , end_it_(end_it)
              , span_it_(it == end_it_ ? std::nullopt : std::optional<SpanIt>{ (*it_)->begin() })
            {
            }

            constexpr IteratorImpl(const IteratorImpl&) = default;

            IteratorImpl& operator=(const IteratorImpl&) = default;

            constexpr const std::uint8_t& operator*() const noexcept { return **span_it_; }
            constexpr const std::uint8_t* operator->() const noexcept { return span_it_->operator->(); }

            constexpr IteratorImpl& operator++() noexcept
            {
                if (++*span_it_ == (*it_)->end()) {
                    span_it_ = ++it_ == end_it_ ? std::nullopt : std::optional<SpanIt>{ (*it_)->begin() };
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

        using SliceType = std::shared_ptr<std::vector<uint8_t, Allocator>>;
        using BufferType = std::vector<SliceType>;

        DataStorage() = default;
        DataStorage(SliceType slice)
          : buffer_{ std::move(slice) }
        {
        }

      public:
        static std::shared_ptr<DataStorage> Create() noexcept
        {
            return std::shared_ptr<DataStorage>(new DataStorage());
        }

        static std::shared_ptr<DataStorage> Create(SliceType slice) noexcept
        {
            return std::shared_ptr<DataStorage>(new DataStorage(std::move(slice)));
        }

        bool Empty() const noexcept { return buffer_.empty(); }
        const SliceType& First() const noexcept { return buffer_.front(); }
        const SliceType& Last() const noexcept { return buffer_.back(); }

        void Push(Span<const uint8_t> bytes)
        {
            auto slice = std::make_shared<typename SliceType::element_type>();
            slice->assign(bytes.begin(), bytes.end());

            buffer_.push_back(std::move(slice));
        }

        void Push(SliceType slice) { buffer_.push_back(std::move(slice)); }

        friend DataStorage& operator<<(DataStorage& buffer, Span<const uint8_t> value)
        {
            buffer.Push(value);
            return buffer;
        }

        // NOLINTBEGIN(readability-identifier-naming)
        using iterator = IteratorImpl<typename BufferType::iterator, typename SliceType::element_type::iterator>;
        using const_iterator =
          IteratorImpl<typename BufferType::const_iterator, typename SliceType::element_type::iterator>;

        iterator begin() noexcept { return iterator(buffer_.begin(), buffer_.end()); }
        iterator end() noexcept { return iterator(buffer_.end(), buffer_.end()); }
        const_iterator begin() const noexcept { return const_iterator(buffer_.begin(), buffer_.end()); }
        const_iterator end() const noexcept { return const_iterator(buffer_.end(), buffer_.end()); }
        // NOLINTEND(readability-identifier-naming)

      private:
        BufferType buffer_;
    };
}
