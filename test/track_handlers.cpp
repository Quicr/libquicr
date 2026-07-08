// SPDX-FileCopyrightText: Copyright (c) 2024 Cisco Systems
// SPDX-License-Identifier: BSD-2-Clause

#include "quicr/common.h"
#include "quicr/handlers/publish_track_handler.h"
#include "quicr/handlers/subscribe_track_handler.h"
#include "quicr/messages/messages.h"

#include <doctest/doctest.h>

#include <stdexcept>

class TestPublishTrackHandler : public quicr::PublishTrackHandler
{
    TestPublishTrackHandler()
      : PublishTrackHandler({ {}, {} }, quicr::TrackMode::kDatagram, 0, 0)
    {
    }

  public:
    static std::shared_ptr<TestPublishTrackHandler> Create()
    {
        return std::shared_ptr<TestPublishTrackHandler>(new TestPublishTrackHandler());
    }

    void StatusChanged(Status status) override { statuses.push_back(status); }

    using quicr::PublishTrackHandler::RequestUpdateReceived;

    std::vector<Status> statuses;
};

TEST_CASE("Create Track Handler")
{
    CHECK_NOTHROW(quicr::PublishTrackHandler::Create({ {}, {} }, quicr::TrackMode::kDatagram, 0, 0, { 0, 0 }));
    CHECK_NOTHROW(TestPublishTrackHandler::Create());
}

TEST_CASE("Publish Track Handler CanPublish")
{
    auto handler = quicr::PublishTrackHandler::Create({ {}, {} }, quicr::TrackMode::kDatagram, 0, 0, { 0, 0 });

    CHECK_FALSE(handler->CanPublish());
}

TEST_CASE("Publish Track Handler updates don't override status")
{
    auto handler = TestPublishTrackHandler::Create();
    const auto parameters = quicr::messages::Parameters{}
                              .Add(quicr::messages::ParameterType::kSubscriberPriority, std::uint8_t{ 42 })
                              .Add(quicr::messages::ParameterType::kForward, false);

    handler->RequestUpdateReceived(parameters);

    CHECK_EQ(handler->GetStatus(), quicr::PublishTrackHandler::Status::kPaused);
    REQUIRE_EQ(handler->statuses.size(), 2);
    CHECK_EQ(handler->statuses[0], quicr::PublishTrackHandler::Status::kPaused);
    CHECK_EQ(handler->statuses[1], quicr::PublishTrackHandler::Status::kSubscriptionUpdated);
}

class TestSubscribeTrackHandler : public quicr::SubscribeTrackHandler
{
  public:
    struct ReceivedStatus
    {
        uint64_t group_id;
        uint64_t object_id;
        uint8_t priority;
        quicr::ObjectStatus status;
        std::optional<quicr::Extensions> extensions;
        std::optional<quicr::Extensions> immutable_extensions;
    };

    explicit TestSubscribeTrackHandler(bool publisher_initiated = false)
      : SubscribeTrackHandler({ {}, {} },
                              0,
                              quicr::messages::GroupOrder::kAscending,
                              {},
                              std::nullopt,
                              publisher_initiated)
    {
    }

    static std::shared_ptr<TestSubscribeTrackHandler> Create(bool publisher_initiated = false)
    {
        return std::shared_ptr<TestSubscribeTrackHandler>(new TestSubscribeTrackHandler(publisher_initiated));
    }

    void StatusChanged(Status status) override { statuses.push_back(status); }

    void ObjectStatusReceived(const uint64_t group_id,
                              const uint64_t object_id,
                              const std::uint8_t priority,
                              const quicr::ObjectStatus status,
                              const std::optional<quicr::Extensions> extensions,
                              const std::optional<quicr::Extensions> immutable_extensions) override
    {
        last_status = { group_id, object_id, priority, status, extensions, immutable_extensions };
        status_received_count++;
    }

    using quicr::SubscribeTrackHandler::RequestUpdateReceived;

    std::optional<ReceivedStatus> last_status;
    int status_received_count{ 0 };
    std::vector<Status> statuses;
};

TEST_CASE("Subscribe Track Handler updates don't override status")
{
    auto handler = TestSubscribeTrackHandler::Create(true);

    handler->RequestUpdateReceived({});

    CHECK_EQ(handler->GetStatus(), quicr::SubscribeTrackHandler::Status::kOk);
    REQUIRE_EQ(handler->statuses.size(), 1);
    CHECK_EQ(handler->statuses[0], quicr::SubscribeTrackHandler::Status::kSubscriptionUpdated);
}

TEST_CASE("Track Handler resolves exactly one update per received update")
{
    std::shared_ptr<quicr::BaseTrackHandler> handler;
    std::function<void()> receive_update;
    SUBCASE("Publish")
    {
        auto h = TestPublishTrackHandler::Create();
        receive_update = [h] { h->RequestUpdateReceived({}); };
        handler = h;
    }
    SUBCASE("Subscribe")
    {
        auto h = TestSubscribeTrackHandler::Create(true);
        receive_update = [h] { h->RequestUpdateReceived({}); };
        handler = h;
    }

    // Request arrives.
    receive_update();
    // Consume should work.
    CHECK_NOTHROW(handler->ResolveRequestUpdate());
    // Another consume should throw.
    CHECK_THROWS_AS(handler->ResolveRequestUpdate(), std::logic_error);
}

