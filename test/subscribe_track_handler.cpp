// SPDX-FileCopyrightText: Copyright (c) 2024 Cisco Systems
// SPDX-License-Identifier: BSD-2-Clause

#include <doctest/doctest.h>
#include <quicr/subscribe_track_handler.h>

using namespace quicr;

TEST_CASE("Report Latency")
{
    const auto handler = SubscribeTrackHandler::Create({}, 0);
    handler->ReportLatency(0, 0, std::chrono::milliseconds(20));
}
