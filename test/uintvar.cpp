#include <doctest/doctest.h>

#include "quicr/detail/uintvar.h"

namespace var {
    constexpr uint64_t kValue1Byte = 0x12;
    constexpr uint64_t kValue2Byte = 0x1234;
    constexpr uint64_t kValue4Byte = 0x123456;
    constexpr uint64_t kValue8Byte = 0x123456789;
}

TEST_CASE("Encode/Decode UintVar Uint64")
{
    CHECK_EQ(var::kValue1Byte, uint64_t(quicr::UintVar(var::kValue1Byte)));
    CHECK_EQ(var::kValue2Byte, uint64_t(quicr::UintVar(var::kValue2Byte)));
    CHECK_EQ(var::kValue4Byte, uint64_t(quicr::UintVar(var::kValue4Byte)));
    CHECK_EQ(var::kValue8Byte, uint64_t(quicr::UintVar(var::kValue8Byte)));
}

TEST_CASE("Length of UintVar")
{
    CHECK_EQ(1, quicr::UintVar(var::kValue1Byte).Size());
    CHECK_EQ(2, quicr::UintVar(var::kValue2Byte).Size());
    CHECK_EQ(4, quicr::UintVar(var::kValue4Byte).Size());
    CHECK_EQ(8, quicr::UintVar(var::kValue8Byte).Size());

    CHECK_EQ(1, quicr::UintVar(var::kValue1Byte).Bytes().size());
    CHECK_EQ(2, quicr::UintVar(var::kValue2Byte).Bytes().size());
    CHECK_EQ(4, quicr::UintVar(var::kValue4Byte).Bytes().size());
    CHECK_EQ(8, quicr::UintVar(var::kValue8Byte).Bytes().size());

    CHECK_EQ(1, quicr::UintVar::Size(quicr::UintVar(var::kValue1Byte).Bytes()[0]));
    CHECK_EQ(2, quicr::UintVar::Size(quicr::UintVar(var::kValue2Byte).Bytes()[0]));
    CHECK_EQ(4, quicr::UintVar::Size(quicr::UintVar(var::kValue4Byte).Bytes()[0]));
    CHECK_EQ(8, quicr::UintVar::Size(quicr::UintVar(var::kValue8Byte).Bytes()[0]));
}

TEST_CASE("Encode/Decode UintVar Bytes")
{
    CHECK_EQ(quicr::UintVar(quicr::UintVar(var::kValue1Byte).Bytes()), var::kValue1Byte);
    CHECK_EQ(quicr::UintVar(quicr::UintVar(var::kValue2Byte).Bytes()), var::kValue2Byte);
    CHECK_EQ(quicr::UintVar(quicr::UintVar(var::kValue4Byte).Bytes()), var::kValue4Byte);
    CHECK_EQ(quicr::UintVar(quicr::UintVar(var::kValue8Byte).Bytes()), var::kValue8Byte);
}

TEST_CASE("Validate UintVar from Known UintVar Bytes")
{
    const std::vector<uint8_t> one_byte = { 0x12 };
    const std::vector<uint8_t> two_byte = { 0x52, 0x34 };
    const std::vector<uint8_t> four_byte = { 0x80, 0x12, 0x34, 0x56 };
    const std::vector<uint8_t> eight_byte = { 0xC0, 0, 0, 0x1, 0x23, 0x45, 0x67, 0x89 };

    const auto v_one_byte = quicr::UintVar(var::kValue1Byte).Bytes();
    const auto v_two_byte = quicr::UintVar(var::kValue2Byte).Bytes();
    const auto v_four_byte = quicr::UintVar(var::kValue4Byte).Bytes();
    const auto v_eight_byte = quicr::UintVar(var::kValue8Byte).Bytes();

    CHECK_EQ(var::kValue1Byte, uint64_t(quicr::UintVar(one_byte)));
    CHECK_EQ(var::kValue2Byte, uint64_t(quicr::UintVar(two_byte)));
    CHECK_EQ(var::kValue4Byte, uint64_t(quicr::UintVar(four_byte)));
    CHECK_EQ(var::kValue8Byte, uint64_t(quicr::UintVar(eight_byte)));

    CHECK(std::vector<uint8_t>(v_one_byte.begin(), v_one_byte.end()) == one_byte);
    CHECK(std::vector<uint8_t>(v_two_byte.begin(), v_two_byte.end()) == two_byte);
    CHECK(std::vector<uint8_t>(v_four_byte.begin(), v_four_byte.end()) == four_byte);
    CHECK(std::vector<uint8_t>(v_eight_byte.begin(), v_eight_byte.end()) == eight_byte);
}

TEST_CASE("UintVar Invalid Construction")
{
    CHECK_THROWS(quicr::UintVar(std::numeric_limits<uint64_t>::max()));
    CHECK_THROWS(quicr::UintVar(std::vector<uint8_t>{}));
    CHECK_THROWS(quicr::UintVar(std::vector<uint8_t>(sizeof(uint64_t) + 1)));
    CHECK_THROWS(quicr::UintVar(std::vector<uint8_t>{ 0xFF, 0xFF }));
}
