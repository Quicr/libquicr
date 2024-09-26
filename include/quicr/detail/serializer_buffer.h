// SPDX-FileCopyrightText: Copyright (c) 2024 Cisco Systems
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

#include "quicr/common.h"
#include "quicr/detail/span.h"

#include <cstdint>
#include <type_traits>
#include <vector>

namespace quicr {
    class SerialBuffer
    {
      public:
        SerialBuffer() = default;
        SerialBuffer(std::size_t reserve_size) { buffer_.reserve(reserve_size); }

        Bytes&& Take() noexcept { return std::move(buffer_); }

        void Push(Byte data) { buffer_.push_back(std::move(data)); }
        void Push(BytesSpan data) { buffer_.insert(buffer_.end(), data.begin(), data.end()); }
        void PushLengthBytes(BytesSpan data)
        {
            Push(UintVar(static_cast<uint64_t>(data.size())));
            Push(data);
        }

        void Clear() noexcept { return buffer_.clear(); }

        inline SerialBuffer& operator<<(Byte value)
        {
            Push(std::move(value));
            return *this;
        }

        template<typename T, typename std::enable_if_t<std::is_trivially_copyable_v<T>, bool> = true>
        inline SerialBuffer& operator<<(T value)
        {
            if constexpr (std::is_integral_v<T>) {
                value = SwapBytes(value);
            }

            const auto length = buffer_.size();
            buffer_.resize(length + sizeof(T));
            std::memcpy(buffer_.data() + length, &value, sizeof(T));

            return *this;
        }

        // NOLINTBEGIN(readability-identifier-naming)
        auto data() const noexcept { return buffer_.data(); }
        std::size_t size() const noexcept { return buffer_.size(); }
        auto begin() const noexcept { return buffer_.begin(); }
        auto end() const noexcept { return buffer_.end(); }
        // NOLINTEND(readability-identifier-naming)

      private:
        Bytes buffer_;
    };
}
