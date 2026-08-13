// SPDX-FileCopyrightText: Copyright (c) 2026 Cisco Systems
// SPDX-License-Identifier: BSD-2-Clause

#include "transport_picoquic.h"

#include <doctest/doctest.h>

using namespace quicr;

TEST_CASE("Stream close validation check")
{
    struct TestCase
    {
        std::uint64_t stream_id;
        bool is_server;
        StreamOperation mode;
    };

    constexpr TestCase cases[]{
        { 2, false, StreamOperation::kStopSending }, { 2, true, StreamOperation::kFin },
        { 2, true, StreamOperation::kReset },        { 3, false, StreamOperation::kFin },
        { 3, false, StreamOperation::kReset },       { 3, true, StreamOperation::kStopSending },
        { 2, false, StreamOperation::kCancel },      { 3, false, StreamOperation::kCancel },
    };

    for (const auto& test_case : cases) {
        CAPTURE(test_case.stream_id);
        CAPTURE(test_case.is_server);
        CAPTURE(test_case.mode);
        CHECK_THROWS_AS(CheckCloseStream(test_case.stream_id, test_case.is_server, test_case.mode),
                        const std::invalid_argument&);
    }
}
