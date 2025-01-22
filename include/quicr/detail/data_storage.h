// SPDX-FileCopyrightText: Copyright (c) 2024 Cisco Systems
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

#include "span.h"

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
    class DataStorage : public std::enable_shared_from_this<DataStorage<Allocator>>
    {
        template<class It, class SpanIt>
        class Iterator
        {
          public:
            // NOLINTBEGIN(readability-identifier-naming)
            using value_type = std::uint8_t;
            using difference_type = std::ptrdiff_t;
            using pointer = std::uint8_t*;
            using reference = std::uint8_t&;
            using iterator_category = std::bidirectional_iterator_tag;
            // NOLINTEND(readability-identifier-naming)

            constexpr Iterator() noexcept = default;
            constexpr Iterator(const It& it, const It& end_it) noexcept
              : it_(it)
              , end_it_(end_it)
              , span_it_(it == end_it_ ? std::nullopt : std::optional<SpanIt>{ (*it_)->begin() })
            {
            }

            constexpr Iterator(const Iterator&) = default;

            Iterator& operator=(const Iterator&) = default;

            constexpr const std::uint8_t& operator*() const noexcept { return **span_it_; }
            constexpr const std::uint8_t* operator->() const noexcept { return span_it_->operator->(); }

            constexpr Iterator& operator++() noexcept
            {
                if (!span_it_.has_value() || ++*span_it_ == (*it_)->end()) {
                    span_it_ = ++it_ == end_it_ ? std::nullopt : std::optional<SpanIt>{ (*it_)->begin() };
                }

                return *this;
            }

            constexpr Iterator& operator--() noexcept
            {
                if (!span_it_.has_value() || --*span_it_ == std::prev((*it_)->begin())) {
                    span_it_ = std::optional<SpanIt>{ std::prev((*--it_)->end()) };
                }

                return *this;
            }

            friend constexpr bool operator==(const Iterator& lhs, const Iterator& rhs)
            {
                return lhs.it_ == rhs.it_ && lhs.span_it_ == rhs.span_it_;
            }

            friend constexpr bool operator!=(const Iterator& lhs, const Iterator& rhs) { return !(lhs == rhs); }

          private:
            It it_;
            It end_it_;
            std::optional<SpanIt> span_it_;
        };

      protected:
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

        const SliceType& First() const noexcept { return *(this->begin()); }
        const SliceType& Last() const noexcept { return *std::prev(this->end()); }

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
        using iterator = Iterator<typename BufferType::iterator, typename SliceType::element_type::iterator>;
        using const_iterator =
          Iterator<typename BufferType::const_iterator, typename SliceType::element_type::const_iterator>;

        std::size_t size() const noexcept
        {
            std::size_t value = 0;
            for (const auto& buf : buffer_) {
                value += buf->size();
            }
            return value;
        }

        iterator begin() noexcept { return iterator(buffer_.begin(), buffer_.end()); }
        iterator end() noexcept { return iterator(buffer_.end(), buffer_.end()); }
        const_iterator begin() const noexcept { return const_iterator(buffer_.begin(), buffer_.end()); }
        const_iterator end() const noexcept { return const_iterator(buffer_.end(), buffer_.end()); }
        const_iterator cbegin() const noexcept { return const_iterator(buffer_.begin(), buffer_.end()); }
        const_iterator cend() const noexcept { return const_iterator(buffer_.end(), buffer_.end()); }
        // NOLINTEND(readability-identifier-naming)

      private:
        BufferType buffer_;
    };

    class DataStorageSpan
    {
        static constexpr std::size_t dynamic_extent = std::numeric_limits<std::size_t>::max();

      public:
        DataStorageSpan(std::shared_ptr<DataStorage<>> storage,
                        std::size_t offset = 0,
                        std::size_t length = dynamic_extent)
          : storage_(std::move(storage))
          , offset_(offset)
          , length_(length == dynamic_extent ? storage_->size() - offset : length)
        {
        }

        DataStorageSpan(const DataStorageSpan&) = default;
        DataStorageSpan(DataStorageSpan&&) = default;
        DataStorageSpan& operator=(const DataStorageSpan&) = default;
        DataStorageSpan& operator=(DataStorageSpan&&) = default;

        DataStorageSpan Subspan(std::size_t offset, std::size_t length = dynamic_extent) const noexcept
        {
            if (length == dynamic_extent) {
                length = length_ - offset;
            }

            return DataStorageSpan(storage_, offset_ + offset, length);
        }

        // NOLINTBEGIN(readability-identifier-naming)
        std::size_t size() const noexcept { return length_; }

        DataStorage<>::iterator begin() noexcept { return std::next(storage_->begin(), offset_); }
        DataStorage<>::iterator end() noexcept { return std::next(this->begin(), size()); }

        DataStorage<>::const_iterator begin() const noexcept { return std::next(storage_->cbegin(), offset_); }
        DataStorage<>::const_iterator end() const noexcept { return std::next(this->begin(), size()); }
        // NOLINTEND(readability-identifier-naming)

      private:
        std::shared_ptr<DataStorage<>> storage_;
        std::size_t offset_ = 0;
        std::size_t length_ = dynamic_extent;
    };
}
