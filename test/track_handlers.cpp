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
                 const std::shared_ptr<quicr::Stream>& stream,
                 std::shared_ptr<const std::vector<uint8_t>> data,
                 uint8_t,
                 uint32_t,
                 EnqueueFlags flags) override
    {
        if (++enqueue_count == fail_enqueue) {
            return false;
        }
        if (stream) {
            const bool has_data = data && !data->empty();
            if (has_data) {
                auto& sent = sent_by_stream[stream->GetStreamId()];
                sent.insert(sent.end(), data->begin(), data->end());
            }
            enqueued.push_back({ stream->GetStreamId(),
                                 flags.close_stream,
                                 has_data ? std::make_optional(data->front()) : std::nullopt });
        }
        return true;
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
        return std::make_shared<TestStream>(4 * ++created_request_streams);
    }

    int enqueue_count{ 0 };
    int fail_enqueue{ -1 };
    int created_streams{ 0 };
    int created_request_streams{ 0 };

    /// Bytes accepted on each stream, in order.
    std::map<std::uint64_t, std::vector<std::uint8_t>> sent_by_stream;

    struct Enqueued
    {
        std::uint64_t stream_id;
        bool close_stream;
        std::optional<std::uint8_t> first_byte;
    };

    /// Every accepted enqueue across all streams, in order.
    std::vector<Enqueued> enqueued;

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

/// Stream Count of the PUBLISH_DONE among the control messages sent on a stream, if one was sent.
static std::optional<std::uint64_t>
SentPublishDoneStreamCount(const std::vector<std::uint8_t>& sent)
{
    std::span<const std::uint8_t> remaining{ sent };
    while (!remaining.empty()) {
        const quicr::UintVar type(remaining);
        remaining = remaining.subspan(type.size());
        REQUIRE(remaining.size() >= sizeof(std::uint16_t));
        const std::size_t length = (remaining[0] << 8) | remaining[1];
        remaining = remaining.subspan(sizeof(std::uint16_t));
        REQUIRE(remaining.size() >= length);
        auto payload = remaining.first(length);
        remaining = remaining.subspan(length);

        if (type.Get() != static_cast<std::uint64_t>(quicr::messages::ControlMessageType::kPublishDone)) {
            continue;
        }

        const quicr::UintVar status_code(payload);
        payload = payload.subspan(status_code.size());
        return quicr::UintVar(payload).Get();
    }
    return std::nullopt;
}

TEST_CASE("PUBLISH_DONE Stream Count reflects the data streams opened")
{
    auto transport = std::make_shared<TestTransport>();
    auto session = CreateTestSession(transport);
    auto handler = std::make_shared<TestStreamPublishTrackHandler>();

    session->PublishTrack(handler);
    REQUIRE(transport->created_request_streams == 1);
    const std::uint64_t request_stream_id = 4;

    // As PUBLISH_OK would.
    handler->SetStatus(quicr::PublishTrackHandler::Status::kOk);

    const auto publish_one_object = [&] {
        quicr::ObjectHeaders headers{};
        const quicr::Bytes payload{ 1 };
        headers.payload_length = payload.size();
        REQUIRE(handler->PublishObject(headers, payload) == quicr::PublishTrackHandler::PublishObjectStatus::kOk);
    };

    std::uint64_t expected_stream_count = 0;
    std::uint64_t publish_done_stream_id = request_stream_id;
    SUBCASE("No data stream opened")
    {
        expected_stream_count = 0;
    }
    SUBCASE("A subgroup stream opened")
    {
        publish_one_object();
        REQUIRE(transport->created_streams == 1);

        // The exact count isn't tracked, so the publisher must say it doesn't know it.
        expected_stream_count = quicr::messages::kPublishDoneStreamCountUnknown;
    }
    SUBCASE("A reused handler does not count an earlier subscription's streams")
    {
        publish_one_object();
        session->UnpublishTrack(handler);
        REQUIRE(SentPublishDoneStreamCount(transport->sent_by_stream[request_stream_id]) ==
                quicr::messages::kPublishDoneStreamCountUnknown);

        session->PublishTrack(handler);
        REQUIRE(transport->created_request_streams == 2);
        publish_done_stream_id = 8;
        handler->SetStatus(quicr::PublishTrackHandler::Status::kOk);
        expected_stream_count = 0;
    }

    session->UnpublishTrack(handler);

    const auto stream_count = SentPublishDoneStreamCount(transport->sent_by_stream[publish_done_stream_id]);
    REQUIRE(stream_count.has_value());
    CHECK_EQ(*stream_count, expected_stream_count);
}

