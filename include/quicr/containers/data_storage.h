// SPDX-FileCopyrightText: Copyright (c) 2024 Cisco Systems
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

#include "quicr/utilities/bytes.h"

#include <cstdint>
#include <deque>
#include <memory>
#include <optional>
#include <span>
#include <vector>

namespace quicr {

    /**
     * @brief Shared, segmented byte storage with a flattened bidirectional iterator.
     * @tparam Allocator Allocator used by each owned byte-vector slice.
     * @details The container and its iterators are not thread-safe. Slices are shared, so retained `SliceType`
     * instances can outlive their membership in the storage.
     */
    template<class Allocator = std::allocator<std::uint8_t>>
    class DataStorage : public std::enable_shared_from_this<DataStorage<Allocator>>
    {
        template<class It, class SpanIt>
        class Iterator
        {
          public:
            // NOLINTBEGIN(readability-identifier-naming)
            /** @brief Value exposed by flattened iteration. */
            using value_type = std::uint8_t;

            /** @brief Iterator distance type. */
            using difference_type = std::ptrdiff_t;

            /** @brief Mutable-byte pointer type retained for iterator traits compatibility. */
            using pointer = std::uint8_t*;

            /** @brief Mutable-byte reference type retained for iterator traits compatibility. */
            using reference = std::uint8_t&;

            /** @brief Iterator category. */
            using iterator_category = std::bidirectional_iterator_tag;
            // NOLINTEND(readability-identifier-naming)

            /** @brief Constructs a singular/end-like iterator. */
            constexpr Iterator() noexcept = default;

            /**
             * @brief Constructs a flattened iterator over a slice range.
             * @param it Current slice iterator.
             * @param end_it End of the slice range.
             * @pre Every traversed slice is non-null and nonempty.
             */
            constexpr Iterator(const It& it, const It& end_it) noexcept
              : it_(it)
              , end_it_(end_it)
              , span_it_(it == end_it_ ? std::nullopt : std::optional<SpanIt>{ (*it_)->begin() })
            {
            }

            /** @brief Copies iterator position. @param other Iterator to copy. */
            constexpr Iterator(const Iterator& other) = default;

            /** @brief Copies iterator position. @param other Iterator to copy. @return `*this`. */
            Iterator& operator=(const Iterator& other) = default;

            /** @return Reference to the current byte. Dereferencing an end or invalidated iterator is undefined. */
            constexpr const std::uint8_t& operator*() const noexcept { return **span_it_; }

            /** @return Pointer to the current byte. Accessing an end or invalidated iterator is undefined. */
            constexpr const std::uint8_t* operator->() const noexcept { return span_it_->operator->(); }

            /** @brief Advances to the next byte, crossing slice boundaries as needed. */
            constexpr Iterator& operator++() noexcept
            {
                if (!span_it_.has_value() || ++*span_it_ == (*it_)->end()) {
                    span_it_ = ++it_ == end_it_ ? std::nullopt : std::optional<SpanIt>{ (*it_)->begin() };
                }

                return *this;
            }

            /** @brief Moves to the previous byte, crossing slice boundaries as needed. */
            constexpr Iterator& operator--() noexcept
            {
                if (!span_it_.has_value() || --*span_it_ == std::prev((*it_)->begin())) {
                    span_it_ = std::optional<SpanIt>{ std::prev((*--it_)->end()) };
                }

                return *this;
            }

            /** @return `true` when both iterators identify the same flattened position. */
            friend constexpr bool operator==(const Iterator& lhs, const Iterator& rhs)
            {
                return lhs.it_ == rhs.it_ && lhs.span_it_ == rhs.span_it_;
            }

            /** @return `true` when the iterators identify different flattened positions. */
            friend constexpr bool operator!=(const Iterator& lhs, const Iterator& rhs) { return !(lhs == rhs); }

          private:
            It it_;
            It end_it_;
            std::optional<SpanIt> span_it_;
        };

      protected:
        /** @brief Shared ownership of one contiguous byte slice. */
        using SliceType = std::shared_ptr<std::vector<uint8_t, Allocator>>;

        /** @brief Sequence of shared slices. */
        using BufferType = std::deque<SliceType>;

