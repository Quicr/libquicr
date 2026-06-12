// SPDX-FileCopyrightText: Copyright (c) 2024 Cisco Systems
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

#include "data_storage.h"
#include "uintvar.h"
#include <span>

#include <algorithm>
#include <any>
#include <cstring>
#include <mutex>
#include <optional>
#include <type_traits>
#include <vector>

namespace quicr {
#define FORCE_INLINE inline __attribute__((always_inline))

    struct NullMutex
    {
        constexpr void lock() {}
        constexpr void unlock() {}
        constexpr bool try_lock() { return true; }
    };

    template<typename T, class Mutex = NullMutex, class Allocator = std::allocator<T>>
    class StreamBuffer
    {
        using BufferT = std::vector<T, Allocator>;

      public:
        StreamBuffer(std::size_t compact_threshold = 4096)
          : compact_threshold_(compact_threshold)
        {
        }

        /**
         * @brief Initialize the parsed data
         * @details Parsed data allows the caller to work on reading data from the
         *        stream buffer. The datatype is any to support the caller data types.
         *        This method will initialize the parsed data using the type specified
         * @tparam D              Data type for value
         */
        template<typename D>
        void InitAny()
        {
            parsed_data_.emplace<D>();
        }

        template<typename D>
        void InitAnyB()
        {
            parsed_dataB_.emplace<D>();
        }

        /**
         * @brief Initialize the parsed data and type
         * @tparam D              Data type for value
         * @param type            user defined type value for the parsed data any object
         */
        template<typename D>
        void InitAny(uint64_t type)
        {
            parsed_data_.emplace<D>();
            parsed_data_type_ = type;
        }

        /**
         * @brief Get the parsed data
         * @details Parsed data allows the caller to work on reading data from the
         *        stream buffer. The datatype is any to support the caller data types.
         *        This returns a reference to the any variable cast to the data type
         *
         * @tparam D              Data type of value
         */
        template<typename D>
        D& GetAny()
        {
            return std::any_cast<D&>(parsed_data_);
        }

        template<typename D>
        D& GetAnyB()
        {
            return std::any_cast<D&>(parsed_dataB_);
        }

        /**
         * @brief Get the user defined parsed type value
         * @return Parsed data type value that was set via initAny(). nullopt if not set
         */
        std::optional<uint64_t> GetAnyType() { return parsed_data_type_; }

        /**
         * @brief Set the user-defined parsed data type value
         * @param type          User defined value for the data type
         */
        void SetAnyType(uint64_t type) { parsed_data_type_ = type; }

        void Clear()
        {
            ResetAny();
            buffer_.clear();
            read_offset_ = 0;
        }

        void ResetAny()
        {
            parsed_data_.reset();
            parsed_dataB_.reset();
            parsed_data_type_ = std::nullopt;
        }

        void ResetAnyB() { parsed_dataB_.reset(); }

        template<class D>
        void ResetAnyB()
        {
            ResetAnyB();
            InitAnyB<D>();
        }

        bool AnyHasValue() { return parsed_data_.has_value(); }

        bool AnyHasValueB() { return parsed_dataB_.has_value(); }

        bool Empty() const noexcept { return read_offset_ >= buffer_.size(); }

        size_t Size() noexcept { return read_offset_ >= buffer_.size() ? 0 : buffer_.size() - read_offset_; }

        /**
         * @brief Get the first data byte in stream buffer
         * @returns span of the first byte, or empty span if no data
         */
        std::span<const T> Front() noexcept
        {
            if (Empty()) {
                return {};
            }

            std::lock_guard _(rw_lock_);
            return { buffer_.data() + read_offset_, 1 };
        }

        /**
         * @brief Front length number of data bytes
         *
         * @param length            Get the first up to length number of data bytes
         *
         * @returns span of bytes, or empty span if no data or length unavailable
         */
        std::span<const T> Front(std::uint32_t length) noexcept
        {
            if (Empty()) {
                return {};
            }

            std::lock_guard _(rw_lock_);

            const auto logical_size = buffer_.size() - read_offset_;
            return logical_size < length ? std::span<const T>{}
                                         : std::span<const T>{ buffer_.data() + read_offset_, length };
        }

        void Pop()
        {
            if (Empty()) {
                return;
            }

            std::lock_guard _(rw_lock_);
            PopInternal(1);
        }

        void Pop(std::uint32_t length)
        {
            if (length == 0 || Empty()) {
                return;
            }

            std::lock_guard _(rw_lock_);
            PopInternal(length);
        }

        /**
         * @brief Checks if length bytes are available for front
         *
         * @param length        length of bytes needed
         *
         * @return True if data length is available, false if not.
         */
        bool Available(std::uint32_t length) const noexcept
        {
            return read_offset_ < buffer_.size() && buffer_.size() - read_offset_ >= length;
        }

        void Push(const T& value)
        {
            std::lock_guard _(rw_lock_);

            CompactFrontIfNeeded();

            buffer_.push_back(value);
        }

        void Push(T&& value)
        {
            std::lock_guard _(rw_lock_);

            CompactFrontIfNeeded();

            buffer_.push_back(std::move(value));
        }

