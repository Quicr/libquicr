// SPDX-FileCopyrightText: Copyright (c) 2024 Cisco Systems
// SPDX-License-Identifier: BSD-2-Clause

#include "quicr/connection.h"
#include "quicr/handlers/publish_fetch_handler.h"
#include "quicr/handlers/publish_track_handler.h"
#include "quicr/handlers/subscribe_track_handler.h"
#include "quicr/messages/message_serialisation.h"
#include "quicr/session.h"
#include "stream.h"

#include <doctest/doctest.h>

class TestPublishTrackHandler : public quicr::PublishTrackHandler
{
    TestPublishTrackHandler()
      : PublishTrackHandler({ {}, {} }, quicr::TrackMode::kDatagram, 0, 0, 0)
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
    CHECK_NOTHROW(quicr::PublishTrackHandler::Create({ {}, {} }, quicr::TrackMode::kDatagram, 0, 0, 0, { 0, 0 }));
    CHECK_NOTHROW(TestPublishTrackHandler::Create());
}

TEST_CASE("Publish Track Handler CanPublish")
{
    auto handler = quicr::PublishTrackHandler::Create({ {}, {} }, quicr::TrackMode::kDatagram, 0, 0, 0, { 0, 0 });

    CHECK_FALSE(handler->CanPublish());
}

