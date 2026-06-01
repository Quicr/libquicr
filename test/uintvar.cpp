// SPDX-FileCopyrightText: Copyright (c) 2024 Cisco Systems
// SPDX-License-Identifier: BSD-2-Clause

#include <doctest/doctest.h>

#include "quicr/detail/uintvar.h"

#include <limits>
#include <vector>

namespace var {

    struct Vector
    {
        std::uint64_t value;
        std::vector<std::uint8_t> bytes;
    };

    const std::vector<Vector> kMinimalVectors = {
        { 0x00, { 0x00 } },
        { 0x7F, { 0x7F } },
        { 0x80, { 0x80, 0x80 } },
        { 0x3FFF, { 0xBF, 0xFF } },
        { 0x4000, { 0xC0, 0x40, 0x00 } },
        { 0x1FFFFF, { 0xDF, 0xFF, 0xFF } },
        { 0x200000, { 0xE0, 0x20, 0x00, 0x00 } },
        { 0xFFFFFFF, { 0xEF, 0xFF, 0xFF, 0xFF } },
        { 0x10000000, { 0xF0, 0x10, 0x00, 0x00, 0x00 } },
        { 0x7FFFFFFFF, { 0xF7, 0xFF, 0xFF, 0xFF, 0xFF } },
        { 0x800000000, { 0xF8, 0x08, 0x00, 0x00, 0x00, 0x00 } },
        { 0x3FFFFFFFFFF, { 0xFB, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF } },
        { 0x40000000000, { 0xFC, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00 } },
        { 0x1FFFFFFFFFFFF, { 0xFD, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF } },
        { 0x2000000000000, { 0xFE, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 } },
        { 0xFFFFFFFFFFFFFF, { 0xFE, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF } },
        { 0x100000000000000, { 0xFF, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 } },
        { std::numeric_limits<std::uint64_t>::max(), { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF } },
    };

    const std::vector<Vector> kSpecVectors = {
        { 37, { 0x25 } },
        { 37, { 0x80, 0x25 } },
        { 15'293, { 0xBB, 0xBD } },
        { 226'442'877, { 0xED, 0x7F, 0x3E, 0x7D } },
        { 2'893'212'287'960, { 0xFA, 0xA1, 0xA0, 0xE4, 0x03, 0xD8 } },
        { 151'288'809'941'952, { 0xFC, 0x89, 0x98, 0xAB, 0xC6, 0x6B, 0xC0 } },
        { 70'423'237'261'249'041, { 0xFE, 0xFA, 0x31, 0x8F, 0xA8, 0xE3, 0xCA, 0x11 } },
        { std::numeric_limits<std::uint64_t>::max(), { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF } },
    };
}

TEST_CASE("Check UintVar")
{
    CHECK_EQ(sizeof(std::uint64_t) + 1, sizeof(quicr::UintVar));
    CHECK(std::is_trivially_copy_constructible_v<quicr::UintVar>);
    CHECK(std::is_trivially_copy_assignable_v<quicr::UintVar>);
    CHECK(std::is_trivially_move_constructible_v<quicr::UintVar>);
    CHECK(std::is_trivially_move_assignable_v<quicr::UintVar>);
}

TEST_CASE("Encode/Decode UintVar Uint64")
{
    for (const auto& vector : var::kMinimalVectors) {
        CAPTURE(vector.value);
        CHECK_EQ(vector.value, uint64_t(quicr::UintVar(vector.value)));
    }
}

TEST_CASE("Encode/Decode UintVar Bytes")
{
    for (const auto& vector : var::kMinimalVectors) {
        CAPTURE(vector.value);
        CHECK_EQ(vector.value, uint64_t(quicr::UintVar(std::span{ quicr::UintVar(vector.value) })));
    }
}

TEST_CASE("Length of UintVar")
{
    for (const auto& [value, bytes] : var::kMinimalVectors) {
        CAPTURE(value);
        CHECK_EQ(bytes.size(), quicr::UintVar(value).Size());
        CHECK_EQ(bytes.size(), quicr::UintVar::Size(bytes.front()));
    }
}

TEST_CASE("Validate UintVar from minimal bytes")
{
    for (const auto& [value, bytes] : var::kMinimalVectors) {
        CAPTURE(value);
        CHECK_EQ(value, uint64_t(quicr::UintVar(bytes)));

        const auto encoded = quicr::UintVar(value);
        CHECK(std::vector<std::uint8_t>(encoded.begin(), encoded.end()) == bytes);
    }
}

TEST_CASE("Validate UintVar from Draft 18 examples")
{
    for (const auto& [value, bytes] : var::kSpecVectors) {
        CAPTURE(value);
        CHECK_EQ(value, uint64_t(quicr::UintVar(bytes)));
    }
}

TEST_CASE("UintVar Invalid Construction")
{
    CHECK_THROWS(quicr::UintVar(std::vector<uint8_t>{}));
    CHECK_THROWS(quicr::UintVar(std::vector<uint8_t>{ 0xFF, 0xFF }));
}
