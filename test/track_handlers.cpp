// SPDX-FileCopyrightText: Copyright (c) 2024 Cisco Systems
// SPDX-License-Identifier: BSD-2-Clause

#include <doctest/doctest.h>

#include <moq/client.h>
#include <moq/common.h>
#include <moq/publish_track_handler.h>
#include <moq/server.h>
#include <moq/server_publish_track_handler.h>
#include <moq/subscribe_track_handler.h>

class TestPublishTrackHandler : public moq::PublishTrackHandler
{
    TestPublishTrackHandler()
      : PublishTrackHandler({ .name{}, .name_space{} }, TrackMode::kStreamPerGroup, 0, 0)
    {
    }

  public:
    static std::shared_ptr<TestPublishTrackHandler> Create()
    {
        return std::shared_ptr<TestPublishTrackHandler>(new TestPublishTrackHandler());
    }
};

using TrackMode = moq::BaseTrackHandler::TrackMode;

TEST_CASE("Create Track Handler")
{
    CHECK_NOTHROW(moq::PublishTrackHandler::Create({ .name{}, .name_space{} }, TrackMode::kStreamPerGroup, 0, 0));
    CHECK_NOTHROW(TestPublishTrackHandler::Create());
}