TEST_CASE("Subscribe Track Handler ObjectStatusReceived - kDoesNotExist")
{
    auto handler = TestSubscribeTrackHandler::Create();

    // Create an ObjectDatagramStatus message
    quicr::messages::ObjectDatagramStatus status_msg;
    status_msg.track_alias = 0x1234;
    status_msg.group_id = 100;
    status_msg.object_id = 50;
    status_msg.priority = 5;
    status_msg.status = quicr::ObjectStatus::kDoesNotExist;

    // Serialize the message
    quicr::Bytes buffer;
    buffer << status_msg;

    // Call DgramDataRecv with the serialized data
    auto data = std::make_shared<std::vector<uint8_t>>(buffer.begin(), buffer.end());
    handler->DgramDataRecv(data);

    // Verify the callback was invoked with correct parameters
    REQUIRE(handler->last_status.has_value());
    CHECK_EQ(handler->last_status->group_id, 100);
    CHECK_EQ(handler->last_status->object_id, 50);
    CHECK_EQ(handler->last_status->status, quicr::ObjectStatus::kDoesNotExist);
    CHECK_EQ(handler->status_received_count, 1);
}

TEST_CASE("Subscribe Track Handler ObjectStatusReceived - kEndOfGroup")
{
    auto handler = TestSubscribeTrackHandler::Create();

    quicr::messages::ObjectDatagramStatus status_msg;
    status_msg.track_alias = 0x5678;
    status_msg.group_id = 200;
    status_msg.object_id = 10;
    status_msg.priority = 3;
    status_msg.status = quicr::ObjectStatus::kEndOfGroup;

    quicr::Bytes buffer;
    buffer << status_msg;

    auto data = std::make_shared<std::vector<uint8_t>>(buffer.begin(), buffer.end());
    handler->DgramDataRecv(data);

    REQUIRE(handler->last_status.has_value());
    CHECK_EQ(handler->last_status->group_id, 200);
    CHECK_EQ(handler->last_status->object_id, 10);
    CHECK_EQ(handler->last_status->status, quicr::ObjectStatus::kEndOfGroup);
}

TEST_CASE("Subscribe Track Handler ObjectStatusReceived - kEndOfTrack")
{
    auto handler = TestSubscribeTrackHandler::Create();

    quicr::messages::ObjectDatagramStatus status_msg;
    status_msg.track_alias = 0xABCD;
    status_msg.group_id = 999;
    status_msg.object_id = 0;
    status_msg.priority = 1;
    status_msg.status = quicr::ObjectStatus::kEndOfTrack;

    quicr::Bytes buffer;
    buffer << status_msg;

    auto data = std::make_shared<std::vector<uint8_t>>(buffer.begin(), buffer.end());
    handler->DgramDataRecv(data);

    REQUIRE(handler->last_status.has_value());
    CHECK_EQ(handler->last_status->group_id, 999);
    CHECK_EQ(handler->last_status->object_id, 0);
    CHECK_EQ(handler->last_status->status, quicr::ObjectStatus::kEndOfTrack);
}

TEST_CASE("Subscribe Track Handler ObjectStatusReceived with extensions")
{
    auto handler = TestSubscribeTrackHandler::Create();

    quicr::messages::ObjectDatagramStatus status_msg;
    status_msg.track_alias = 0x1111;
    status_msg.group_id = 42;
    status_msg.object_id = 7;
    status_msg.priority = 2;
    status_msg.status = quicr::ObjectStatus::kDoesNotExist;
    // Add extensions to trigger type 0x05 instead of 0x04
    status_msg.extensions = quicr::Extensions{ { 0x1, { { 0xAA, 0xBB } } } };

    quicr::Bytes buffer;
    buffer << status_msg;

    // Verify properties.
    const auto properties = quicr::messages::DatagramHeaderProperties(buffer.front());
    CHECK(properties.status);
    CHECK(properties.extensions);

    auto data = std::make_shared<std::vector<uint8_t>>(buffer.begin(), buffer.end());
    handler->DgramDataRecv(data);

    REQUIRE(handler->last_status.has_value());
    CHECK_EQ(handler->last_status->group_id, 42);
    CHECK_EQ(handler->last_status->object_id, 7);
    CHECK_EQ(handler->last_status->status, quicr::ObjectStatus::kDoesNotExist);
    // Verify extensions were received
    REQUIRE(handler->last_status->extensions.has_value());
    REQUIRE(handler->last_status->extensions->contains(0x1));
    CHECK_EQ(handler->last_status->extensions->at(0x1).size(), 1);
    CHECK_EQ(handler->last_status->extensions->at(0x1)[0], quicr::Bytes({ 0xAA, 0xBB }));
}