        void Push(std::span<const T> value)
        {
            std::lock_guard _(rw_lock_);

            CompactFrontIfNeeded();

            buffer_.insert(buffer_.end(), value.begin(), value.end());
        }

        void PushLengthBytes(std::span<const T> value)
        {
            std::lock_guard _(rw_lock_);

            CompactFrontIfNeeded();

            UintVar len(static_cast<uint64_t>(value.size()));
            buffer_.insert(buffer_.end(), len.begin(), len.end());
            buffer_.insert(buffer_.end(), value.begin(), value.end());
        }

        /**
         * Decodes a variable length int (uintV) from start of stream buffer
         *
         * @details Reads uintV from stream buffer. If all bytes are available, the
         *      unsigned 64bit integer will be returned and the buffer
         *      will be moved past the uintV. Nullopt will be returned if not enough
         *      bytes are available.
         * @param pop True to move the buffer past the uintV.
         * @return Returns uint64 decoded value or nullopt if not enough bytes are available
         */
        std::optional<uint64_t> DecodeUintV(bool pop = true)
        {
            const auto uintv = ReadUintV(pop);
            if (uintv) {
                return uint64_t(*uintv);
            }
            return std::nullopt;
        }

        /**
         * Reads a variable length int (uintV) from start of stream buffer
         *
         * @details Reads uintV from stream buffer. If all bytes are available, the
         *      encoded uintV will be returned and the buffer
         *      will be moved past the uintV. Nullopt will be returned if not enough
         *      bytes are available.
         * @param pop True to move the buffer past the uintV.
         * @return Returns uintV or nullopt if not enough bytes are available
         */
        std::optional<UintVar> ReadUintV(bool pop = true)
        {
            if (Empty()) {
                return std::nullopt;
            }

            std::lock_guard _(rw_lock_);

            const auto& uv_msb = buffer_[read_offset_];
            uint64_t uv_len = UintVar::Size(uv_msb);

            const auto logical_size = buffer_.size() - read_offset_;
            if (logical_size >= uv_len) {
                auto val = UintVar(std::span<const T>{ buffer_.data() + read_offset_, uv_len });
                if (pop) {
                    PopInternal(uv_len);
                }

                return val;
            }

            return std::nullopt;
        }

        /**
         * Decodes a variable length array of uint8_t bytes from start of stream buffer
         *
         * @details Reads uintV from stream buffer to get the length of the byte array. Then
         *      reads byte array from stream buffer after the uintV length.  Vector of bytes
         *      will be returned if all bytes are available. Otherwise nullopt will be returned
         *      to indicate not enough bytes available.

         * @return Returns vector<uint8_t> or nullopt if not enough bytes are available
         */
        std::optional<std::vector<uint8_t>> DecodeBytes()
        {
            if (Empty()) {
                return std::nullopt;
            }

            std::lock_guard _(rw_lock_);

            const auto& uv_msb = buffer_[read_offset_];
            uint64_t uv_len = UintVar::Size(uv_msb);

            auto logical_size = buffer_.size() - read_offset_;
            if (logical_size >= uv_len) {
                auto len = UintVar(std::span<const T>{ buffer_.data() + read_offset_, uv_len });
                if (logical_size >= uv_len + uint64_t(len)) {
                    PopInternal(uv_len);
                    auto v = std::span<const T>{ buffer_.data() + read_offset_, uint64_t(len) };
                    auto bytes = std::vector<uint8_t>(v.begin(), v.end());
                    PopInternal(uint64_t(len));

                    return bytes;
                }
            }

            return std::nullopt;
        }

      private:
        FORCE_INLINE void CompactFrontIfNeeded()
        {

            if (read_offset_ == 0) {
                return;
            }

            const std::size_t logical_size = buffer_.size() - read_offset_;

            if (read_offset_ < compact_threshold_ && read_offset_ <= logical_size) {
                return;
            }

            if (read_offset_ >= buffer_.size()) {
                buffer_.clear();
                read_offset_ = 0;
                return;
            }

            if constexpr (std::is_trivially_copyable_v<T>) {
                std::memmove(buffer_.data(), buffer_.data() + read_offset_, logical_size * sizeof(T));
            } else {
                std::move(buffer_.begin() + static_cast<std::ptrdiff_t>(read_offset_), buffer_.end(), buffer_.begin());
            }

            buffer_.resize(logical_size);
            read_offset_ = 0;
        }

        FORCE_INLINE void PopInternal(std::uint32_t length)
        {
            if (read_offset_ >= buffer_.size() || length >= buffer_.size() - read_offset_) {
                buffer_.clear();
                read_offset_ = 0;
                return;
            }

            read_offset_ += length;
        }

      private:
        BufferT buffer_;
        Mutex rw_lock_;
        std::any parsed_data_;                     /// Working buffer for parsed data
        std::any parsed_dataB_;                    /// Second Working buffer for parsed data
        std::optional<uint64_t> parsed_data_type_; /// working buffer type value
        std::size_t read_offset_{ 0 };
        std::size_t compact_threshold_{ 4096 };
    };

    template<class T, class Allocator = std::allocator<T>>
    using SafeStreamBuffer = StreamBuffer<T, std::mutex, Allocator>;

#undef FORCE_INLINE
}
