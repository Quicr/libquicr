// SPDX-FileCopyrightText: Copyright (c) 2026 Cisco Systems
// SPDX-License-Identifier: BSD-2-Clause

#include "transport_picoquic.h"

#include <doctest/doctest.h>

using namespace quicr;

TEST_CASE("Picoquic data priority mapping at boundaries")
{
    CHECK_EQ(ToPicoquicDataPriority(0), 2);
    CHECK_EQ(ToPicoquicDataPriority(1), 2);
    CHECK_EQ(ToPicoquicDataPriority(2), 4);
    CHECK_EQ(ToPicoquicDataPriority(127), 128);
    CHECK_EQ(ToPicoquicDataPriority(128), 130);
    CHECK_EQ(ToPicoquicDataPriority(129), 130);
    CHECK_EQ(ToPicoquicDataPriority(251), 252);
    CHECK_EQ(ToPicoquicDataPriority(252), 254);
    CHECK_EQ(ToPicoquicDataPriority(253), 254);
    CHECK_EQ(ToPicoquicDataPriority(254), 254);
    CHECK_EQ(ToPicoquicDataPriority(255), 254);
}

TEST_CASE("Picoquic data priorities preserve order and reserve control priority")
{
    uint8_t previous = 0;
    for (unsigned priority = 0; priority <= 255; ++priority) {
        CAPTURE(priority);
        const auto mapped = ToPicoquicDataPriority(static_cast<uint8_t>(priority));
        CHECK_GE(mapped, 2);
        CHECK_LE(mapped, 254);
        CHECK_EQ(mapped % 2, 0);
        CHECK_GE(mapped, previous);
        previous = mapped;
    }
}