        /** @brief Constructs empty storage; use `Create()` from callers. */
        DataStorage() = default;

        /**
         * @brief Constructs storage containing one shared slice.
         * @param slice Slice to retain.
         * @pre `slice` is non-null and nonempty before any flattened iteration or first/last access.
         */
        DataStorage(SliceType slice)
          : buffer_{ std::move(slice) }
        {
        }

      public:
        /** @return Shared ownership of newly allocated empty storage, or terminates on allocation failure. */
        static std::shared_ptr<DataStorage> Create() noexcept
        {
            return std::shared_ptr<DataStorage>(new DataStorage());
        }

        /**
         * @param slice Initial shared slice.
         * @return Shared ownership of newly allocated storage, or terminates on allocation failure.
         * @pre `slice` is non-null and nonempty before iteration or first/last access.
         */
        static std::shared_ptr<DataStorage> Create(SliceType slice) noexcept
        {
            return std::shared_ptr<DataStorage>(new DataStorage(std::move(slice)));
        }

        /** @return `true` if there are no slices; zero-length slices still make this `false`. */
        bool Empty() const noexcept { return buffer_.empty(); }

        /**
         * @return Intended to return the first shared slice.
         * @warning The current definition dereferences the flattened byte iterator and is ill-formed if instantiated.
         */
        const SliceType& First() const noexcept { return *(this->begin()); }

        /**
         * @return Intended to return the last shared slice.
         * @warning The current definition dereferences the flattened byte iterator and is ill-formed if instantiated.
         */
        const SliceType& Last() const noexcept { return *std::prev(this->end()); }

        /**
         * @return Intended to return a shared-ownership copy of the last slice.
         * @warning The current definition dereferences the flattened byte iterator and is ill-formed if instantiated.
         */
        const SliceType GetLast() const noexcept { return *std::prev(this->end()); }

        /**
         * @brief Copies bytes into a new owned slice at the back.
         * @param bytes Source view, valid for the duration of the call.
         * @throws Any allocation exception.
         * @note An empty span creates an empty slice, which flattened iteration does not support.
         */
        void Push(std::span<const uint8_t> bytes)
        {
            auto slice = std::make_shared<typename SliceType::element_type>();
            slice->assign(bytes.begin(), bytes.end());

            buffer_.push_back(std::move(slice));
        }

        /**
         * @brief Appends a shared slice without copying its bytes.
         * @param slice Slice whose ownership is moved into the storage.
         * @pre `slice` is non-null and nonempty before flattened iteration.
         */
        void Push(SliceType slice) { buffer_.push_back(std::move(slice)); }

        /**
         * @brief Appends a copied byte span.
         * @param buffer Destination storage.
         * @param value Bytes to copy.
         * @return `buffer`.
         */
        friend DataStorage& operator<<(DataStorage& buffer, std::span<const uint8_t> value)
        {
            buffer.Push(value);
            return buffer;
        }

        // NOLINTBEGIN(readability-identifier-naming)
        /** @brief Mutable flattened byte iterator type. */
        using iterator = Iterator<typename BufferType::iterator, typename SliceType::element_type::iterator>;

        /** @brief Read-only flattened byte iterator type. */
        using const_iterator =
          Iterator<typename BufferType::const_iterator, typename SliceType::element_type::const_iterator>;

        /** @return Total byte count across all slices. @pre Every slice is non-null. */
        std::size_t size() const noexcept
        {
            std::size_t value = 0;
            for (const auto& buf : buffer_) {
                value += buf->size();
            }
            return value;
        }

        /**
         * @brief Removes complete slices from the front whose cumulative size does not exceed `len`.
         * @param len Maximum byte count considered for removal.
         * @return Remaining byte count when the next slice is only partially covered; zero when all requested bytes
         * were consumed or a null slice is encountered. Slices are never partially erased.
         * @warning Any storage iterator or view position at/after removed slices may be invalidated.
         */
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

        /** @return Iterator to the first byte, or `end()` when there are no slices. */
        iterator begin() noexcept { return iterator(buffer_.begin(), buffer_.end()); }

        /** @return End iterator. */
        iterator end() noexcept { return iterator(buffer_.end(), buffer_.end()); }

