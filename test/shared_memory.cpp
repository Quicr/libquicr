// SPDX-FileCopyrightText: Copyright (c) 2024 Cisco Systems
// SPDX-License-Identifier: BSD-2-Clause

#include <doctest/doctest.h>

#include "quicr/shared_memory.h"

TEST_CASE("SharedMemory Construct")
{
    CHECK_NOTHROW(quicr::SharedMemory::Create());
}

TEST_CASE("SharedMemory Push")
{
    auto buffer = quicr::SharedMemory::Create();

    uint64_t value = 0;
    auto bytes = quicr::AsBytes(value);
    CHECK_NOTHROW(buffer->Push(bytes));
}

TEST_CASE("SharedMemory Read")
{
    auto buffer = quicr::SharedMemory::Create();

    uint64_t value = 0x0102030405060708;
    auto bytes = quicr::AsBytes(value);
    CHECK_NOTHROW(buffer->Push(bytes));

    std::vector<uint8_t> v;
    for (auto value : *buffer)
    {
        v.push_back(value);
    }

    CHECK_EQ(v.size(), 8);
    CHECK_EQ(v, std::vector<uint8_t>{0x08, 0x07, 0x06, 0x05, 0x04, 0x03, 0x02, 0x01});
}
