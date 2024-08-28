#pragma once

#include <algorithm>
#include <any>
#include <deque>
#include <mutex>
#include <optional>
#include <transport/span.h>

#include <transport/uintvar.h>

namespace qtransport {
    template<typename T, class Allocator = std::allocator<T>>
    class StreamBuffer
    {
        using BufferT = std::deque<T, Allocator>;

      public:
        StreamBuffer() = default;

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

        void ResetAny()
        {
            parsed_data_.reset();
            parsed_dataB_.reset();
            parsed_data_type_ = std::nullopt;
        }

        void ResetAnyB()
        {
            parsed_dataB_.reset();
        }

        bool AnyHasValue()
        {
            return parsed_data_.has_value();
        }

        bool AnyHasValueB()
        {
            return parsed_dataB_.has_value();
        }

        bool Empty() const noexcept { return buffer_.empty(); }

        size_t Size() noexcept { return buffer_.size(); }

        /**
         * @brief Get the first data byte in stream buffer
         * @returns data byt or nullopt if no data
         */
        std::optional<T> Front() noexcept
        {
            if (buffer_.size()) {
                std::lock_guard<std::mutex> _(rwLock_);
                return buffer_.front();
            }

            return std::nullopt;
        }

        /**
         * @brief Front length number of data bytes
         *
         * @param length            Get the first up to length number of data bytes
         *
         * @returns data vector of bytes or nullopt if no data
         */
        std::vector<T> Front(std::uint32_t length) noexcept
        {

            if (!buffer_.empty()) {
                std::lock_guard<std::mutex> _(rwLock_);

                std::vector<T> result(length);
                std::copy_n(buffer_.begin(), length, result.begin());
                return result;
            }
            return std::vector<T>();
        }

        void Pop()
        {
            if (buffer_.size()) {
                std::lock_guard<std::mutex> _(rwLock_);
                buffer_.pop_front();
            }
        }

        void Pop(std::uint32_t length)
        {
            if (!length || buffer_.empty())
                return;

            std::lock_guard<std::mutex> _(rwLock_);

            if (length >= buffer_.size()) {
                buffer_.clear();
            } else {
                buffer_.erase(buffer_.begin(), buffer_.begin() + length);
            }
        }

        /**
         * @brief Checks if lenght bytes are avaialble for front
         *
         * @param length        length of bytes needed
         *
         * @return True if data length is available, false if not.
         */
        bool Available(std::uint32_t length) const noexcept { return buffer_.size() >= length; }

        void Push(const T& value)
        {
            std::lock_guard<std::mutex> _(rwLock_);
            buffer_.push_back(value);
        }

        void Push(T&& value)
        {
            std::lock_guard<std::mutex> _(rwLock_);
            buffer_.push_back(std::move(value));
        }

        void Push(const Span<const T>& value)
        {
            std::lock_guard<std::mutex> _(rwLock_);
            buffer_.insert(buffer_.end(), value.begin(), value.end());
        }

        void Push(std::initializer_list<T> value)
        {
            std::lock_guard<std::mutex> _(rwLock_);
            buffer_.insert(buffer_.end(), value.begin(), value.end());
        }

        void PushLv(const Span<const T>& value)
        {
            std::lock_guard<std::mutex> _(rwLock_);
            const auto len = ToUintV(static_cast<uint64_t>(value.size()));
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
         *
         * @return Returns uint64 decoded value or nullopt if not enough bytes are available
         */
        std::optional<uint64_t> DecodeUintV()
        {
            if (const auto uv_msb = Front()) {
                if (Available(UintVSize(*uv_msb))) {
                    uint64_t uv_len = UintVSize(*uv_msb);
                    auto val = ToUint64(Front(uv_len));

                    Pop(uv_len);

                    return val;
                }
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
            if (const auto uv_msb = Front()) {
                if (Available(UintVSize(*uv_msb))) {
                    uint64_t uv_len = UintVSize(*uv_msb);
                    auto len = ToUint64(Front(uv_len));

                    if (buffer_.size() >= uv_len + len) {
                        Pop(uv_len);
                        auto v = Front(len);
                        Pop(len);

                        return v;
                    }
                }
            }

            return std::nullopt;
        }

      private:
        BufferT buffer_;
        std::mutex rwLock_;
        std::any parsed_data_; /// Working buffer for parsed data
        std::any parsed_dataB_; /// Second Working buffer for parsed data
        std::optional<uint64_t> parsed_data_type_; /// working buffer type value
    };
}
