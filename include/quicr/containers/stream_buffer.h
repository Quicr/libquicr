// SPDX-FileCopyrightText: Copyright (c) 2024 Cisco Systems
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

#include "quicr/messages/uintvar.h"

#include <algorithm>
#include <any>
#include <cstring>
#include <optional>
#include <span>
#include <type_traits>
#include <vector>

namespace quicr {
#ifdef _MSC_VER
/** @brief Requests compiler-specific forced inlining for internal stream-buffer helpers. */
#define FORCE_INLINE inline __forceinline
#else
/** @brief Requests compiler-specific forced inlining for internal stream-buffer helpers. */
#define FORCE_INLINE inline __attribute__((always_inline))
#endif

    /**
     * @brief Contiguous append-only stream accumulator with a logical read offset and parser scratch state.
     * @tparam T Element type; QUIC variable-length decoding requires a byte-compatible integral type.
     * @tparam Allocator Allocator for the underlying vector.
     * @details Pop operations normally advance a logical offset. A later push compacts consumed storage once the
     * offset reaches the configured threshold or exceeds the unread size. The class is not thread-safe. Spans and
     * references returned by accessors are invalidated by compaction, reallocation, clear, or destruction.
     */
    template<typename T, class Allocator = std::allocator<T>>
    class StreamBuffer
    {
        using BufferT = std::vector<T, Allocator>;

      public:
        /**
         * @brief Constructs an empty stream buffer.
         * @param compact_threshold Minimum consumed prefix that can trigger compaction on a later push.
         */
        StreamBuffer(std::size_t compact_threshold = 4096)
          : compact_threshold_(compact_threshold)
        {
        }

        /**
         * @brief Initialize the parsed data
         * @details Parsed data allows the caller to work on reading data from the
         *        stream buffer. The datatype is any to support the caller data types.
         *        This method will initialize the parsed data using the type specified
         * @tparam D Data type for value
         * @throws Any exception thrown by constructing `D`.
         */
        template<typename D>
        void InitAny()
        {
            parsed_data_.emplace<D>();
        }

        /**
         * @brief Initializes the secondary parser scratch object as `D`.
         * @tparam D Type constructed in place.
         * @throws Any exception thrown by constructing `D`.
         */
        template<typename D>
        void InitAnyB()
        {
            parsed_dataB_.emplace<D>();
        }

        /**
         * @brief Initialize the parsed data and type
         * @tparam D Data type for value
         * @param type user defined type value for the parsed data any object
         * @throws Any exception thrown by constructing `D`.
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
         * @tparam D Data type of value
         * @return Mutable reference to the primary parser scratch object.
         * @throws std::bad_any_cast If the stored object is not exactly `D`.
         */
        template<typename D>
        D& GetAny()
        {
            return std::any_cast<D&>(parsed_data_);
        }

        /**
         * @brief Gets the secondary parser scratch object.
         * @tparam D Exact stored type.
         * @return Mutable reference valid until the secondary object is reset or the buffer is destroyed.
         * @throws std::bad_any_cast If the stored object is not exactly `D`.
         */
        template<typename D>
        D& GetAnyB()
        {
            return std::any_cast<D&>(parsed_dataB_);
        }

        /**
         * @brief Get the user defined parsed type value
         * @return Parsed data type value, or `std::nullopt` if none was set.
         */
        std::optional<uint64_t> GetAnyType() { return parsed_data_type_; }

        /**
         * @brief Set the user-defined parsed data type value
         * @param type User defined value for the data type
         */
        void SetAnyType(uint64_t type) { parsed_data_type_ = type; }

        /**
         * @brief Clears unread data and all parser scratch state.
         * @note Retained vector capacity is implementation-dependent; all returned spans are invalidated.
         */
        void Clear()
        {
            ResetAny();
            buffer_.clear();
            read_offset_ = 0;
        }

        /** @brief Clears both parser scratch objects and the optional parser type without changing stream data. */
        void ResetAny()
        {
            parsed_data_.reset();
            parsed_dataB_.reset();
            parsed_data_type_ = std::nullopt;
        }

        /** @brief Clears only the secondary parser scratch object. */
        void ResetAnyB() { parsed_dataB_.reset(); }

        /**
         * @brief Replaces the secondary parser scratch object with a default-constructed `D`.
         * @tparam D New scratch type.
         * @throws Any exception thrown by constructing `D`.
         */
        template<class D>
        void ResetAnyB()
        {
            ResetAnyB();
            InitAnyB<D>();
        }

        /** @return Whether the primary parser scratch object contains a value. */
        bool AnyHasValue() { return parsed_data_.has_value(); }

        /** @return Whether the secondary parser scratch object contains a value. */
        bool AnyHasValueB() { return parsed_dataB_.has_value(); }

        /** @return `true` when no unread elements remain. */
        bool Empty() const noexcept { return read_offset_ >= buffer_.size(); }

        /** @return Number of unread elements. */
        size_t Size() noexcept { return read_offset_ >= buffer_.size() ? 0 : buffer_.size() - read_offset_; }

