// SPDX-FileCopyrightText: Copyright (c) 2024 Cisco Systems
// SPDX-License-Identifier: BSD-2-Clause

#include <doctest/doctest.h>

#include "quicr/data_storage.h"

TEST_CASE("DataStorage Construct")
{
    CHECK_NOTHROW(quicr::DataStorage<>::Create());

    auto storage = quicr::DataStorage<>::Create();
    CHECK_NOTHROW(quicr::DataStorage<>::Create());
}

TEST_CASE("DataStorage Push")
{
    auto buffer = quicr::DataStorage<>::Create();

    uint64_t value = 0;
    auto bytes = quicr::AsBytes(value);
    CHECK_NOTHROW(buffer->Push(bytes));
}

TEST_CASE("DataStorage Read")
{
    auto buffer = quicr::DataStorage<>::Create();

    uint64_t value = 0x0102030405060708;
    CHECK_NOTHROW(buffer->Push(quicr::AsBytes(value)));

    std::vector<uint8_t> v(buffer->begin(), buffer->end());

    CHECK_EQ(v.size(), 8);
    CHECK_EQ(v, std::vector<uint8_t>{ 0x08, 0x07, 0x06, 0x05, 0x04, 0x03, 0x02, 0x01 });
}

TEST_CASE("DataStorage Multiples")
{
    auto buffer = quicr::DataStorage<>::Create();

    std::string s1 = "one";
    std::string s2 = " two";
    std::string s3 = " three";

    buffer->Push(quicr::AsBytes(s1));
    buffer->Push(quicr::AsBytes(s2));
    buffer->Push(quicr::AsBytes(s3));

    std::vector<uint8_t> v(buffer->begin(), buffer->end());

    CHECK_EQ(v.size(), s1.size() + s2.size() + s3.size());
    CHECK_EQ(v.at(5), 'w');
}
