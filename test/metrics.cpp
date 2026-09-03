// SPDX-FileCopyrightText: Copyright (c) 2026 Cisco Systems
// SPDX-License-Identifier: BSD-2-Clause

#include "quicr/metrics.h"

#include <doctest/doctest.h>

TEST_CASE("MinMaxAvg retains a zero minimum")
{
    quicr::MinMaxAvg metric;

    metric.AddValue(0);
    metric.AddValue(4);

    CHECK_EQ(metric.min, 0);
    CHECK_EQ(metric.max, 4);
    CHECK_EQ(metric.avg, 2);
    CHECK_EQ(metric.value_sum, 4);
    CHECK_EQ(metric.value_count, 2);
}