        /**
         * @brief Get the first data byte in stream buffer
         * @returns One-element span of the first unread value, or an empty span if no data is available.
         */
        std::span<const T> Front() noexcept
        {
            if (Empty()) {
                return {};
            }

            return { buffer_.data() + read_offset_, 1 };
        }

        /**
         * @brief Front length number of data bytes
         *
         * @param length Get the first up to length number of data bytes
         *
         * @returns Span of exactly `length` unread elements, or an empty span if fewer are available.
         * @note A zero length request returns an empty span regardless of availability.
         */
        std::span<const T> Front(std::size_t length) noexcept
        {
            if (Empty()) {
                return {};
            }

            const auto logical_size = buffer_.size() - read_offset_;
            return logical_size < length ? std::span<const T>{}
                                         : std::span<const T>{ buffer_.data() + read_offset_, length };
        }

        /**
         * @brief All readable data bytes in the stream buffer
         *
         * @returns Span of all unread elements, or an empty span.
         */
        std::span<const T> Data() noexcept
        {
            if (Empty()) {
                return {};
            }

            return { buffer_.data() + read_offset_, buffer_.size() - read_offset_ };
        }

        /**
         * @brief Surrender everything not yet read, leaving the buffer empty
         *
         * @details For a caller that takes the whole of what has arrived rather than reading
         *      messages out of it one at a time, which costs nothing once it has read what came
         *      before. What has been parsed so far is left alone, since that is what says how the
         *      stream is being read rather than what is left to read.
         *
         * @returns The bytes not yet read
         * @details When no prefix has been consumed this may transfer the underlying allocation; otherwise it copies
         * unread elements. Parser scratch state is preserved. All previously returned spans are invalidated.
         * @throws Any exception thrown while allocating or copying unread elements.
         */
        BufferT TakeAll()
        {
            BufferT taken;
            if (read_offset_ == 0) {
                taken = std::move(buffer_);
            } else {
                taken.assign(buffer_.begin() + static_cast<std::ptrdiff_t>(read_offset_), buffer_.end());
            }

            buffer_.clear();
            read_offset_ = 0;

            return taken;
        }

        /** @brief Consumes one unread element, or does nothing when empty. */
        void Pop()
        {
            if (Empty()) {
                return;
            }

            PopInternal(1);
        }

        /**
         * @brief Consumes up to `length` unread elements.
         * @param length Requested count; zero does nothing. A count at least as large as the unread size empties the
         * buffer.
         */
        void Pop(std::size_t length)
        {
            if (length == 0 || Empty()) {
                return;
            }

            PopInternal(length);
        }

        /**
         * @brief Checks if length bytes are available for front
         *
         * @param length length of bytes needed
         *
         * @return True if data length is available, false if not.
         */
        bool Available(std::size_t length) const noexcept
        {
            return length == 0 || (read_offset_ < buffer_.size() && buffer_.size() - read_offset_ >= length);
        }

        /**
         * @brief Copies one value to the back of the stream.
         * @param value Value to append.
         * @throws Any exception thrown by compaction, allocation, or copying.
         */
        void Push(const T& value)
        {
            CompactFrontIfNeeded();

            buffer_.push_back(value);
        }

        /**
         * @brief Moves one value to the back of the stream.
         * @param value Value to append.
         * @throws Any exception thrown by compaction, allocation, or moving.
         */
        void Push(T&& value)
        {
            CompactFrontIfNeeded();

            buffer_.push_back(std::move(value));
        }

        /**
         * @brief Appends a copy of a contiguous value range.
         * @param value Source values.
         * @pre `value` does not alias this buffer's storage, because compaction or reallocation can invalidate it.
         * @throws Any exception thrown by compaction, allocation, or copying.
         */
        void Push(std::span<const T> value)
        {
            CompactFrontIfNeeded();

            buffer_.insert(buffer_.end(), value.begin(), value.end());
        }

        /**
         * @brief Appends a QUIC variable-length count followed by the supplied values.
         * @param value Values whose element count is encoded first.
         * @pre `T` can represent the encoded bytes and `value` does not alias this buffer's storage.
         * @throws Any exception thrown by `UintVar`, compaction, allocation, or copying.
         */
        void PushLengthBytes(std::span<const T> value)
        {
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

            const auto& uv_msb = buffer_[read_offset_];
            std::size_t uv_len = UintVar::Size(uv_msb);

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
         * @note The length prefix and payload are consumed only when the complete payload is available.
         * @throws Any exception thrown while constructing the result vector.
         */
        std::optional<std::vector<uint8_t>> DecodeBytes()
        {
            if (Empty()) {
                return std::nullopt;
            }

            const auto& uv_msb = buffer_[read_offset_];
            std::size_t uv_len = UintVar::Size(uv_msb);

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

        FORCE_INLINE void PopInternal(std::size_t length)
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
        std::any parsed_data_;                     ///< Working buffer for parsed data
        std::any parsed_dataB_;                    ///< Second Working buffer for parsed data
        std::optional<uint64_t> parsed_data_type_; ///< working buffer type value
        std::size_t read_offset_{ 0 };
        std::size_t compact_threshold_{ 4096 };
    };

#undef FORCE_INLINE
}
