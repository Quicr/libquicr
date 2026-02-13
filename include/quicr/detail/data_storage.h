// SPDX-FileCopyrightText: Copyright (c) 2024 Cisco Systems
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

#include "quicr/common.h"

#include <cstdint>
#include <deque>
#include <memory>
#include <optional>
#include <span>
#include <vector>

namespace quicr {

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
        using BufferType = std::deque<SliceType>;

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
        const SliceType GetLast() const noexcept { return *std::prev(this->end()); }

        void Push(std::span<const uint8_t> bytes)
        {
            auto slice = std::make_shared<typename SliceType::element_type>();
            slice->assign(bytes.begin(), bytes.end());

            buffer_.push_back(std::move(slice));
        }

        void Push(SliceType slice) { buffer_.push_back(std::move(slice)); }

        friend DataStorage& operator<<(DataStorage& buffer, std::span<const uint8_t> value)
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

        std::size_t EraseFront(std::size_t len) noexcept
        {
            for (const auto& s : buffer_) {
                if (s == nullptr) {
                    return 0;
                }

                const auto size = s->size();
                if (len >= size) {
                    buffer_.pop_front();
                    len -= size;
                } else {
                    return len;
                }
            }

            return 0;
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

    class DataStorageDynView
    {
      public:
        DataStorageDynView(std::shared_ptr<DataStorage<>> storage,
                           std::size_t start_pos = 0,
                           std::optional<std::size_t> end_pos = std::nullopt)
          : storage_(std::move(storage))
          , start_pos_(start_pos)
          , end_pos_(end_pos)
        {
        }

        DataStorageDynView(const DataStorageDynView&) = default;
        DataStorageDynView(DataStorageDynView&&) = default;
        DataStorageDynView& operator=(const DataStorageDynView&) = default;
        DataStorageDynView& operator=(DataStorageDynView&&) = default;

        DataStorageDynView Subspan(std::size_t start_pos,
                                   std::optional<std::size_t> end_pos = std::nullopt) const noexcept
        {
            if (not end_pos.has_value() || *end_pos > storage_->size()) {
                end_pos = storage_->size();
            }

            return DataStorageDynView(storage_, start_pos, *end_pos);
        }

        // NOLINTBEGIN(readability-identifier-naming)
        std::size_t size() const noexcept { return (end_pos_.has_value() ? *end_pos_ : storage_->size()) - start_pos_; }

        DataStorage<>::iterator begin() noexcept { return std::next(storage_->begin(), start_pos_); }
        DataStorage<>::iterator end() noexcept { return std::next(this->begin(), size()); }

        DataStorage<>::const_iterator begin() const noexcept { return std::next(storage_->cbegin(), start_pos_); }
        DataStorage<>::const_iterator end() const noexcept { return std::next(this->begin(), size()); }
        // NOLINTEND(readability-identifier-naming)

      private:
        std::shared_ptr<DataStorage<>> storage_;
        std::size_t start_pos_ = 0;
        std::optional<std::size_t> end_pos_;
    };
}
