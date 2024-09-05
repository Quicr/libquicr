// SPDX-FileCopyrightText: Copyright (c) 2024 Cisco Systems
// SPDX-License-Identifier: BSD-2-Clause

#include "moq/detail/serializer.h"

namespace moq {
    Serializer::Serializer(std::size_t reserve_size)
    {
        buffer_.reserve(reserve_size);
    }

    void Serializer::Push(Byte data)
    {
        buffer_.push_back(std::move(data));
    }

    void Serializer::Push(BytesSpan data)
    {
        buffer_.insert(buffer_.end(), data.begin(), data.end());
    }

    void Serializer::PushLengthBytes(BytesSpan data)
    {
        const auto len = ToUintV(static_cast<uint64_t>(data.size()));
        Push(len);
        Push(data);
    }
}