TEST_CASE("Subscribe rendezvous timeout rejects negative durations")
{
    using namespace std::chrono_literals;
    auto handler = quicr::SubscribeTrackHandler::Create({ {}, {} }, 0);
    CHECK(handler->GetRendezvousTimeout() == 0ms);
    handler->SetRendezvousTimeout(30s);
    CHECK_THROWS_AS(handler->SetRendezvousTimeout(-1ms), std::invalid_argument);
    CHECK(handler->GetRendezvousTimeout() == 30s);
    handler->SetRendezvousTimeout(0ms);
    CHECK(handler->GetRendezvousTimeout() == 0ms);
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

TEST_CASE("Subscribe Track Handler ObjectStatusReceived - kDoesNotExist")
{
    auto handler = TestSubscribeTrackHandler::Create();

    // Create an ObjectDatagramStatus message
    quicr::messages::ObjectDatagramStatus status_msg;
    status_msg.track_alias = 0x1234;
    status_msg.group_id = 100;
    status_msg.object_id = 50;
    status_msg.publisher_priority = 5;
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
    status_msg.publisher_priority = 3;
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
    status_msg.publisher_priority = 1;
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
    status_msg.publisher_priority = 2;
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

class TestConnection : public quicr::Connection
{
  public:
    TestConnection()
      : Connection(1)
    {
    }
    quicr::QuicMetricsSample TakeMetricsSample() override { return {}; }
    void ReportMetricsSample(const quicr::MetricsTimeStamp&, const quicr::QuicMetricsSample&) override {}
};

class TestStream : public quicr::Stream
{
  public:
    explicit TestStream(uint64_t id)
      : Stream(id, 1)
    {
    }
};

class TestTransport : public quicr::Transport
{
  public:
    quicr::TransportStatus Status() const override { return quicr::TransportStatus::kReady; }
    std::shared_ptr<quicr::Connection> Start() override { return nullptr; }
    void Close(const std::shared_ptr<quicr::Connection>&, quicr::AppReasonForClose) override {}
    void CloseStream(const std::shared_ptr<quicr::Connection>&,
                     const std::shared_ptr<quicr::Stream>&,
                     quicr::StreamOperation) override
    {
    }
    bool GetPeerAddrInfo(const std::shared_ptr<quicr::Connection>&, sockaddr_storage*) override { return false; }
    bool Enqueue(const std::shared_ptr<quicr::Connection>&,
                 const std::shared_ptr<quicr::Stream>&,
                 std::shared_ptr<const std::vector<uint8_t>>,
                 uint32_t,
                 EnqueueFlags) override
    {
        return ++enqueue_count != fail_enqueue;
    }
    void EnqueueDatagram(const std::shared_ptr<quicr::Connection>&,
                         std::shared_ptr<const std::vector<uint8_t>>,
                         uint8_t,
                         uint32_t) override
    {
    }
    std::shared_ptr<const std::vector<uint8_t>> Dequeue(const std::shared_ptr<quicr::Connection>&) override
    {
        return nullptr;
    }
    int CloseWebTransportSession(const std::shared_ptr<quicr::Connection>&, uint32_t, const char*) override
    {
        return 0;
    }
    int DrainWebTransportSession(const std::shared_ptr<quicr::Connection>&) override { return 0; }
    std::shared_ptr<quicr::Stream> CreateDataStream(const std::shared_ptr<quicr::Connection>&, uint8_t) override
    {
        return std::make_shared<TestStream>(2 + 4 * created_streams++);
    }
    std::shared_ptr<quicr::Stream> CreateControlStream(const std::shared_ptr<quicr::Connection>&) override
    {
        return nullptr;
    }
    std::shared_ptr<quicr::Stream> CreateRequestStream(const std::shared_ptr<quicr::Connection>&) override
    {
        return nullptr;
    }

    int enqueue_count{ 0 };
    int fail_enqueue{ -1 };
    int created_streams{ 0 };

  private:
    void QueueDeferredReply(std::function<void()>&&) override {}
};

class TestStreamPublishTrackHandler : public quicr::PublishTrackHandler
{
  public:
    TestStreamPublishTrackHandler()
      : PublishTrackHandler({ {}, {} }, quicr::TrackMode::kStream, 0, 0, 1000)
    {
    }
    using TrackHandler::SetTransport;
};

static std::shared_ptr<quicr::Session>
CreateTestSession(const std::shared_ptr<TestTransport>& transport)
{
    return quicr::Session::Create(
      quicr::ClientConfig{}, transport, std::make_shared<TestConnection>(), nullptr, nullptr);
}

TEST_CASE("Publishing reports rejected subgroup enqueues")
{
    using Status = quicr::PublishTrackHandler::PublishObjectStatus;
    auto transport = std::make_shared<TestTransport>();
    auto session = CreateTestSession(transport);
    auto handler = std::make_shared<TestStreamPublishTrackHandler>();
    handler->SetTransport(session);
    handler->SetRequestId(0);
    handler->SetTrackAlias(0);
    handler->SetStatus(quicr::PublishTrackHandler::Status::kOk);
    quicr::ObjectHeaders headers{};
    const quicr::Bytes payload{ 1 };
    headers.payload_length = payload.size();
    REQUIRE(handler->PublishObject(headers, payload) == Status::kOk);

    SUBCASE("Enqueue rejects the object")
    {
        transport->fail_enqueue = transport->enqueue_count + 1;
        headers.object_id++;
        CHECK(handler->PublishObject(headers, payload) == Status::kSubgroupClosed);
    }
    SUBCASE("Forwarding enqueue rejects the object")
    {
        transport->fail_enqueue = transport->enqueue_count + 1;
        CHECK(handler->ForwardPublishedData(false, 0, 0, std::make_shared<quicr::Bytes>(payload)) ==
              Status::kSubgroupClosed);
    }
}

TEST_CASE("Fetch closure is terminal")
{
    using Status = quicr::PublishTrackHandler::PublishObjectStatus;
    auto transport = std::make_shared<TestTransport>();
    auto session = CreateTestSession(transport);
    auto handler = quicr::PublishFetchHandler::Create({ {}, {} }, 0, 0, quicr::messages::GroupOrder::kAscending, 1000);
    session->BindFetchTrack(handler);
    quicr::ObjectHeaders headers{};
    const quicr::Bytes payload{ 1 };
    headers.payload_length = payload.size();
    int expected_streams = 1;
    SUBCASE("Header rejected")
    {
        transport->fail_enqueue = 1;
    }
    SUBCASE("Object rejected")
    {
        transport->fail_enqueue = 2;
    }
    SUBCASE("Completed before first object")
    {
        session->UnbindFetchTrack(handler);
        expected_streams = 0;
    }
    SUBCASE("Completed after an object")
    {
        REQUIRE(handler->PublishObject(headers, payload) == Status::kOk);
        session->UnbindFetchTrack(handler);
    }
    CHECK(handler->PublishObject(headers, payload) == Status::kFetchClosed);
    const auto attempts = transport->enqueue_count;
    CHECK(handler->PublishObject(headers, payload) == Status::kFetchClosed);
    CHECK(transport->enqueue_count == attempts);
    CHECK(transport->created_streams == expected_streams);
}
