// SPDX-FileCopyrightText: Copyright (c) 2024 Cisco Systems
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

#include "quicr/common.h"
#include "quicr/detail/span.h"

#include <cstdint>
#include <type_traits>
#include <vector>

namespace quicr {
    class Serializer
    {
      public:
        Serializer() = default;
        Serializer(std::size_t reserve_size);

        BytesSpan View() const noexcept { return buffer_; }
        Bytes&& Take() noexcept { return std::move(buffer_); }

        void Push(Byte data);
        void Push(BytesSpan data);
        void PushLengthBytes(BytesSpan data);
        void Clear() noexcept { return buffer_.clear(); }

        inline Serializer& operator<<(Byte value)
        {
            Push(std::move(value));
            return *this;
        }

        template<typename T, typename std::enable_if_t<std::is_standard_layout_v<T>, bool> = true>
        inline Serializer& operator<<(T value)
        {
            if constexpr (std::is_integral_v<T>) {
                value = SwapBytes(value);
            }

            const auto length = buffer_.size();
            buffer_.resize(length + sizeof(T));
            std::memcpy(buffer_.data() + length, &value, sizeof(T));

            return *this;
        }

      private:
        Bytes buffer_;
    };
}