/// True if PUBLISH_DONE went out on the request stream only after every subgroup stream had its FIN.
static bool
PublishDoneFollowsSubgroupFins(const TestTransport& transport, std::uint64_t request_stream_id)
{
    const auto& log = transport.enqueued;
    const auto done = std::find_if(log.begin(), log.end(), [&](const TestTransport::Enqueued& e) {
        return e.stream_id == request_stream_id &&
               e.first_byte == static_cast<std::uint8_t>(quicr::messages::ControlMessageType::kPublishDone);
    });
    if (done == log.end()) {
        return false;
    }

    for (const auto& e : log) {
        if (e.stream_id == request_stream_id) {
            continue;
        }
        const auto fin = std::find_if(log.begin(), done, [&](const TestTransport::Enqueued& f) {
            return f.stream_id == e.stream_id && f.close_stream;
        });
        if (fin == done) {
            return false;
        }
    }
    return true;
}

TEST_CASE("PUBLISH_DONE unknown Stream Count encodes as the largest 62-bit value")
{
    const quicr::UintVar encoded(quicr::messages::kPublishDoneStreamCountUnknown);
    REQUIRE_EQ(encoded.size(), 9);
    CHECK_EQ(*encoded.begin(), 0xFF);
    CHECK_EQ(quicr::UintVar(std::span<const std::uint8_t>{ encoded.begin(), encoded.end() }).Get(),
             (std::uint64_t{ 1 } << 62) - 1);
}

TEST_CASE("PUBLISH_DONE Stream Count from a server ending a subscription")
{
    // A client session writes the SUBSCRIBE, so the server reads one exactly as a peer would send it.
    auto client_transport = std::make_shared<TestTransport>();
    auto client = CreateTestSession(client_transport);
    auto subscribe_handler = quicr::SubscribeTrackHandler::Create({ {}, {} }, 0);
    client->SubscribeTrack(subscribe_handler);
    REQUIRE(subscribe_handler->GetRequestId().has_value());
    const auto request_id = *subscribe_handler->GetRequestId();
    const std::uint64_t request_stream_id = 4;
    const auto& subscribe = client_transport->sent_by_stream[request_stream_id];
    REQUIRE_FALSE(subscribe.empty());

    auto server_transport = std::make_shared<TestTransport>();
    auto server_connection = std::make_shared<TestConnection>();
    auto server = quicr::Session::Create(quicr::ServerConfig{}, server_transport, server_connection, nullptr, nullptr);
    server_connection->SetDelegate(server);

    const auto request_stream = std::make_shared<TestStream>(request_stream_id);
    REQUIRE(request_stream->IsBidirectional());
    {
        std::lock_guard _(request_stream->rx_mutex);
        request_stream->rx_data.Push(std::span<const std::uint8_t>{ subscribe });
    }
    server_connection->OnRecvStream(request_stream);

    // As a relay would from SubscribeReceived.
    auto handler = std::make_shared<TestStreamPublishTrackHandler>();
    server->BindPublisherTrack(server_connection->GetID(), request_id, handler, false);
    REQUIRE(handler->GetStatus() == quicr::PublishTrackHandler::Status::kOk);

    std::uint64_t expected_stream_count = 0;
    SUBCASE("No data stream opened")
    {
        expected_stream_count = 0;
    }
    SUBCASE("A subgroup stream opened")
    {
        quicr::ObjectHeaders headers{};
        const quicr::Bytes payload{ 1 };
        headers.payload_length = payload.size();
        REQUIRE(handler->PublishObject(headers, payload) == quicr::PublishTrackHandler::PublishObjectStatus::kOk);
        REQUIRE(server_transport->created_streams == 1);
        expected_stream_count = quicr::messages::kPublishDoneStreamCountUnknown;
    }

    server->UnbindPublisherTrack(server_connection->GetID(), handler, true);

    const auto stream_count = SentPublishDoneStreamCount(server_transport->sent_by_stream[request_stream_id]);
    REQUIRE(stream_count.has_value());
    CHECK_EQ(*stream_count, expected_stream_count);
    CHECK(PublishDoneFollowsSubgroupFins(*server_transport, request_stream_id));
}

TEST_CASE("PUBLISH_DONE follows the FIN of every subgroup stream")
{
    auto transport = std::make_shared<TestTransport>();
    auto session = CreateTestSession(transport);
    auto handler = std::make_shared<TestStreamPublishTrackHandler>();

    session->PublishTrack(handler);
    REQUIRE(transport->created_request_streams == 1);
    const std::uint64_t request_stream_id = 4;

    // As PUBLISH_OK would.
    handler->SetStatus(quicr::PublishTrackHandler::Status::kOk);

    // Two groups, so two subgroup streams the application leaves open.
    quicr::ObjectHeaders headers{};
    const quicr::Bytes payload{ 1 };
    headers.payload_length = payload.size();
    REQUIRE(handler->PublishObject(headers, payload) == quicr::PublishTrackHandler::PublishObjectStatus::kOk);
    headers.group_id = 1;
    REQUIRE(handler->PublishObject(headers, payload) == quicr::PublishTrackHandler::PublishObjectStatus::kOk);
    REQUIRE(transport->created_streams == 2);

    session->UnpublishTrack(handler);

    CHECK(PublishDoneFollowsSubgroupFins(*transport, request_stream_id));
}
