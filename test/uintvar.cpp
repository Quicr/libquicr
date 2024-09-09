#include <doctest/doctest.h>

#include "moq/detail/uintvar.h"

namespace var {
    constexpr uint64_t kValue1Byte = 0x12;
    constexpr uint64_t kValue2Byte = 0x1234;
    constexpr uint64_t kValue4Byte = 0x123456;
    constexpr uint64_t kValue8Byte = 0x123456789;
}

TEST_CASE("Encode/Decode UintVar")
{
    CHECK_EQ(var::kValue1Byte, moq::ToUint64(moq::ToUintV(var::kValue1Byte)));
    CHECK_EQ(var::kValue2Byte, moq::ToUint64(moq::ToUintV(var::kValue2Byte)));
    CHECK_EQ(var::kValue4Byte, moq::ToUint64(moq::ToUintV(var::kValue4Byte)));
    CHECK_EQ(var::kValue8Byte, moq::ToUint64(moq::ToUintV(var::kValue8Byte)));
}

TEST_CASE("Length of UintVar")
{
    CHECK_EQ(1, moq::SizeofUintV(moq::ToUintV(var::kValue1Byte).at(0)));
    CHECK_EQ(2, moq::SizeofUintV(moq::ToUintV(var::kValue2Byte).at(0)));
    CHECK_EQ(4, moq::SizeofUintV(moq::ToUintV(var::kValue4Byte).at(0)));
    CHECK_EQ(8, moq::SizeofUintV(moq::ToUintV(var::kValue8Byte).at(0)));
}