        /** @return Const iterator to the first byte, or `end()` when there are no slices. */
        const_iterator begin() const noexcept { return const_iterator(buffer_.begin(), buffer_.end()); }

        /** @return Const end iterator. */
        const_iterator end() const noexcept { return const_iterator(buffer_.end(), buffer_.end()); }

        /** @return Const iterator to the first byte, or `cend()` when there are no slices. */
        const_iterator cbegin() const noexcept { return const_iterator(buffer_.begin(), buffer_.end()); }

        /** @return Const end iterator. */
        const_iterator cend() const noexcept { return const_iterator(buffer_.end(), buffer_.end()); }
        // NOLINTEND(readability-identifier-naming)

      private:
        BufferType buffer_;
    };

    /**
     * @brief Non-owning-position view that shares ownership of a `DataStorage<>`.
     * @details Positions are absolute flattened byte offsets into the underlying storage. The storage object remains
     * alive with the view, but mutation is not synchronized and front erasure changes what offsets identify.
     */
    class DataStorageDynView
    {
      public:
        /**
         * @brief Constructs a view over an absolute half-open range.
         * @param storage Storage retained by shared ownership.
         * @param start_pos Absolute inclusive byte offset.
         * @param end_pos Optional absolute exclusive byte offset; no value tracks the storage's current end.
         * @pre `storage` is non-null, `start_pos` is within its flattened size, and a supplied `end_pos` is between
         * `start_pos` and the storage size.
         */
        DataStorageDynView(std::shared_ptr<DataStorage<>> storage,
                           std::size_t start_pos = 0,
                           std::optional<std::size_t> end_pos = std::nullopt)
          : storage_(std::move(storage))
          , start_pos_(start_pos)
          , end_pos_(end_pos)
        {
        }

        /** @brief Copies the view and shares ownership of its storage. @param other View to copy. */
        DataStorageDynView(const DataStorageDynView& other) = default;

        /** @brief Moves the view. @param other View to move. */
        DataStorageDynView(DataStorageDynView&& other) = default;

        /**
         * @brief Copies the view and shares ownership of its storage.
         * @param other View to copy.
         * @return `*this`.
         */
        DataStorageDynView& operator=(const DataStorageDynView& other) = default;

        /** @brief Moves the view. @param other View to move. @return `*this`. */
        DataStorageDynView& operator=(DataStorageDynView&& other) = default;

        /**
         * @brief Creates another absolute view of the same storage.
         * @param start_pos Absolute inclusive storage offset, not relative to this view.
         * @param end_pos Optional absolute exclusive offset; absent or beyond the current storage size is clamped to
         * the current storage size.
         * @return View with a fixed end position.
         * @pre `start_pos` does not exceed the selected end position.
         */
        DataStorageDynView Subspan(std::size_t start_pos,
                                   std::optional<std::size_t> end_pos = std::nullopt) const noexcept
        {
            if (not end_pos.has_value() || *end_pos > storage_->size()) {
                end_pos = storage_->size();
            }

            return DataStorageDynView(storage_, start_pos, *end_pos);
        }

        // NOLINTBEGIN(readability-identifier-naming)
        /** @return Current view length. A dynamic end reflects subsequent appends to storage. */
        std::size_t size() const noexcept { return (end_pos_.has_value() ? *end_pos_ : storage_->size()) - start_pos_; }

        /** @return Mutable iterator at the absolute start offset. */
        DataStorage<>::iterator begin() noexcept { return std::next(storage_->begin(), start_pos_); }

        /** @return Mutable iterator at the current exclusive end offset. */
        DataStorage<>::iterator end() noexcept { return std::next(this->begin(), size()); }

        /** @return Const iterator at the absolute start offset. */
        DataStorage<>::const_iterator begin() const noexcept { return std::next(storage_->cbegin(), start_pos_); }

        /** @return Const iterator at the current exclusive end offset. */
        DataStorage<>::const_iterator end() const noexcept { return std::next(this->begin(), size()); }
        // NOLINTEND(readability-identifier-naming)

      private:
        std::shared_ptr<DataStorage<>> storage_;
        std::size_t start_pos_ = 0;
        std::optional<std::size_t> end_pos_;
    };
}
