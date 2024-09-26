// SPDX-FileCopyrightText: Copyright (c) 2024 Cisco Systems
// SPDX-License-Identifier: BSD-2-Clause

#include <doctest/doctest.h>

#include "quicr/detail/uintvar.h"

namespace var {

    // integer values for valdiation tests
    constexpr uint64_t kValue1Byte = 0x12;
    constexpr uint64_t kValue2Byte = 0x1234;
    constexpr uint64_t kValue4Byte = 0x123456;
    constexpr uint64_t kValue8Byte = 0x123456789;

    // Encoded values of the above for validation tests
    const std::vector<uint8_t> kValue1ByteEncoded{ 0x12 };
    const std::vector<uint8_t> kValue2ByteEncoded{ 0x52, 0x34 };
    const std::vector<uint8_t> kValue4ByteEncoded{ 0x80, 0x12, 0x34, 0x56 };
    const std::vector<uint8_t> kValue8ByteEncoded = { 0xC0, 0, 0, 0x1, 0x23, 0x45, 0x67, 0x89 };
}

TEST_CASE("Encode/Decode UintVar Uint64")
{
    CHECK_EQ(var::kValue1Byte, uint64_t(quicr::UintVar(var::kValue1Byte)));
    CHECK_EQ(var::kValue2Byte, uint64_t(quicr::UintVar(var::kValue2Byte)));
    CHECK_EQ(var::kValue4Byte, uint64_t(quicr::UintVar(var::kValue4Byte)));
    CHECK_EQ(var::kValue8Byte, uint64_t(quicr::UintVar(var::kValue8Byte)));
}

TEST_CASE("Encode/Decode UintVar Bytes")
{
    CHECK_EQ(var::kValue1Byte, uint64_t(quicr::UintVar(Span{ quicr::UintVar(var::kValue1Byte) })));
    CHECK_EQ(var::kValue2Byte, uint64_t(quicr::UintVar(Span{ quicr::UintVar(var::kValue2Byte) })));
    CHECK_EQ(var::kValue4Byte, uint64_t(quicr::UintVar(Span{ quicr::UintVar(var::kValue4Byte) })));
    CHECK_EQ(var::kValue8Byte, uint64_t(quicr::UintVar(Span{ quicr::UintVar(var::kValue8Byte) })));
}

TEST_CASE("Length of UintVar")
{
    CHECK_EQ(1, quicr::UintVar(var::kValue1Byte).Size());
    CHECK_EQ(2, quicr::UintVar(var::kValue2Byte).Size());
    CHECK_EQ(4, quicr::UintVar(var::kValue4Byte).Size());
    CHECK_EQ(8, quicr::UintVar(var::kValue8Byte).Size());

    CHECK_EQ(1, quicr::UintVar::Size(*quicr::UintVar(var::kValue1Byte).begin()));
    CHECK_EQ(2, quicr::UintVar::Size(*quicr::UintVar(var::kValue2Byte).begin()));
    CHECK_EQ(4, quicr::UintVar::Size(*quicr::UintVar(var::kValue4Byte).begin()));
    CHECK_EQ(8, quicr::UintVar::Size(*quicr::UintVar(var::kValue8Byte).begin()));
}

TEST_CASE("Validate UintVar from Known UintVar Bytes")
{
    const auto v_one_byte = quicr::UintVar(var::kValue1Byte);
    const auto v_two_byte = quicr::UintVar(var::kValue2Byte);
    const auto v_four_byte = quicr::UintVar(var::kValue4Byte);
    const auto v_eight_byte = quicr::UintVar(var::kValue8Byte);

    CHECK_EQ(var::kValue1Byte, uint64_t(quicr::UintVar(var::kValue1ByteEncoded)));
    CHECK_EQ(var::kValue2Byte, uint64_t(quicr::UintVar(var::kValue2ByteEncoded)));
    CHECK_EQ(var::kValue4Byte, uint64_t(quicr::UintVar(var::kValue4ByteEncoded)));
    CHECK_EQ(var::kValue8Byte, uint64_t(quicr::UintVar(var::kValue8ByteEncoded)));

    CHECK(std::vector<uint8_t>(v_one_byte.begin(), v_one_byte.end()) == var::kValue1ByteEncoded);
    CHECK(std::vector<uint8_t>(v_two_byte.begin(), v_two_byte.end()) == var::kValue2ByteEncoded);
    CHECK(std::vector<uint8_t>(v_four_byte.begin(), v_four_byte.end()) == var::kValue4ByteEncoded);
    CHECK(std::vector<uint8_t>(v_eight_byte.begin(), v_eight_byte.end()) == var::kValue8ByteEncoded);
}

TEST_CASE("UintVar Invalid Construction")
{
    CHECK_THROWS(quicr::UintVar(std::numeric_limits<uint64_t>::max()));
    CHECK_THROWS(quicr::UintVar(std::vector<uint8_t>{}));
    CHECK_THROWS(quicr::UintVar(std::vector<uint8_t>(sizeof(uint64_t) + 1)));
    CHECK_THROWS(quicr::UintVar(std::vector<uint8_t>{ 0xFF, 0xFF }));
}
