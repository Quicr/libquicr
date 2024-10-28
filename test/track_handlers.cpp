// SPDX-FileCopyrightText: Copyright (c) 2024 Cisco Systems
// SPDX-License-Identifier: BSD-2-Clause

#include <doctest/doctest.h>

#include <quicr/client.h>
#include <quicr/common.h>
#include <quicr/publish_track_handler.h>
#include <quicr/server.h>
#include <quicr/subscribe_track_handler.h>

class TestPublishTrackHandler : public quicr::PublishTrackHandler
{
    TestPublishTrackHandler()
      : PublishTrackHandler({ {}, {}, std::nullopt }, quicr::TrackMode::kDatagram, 0, 0)
    {
    }

  public:
    static std::shared_ptr<TestPublishTrackHandler> Create()
    {
        return std::shared_ptr<TestPublishTrackHandler>(new TestPublishTrackHandler());
    }
};

TEST_CASE("Create Track Handler")
{
    CHECK_NOTHROW(quicr::PublishTrackHandler::Create({ {}, {}, std::nullopt }, quicr::TrackMode::kDatagram, 0, 0));
    CHECK_NOTHROW(TestPublishTrackHandler::Create());
}
