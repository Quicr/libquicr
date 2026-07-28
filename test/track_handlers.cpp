// SPDX-FileCopyrightText: Copyright (c) 2024 Cisco Systems
// SPDX-License-Identifier: BSD-2-Clause

#include "quicr/common.h"
#include "quicr/handlers/publish_track_handler.h"
#include "quicr/handlers/subscribe_track_handler.h"
#include "quicr/messages/messages.h"

#include <doctest/doctest.h>

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

    TestSubscribeTrackHandler()
      : SubscribeTrackHandler({ {}, {} }, 0, quicr::messages::GroupOrder::kAscending)
    {
    }

    static std::shared_ptr<TestSubscribeTrackHandler> Create()
    {
        return std::shared_ptr<TestSubscribeTrackHandler>(new TestSubscribeTrackHandler());
    }

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

    void ObjectReceived(const quicr::ObjectHeaders& object_headers,
                        quicr::BytesSpan data,
                        std::optional<quicr::messages::StreamHeaderProperties> stream_mode) override
    {
        received_objects.push_back({ object_headers.group_id,
                                     object_headers.object_id,
                                     quicr::Bytes(data.begin(), data.end()),
                                     std::move(stream_mode) });
    }

    struct ReceivedObject
    {
        uint64_t group_id;
        uint64_t object_id;
        quicr::Bytes payload;
        std::optional<quicr::messages::StreamHeaderProperties> stream_mode;
    };

    std::optional<ReceivedStatus> last_status;
    int status_received_count{ 0 };
    std::vector<ReceivedObject> received_objects;
};

/**
 * @brief A subgroup header followed by two objects, as it arrives on the wire.
 */
static quicr::Bytes
SerializeSubgroup(const quicr::messages::StreamHeaderProperties& properties)
{
    quicr::messages::StreamHeaderSubGroup header;
    header.properties.emplace(properties);
    header.track_alias = 0x1234;
    header.group_id = 7;
    header.priority = 128;

    quicr::Bytes bytes;
    bytes << header;

    for (const auto& payload : { quicr::Bytes{ 0x0A, 0x0B }, quicr::Bytes{ 0x0C, 0x0D } }) {
        quicr::messages::StreamSubGroupObject object;
        object.properties.emplace(properties);
        object.object_delta = 0;
        object.payload = payload;
        bytes << object;
    }

    return bytes;
}

TEST_CASE("Subscribe Track Handler receives every object buffered on a stream")
{
    auto handler = TestSubscribeTrackHandler::Create();

    const quicr::messages::StreamHeaderProperties properties{
        false, quicr::messages::SubgroupIdType::kIsZero, false, false, true
    };

    // Mirror the session handoff: the stream type stays in the buffer for the handler to parse.
    quicr::StreamBuffer<uint8_t> buffer;
    buffer.Push(SerializeSubgroup(properties));
    handler->StreamDataRecv(0, std::move(buffer));

    REQUIRE_EQ(handler->received_objects.size(), 2);

    CHECK_EQ(handler->received_objects[0].group_id, 7);
    CHECK_EQ(handler->received_objects[0].object_id, 0);
    CHECK_EQ(handler->received_objects[0].payload, quicr::Bytes{ 0x0A, 0x0B });
    CHECK(handler->received_objects[0].stream_mode.has_value());

    CHECK_EQ(handler->received_objects[1].group_id, 7);
    CHECK_EQ(handler->received_objects[1].object_id, 1);
    CHECK_EQ(handler->received_objects[1].payload, quicr::Bytes{ 0x0C, 0x0D });
    CHECK_FALSE(handler->received_objects[1].stream_mode.has_value());
}

TEST_CASE("Subscribe Track Handler resumes parsing across single byte chunks")
{
    auto handler = TestSubscribeTrackHandler::Create();

    const quicr::messages::StreamHeaderProperties properties{
        false, quicr::messages::SubgroupIdType::kIsZero, false, false, true
    };
    const auto bytes = SerializeSubgroup(properties);

    quicr::StreamBuffer<uint8_t> buffer;
    buffer.Push(bytes.front());
    handler->StreamDataRecv(0, std::move(buffer));

    for (auto it = std::next(bytes.begin()); it != bytes.end(); ++it) {
        handler->StreamDataRecv(0, std::make_shared<std::vector<uint8_t>>(1, *it));
    }

    REQUIRE_EQ(handler->received_objects.size(), 2);
    CHECK_EQ(handler->received_objects[0].object_id, 0);
    CHECK_EQ(handler->received_objects[0].payload, quicr::Bytes{ 0x0A, 0x0B });
    CHECK_EQ(handler->received_objects[1].object_id, 1);
    CHECK_EQ(handler->received_objects[1].payload, quicr::Bytes{ 0x0C, 0x0D });
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
