#include <doctest/doctest.h>

#include "quicr/detail/uintvar.h"

namespace var {
    constexpr uint64_t kValue1Byte = 0x12;
    constexpr uint64_t kValue2Byte = 0x1234;
    constexpr uint64_t kValue4Byte = 0x123456;
    constexpr uint64_t kValue8Byte = 0x123456789;
}

TEST_CASE("Encode/Decode UintVar")
{
    CHECK_EQ(var::kValue1Byte, quicr::ToUint64(quicr::ToUintV(var::kValue1Byte)));
    CHECK_EQ(var::kValue2Byte, quicr::ToUint64(quicr::ToUintV(var::kValue2Byte)));
    CHECK_EQ(var::kValue4Byte, quicr::ToUint64(quicr::ToUintV(var::kValue4Byte)));
    CHECK_EQ(var::kValue8Byte, quicr::ToUint64(quicr::ToUintV(var::kValue8Byte)));
}

TEST_CASE("Length of UintVar")
{
    CHECK_EQ(1, quicr::SizeofUintV(quicr::ToUintV(var::kValue1Byte).at(0)));
    CHECK_EQ(2, quicr::SizeofUintV(quicr::ToUintV(var::kValue2Byte).at(0)));
    CHECK_EQ(4, quicr::SizeofUintV(quicr::ToUintV(var::kValue4Byte).at(0)));
    CHECK_EQ(8, quicr::SizeofUintV(quicr::ToUintV(var::kValue8Byte).at(0)));
}