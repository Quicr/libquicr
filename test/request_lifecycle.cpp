// SPDX-FileCopyrightText: Copyright (c) 2026 Cisco Systems
// SPDX-License-Identifier: BSD-2-Clause

#include "quicr/handlers/fetch_track_handler.h"
#include "quicr/handlers/publish_fetch_handler.h"
#include "quicr/handlers/publish_namespace_handler.h"
#include "quicr/handlers/subscribe_namespace_handler.h"
#include "quicr/handlers/track_status_handler.h"
#include "quicr/session_callbacks.h"
#include "stream.h"

#include <doctest/doctest.h>

#include <deque>

// These tests are a little involved and maybe less idiomatic than others,
// but they're here to cover MoQ request lifecycle in libquicr, which is complicated and easy to get wrong.
// Update the draft references if we rev draft version!

namespace {
    using namespace quicr;
    using namespace quicr::messages;

    class MockConnection : public Connection
    {
      public:
        MockConnection()
          : Connection(1)
        {
        }
        QuicMetricsSample TakeMetricsSample() override { return {}; }
        void ReportMetricsSample(const MetricsTimeStamp&, const QuicMetricsSample&) override {}
    };

    class MockStream : public Stream
    {
      public:
        MockStream(uint64_t id, const std::shared_ptr<Connection>& connection)
          : Stream(id, connection)
        {
        }
    };

    class RecordingTransport : public Transport
    {
      public:
        struct Send
        {
            std::shared_ptr<Stream> stream;
            Bytes bytes;
            bool fin;
        };
        std::vector<Send> sends;
        std::vector<std::pair<uint64_t, StreamOperation>> closes;
        std::map<uint64_t, std::shared_ptr<Stream>> streams;
        std::deque<std::function<void()>> replies;
        uint64_t next_request = 0;
        uint64_t next_data = 2;

        TransportStatus Status() const override { return TransportStatus::kReady; }
        std::shared_ptr<Connection> Start() override { return nullptr; }
        std::shared_ptr<Connection> Connect(const TransportRemote&) override { return nullptr; }
        void Close(const std::shared_ptr<Connection>&, AppReasonForClose) override {}
        void CloseStream(const std::shared_ptr<Stream>& stream, StreamOperation op) override
        {
            if (stream) {
                closes.emplace_back(stream->GetStreamId(), op);
            }
        }
        bool Enqueue(const std::shared_ptr<Connection>&,
                     const std::shared_ptr<Stream>& stream,
                     std::shared_ptr<const Bytes> bytes,
                     uint8_t,
                     uint32_t,
                     EnqueueFlags flags) override
        {
            sends.push_back({ stream, bytes ? *bytes : Bytes{}, flags.close_stream });
            return true;
        }
        void EnqueueDatagram(const std::shared_ptr<Connection>&,
                             std::shared_ptr<const Bytes>,
                             uint8_t,
                             uint32_t) override
        {
        }
        std::shared_ptr<const Bytes> Dequeue(const std::shared_ptr<Connection>&) override { return nullptr; }
        int CloseWebTransportSession(const std::shared_ptr<Connection>&, uint32_t, const char*) override { return 0; }
        int DrainWebTransportSession(const std::shared_ptr<Connection>&) override { return 0; }
        std::shared_ptr<Stream> Make(uint64_t id, const std::shared_ptr<Connection>& connection)
        {
            auto [it, inserted] = streams.try_emplace(id);
            if (inserted) {
                it->second = std::make_shared<MockStream>(id, connection);
            }
            return it->second;
        }
        std::shared_ptr<Stream> CreateRequestStream(const std::shared_ptr<Connection>& c) override
        {
            const auto id = next_request;
            next_request += 4;
            return Make(id, c);
        }
        std::shared_ptr<Stream> CreateDataStream(const std::shared_ptr<Connection>& c, uint8_t) override
        {
            const auto id = next_data;
            next_data += 4;
            return Make(id, c);
        }
        std::shared_ptr<Stream> CreateControlStream(const std::shared_ptr<Connection>& c) override
        {
            return CreateDataStream(c, 0);
        }
        bool Closed(uint64_t id, StreamOperation op) const
        {
            return std::find(closes.begin(), closes.end(), std::pair{ id, op }) != closes.end();
        }
        void RunReplies()
        {
            while (!replies.empty()) {
                auto f = std::move(replies.front());
                replies.pop_front();
                f();
            }
        }

      private:
        void QueueDeferredReply(std::function<void()>&& reply) override { replies.push_back(std::move(reply)); }
    };

    const FullTrackName kTrack{ TrackNamespace({ "lifecycle" }), { 1 } };
    const Bytes kPayload{ 0x42 };
    const ObjectHeaders kObject{ .group_id = 0, .object_id = 0, .subgroup_id = 0, .payload_length = 1 };

    class Receiver : public SubscribeTrackHandler
    {
      public:
        explicit Receiver(const std::optional<JoiningFetch>& joining_fetch = std::nullopt)
          : SubscribeTrackHandler(kTrack, 0, GroupOrder::kAscending, std::monostate{}, joining_fetch)
        {
        }
    };

    class ServerCallbacks : public Session::ServerCallbacks
    {
      public:
        bool reject = false;
        bool defer = false;
        Reply<SubscribeResponse, RequestErrorCode>::CompletionType subscribe_reply;
        Reply<const FetchResponse, FetchErrorCode>::CompletionType fetch_reply;
        Reply<const PublishResponse, PublishErrorCode>::CompletionType publish_reply;
        Reply<TrackStatusResponse, RequestErrorCode>::CompletionType status_reply;
        Reply<std::vector<TrackNamespace>, RequestErrorCode>::CompletionType namespace_reply;
        Reply<void, PublishNamespaceErrorCode>::CompletionType publish_namespace_reply;
        std::shared_ptr<PublishTrackHandler> publisher;
        std::shared_ptr<PublishFetchHandler> fetch;
        std::shared_ptr<SubscribeTrackHandler> receiver;
        unsigned cancellations = 0;
        unsigned fetch_cancellations = 0;
        std::optional<JoiningFetchAttributes> joining_attributes;
        Reply<SubscribeResponse, RequestErrorCode> SubscribeReceived(const std::shared_ptr<Session>& session,
                                                                     uint64_t id,
                                                                     const FullTrackName&,
                                                                     const SubscribeAttributes&) override
        {
            if (reject) {
                return Unexpected<Error<RequestErrorCode>>(RequestErrorCode::kUnauthorized, std::nullopt);
            }
            if (defer) {
                return Reply<SubscribeResponse, RequestErrorCode>::Defer(
                  [this](auto&& done) { subscribe_reply = std::move(done); });
            }
            publisher = PublishTrackHandler::Create(kTrack, TrackMode::kStream, 0, 0, 1000, { 0, 0 });
            session->BindPublisherTrack(1, id, publisher, false);
            return SubscribeResponse{};
        }
        Reply<const PublishResponse, PublishErrorCode> PublishReceived(
          const std::shared_ptr<Session>&,
          uint64_t,
          const PublishAttributes&,
          std::weak_ptr<SubscribeNamespaceHandler>) override
        {
            if (reject) {
                return Unexpected<Error<PublishErrorCode>>(PublishErrorCode::kRejected, std::nullopt);
            }
            if (defer) {
                return Reply<const PublishResponse, PublishErrorCode>::Defer(
                  [this](auto&& done) { publish_reply = std::move(done); });
            }
            receiver = std::make_shared<Receiver>();
            return PublishResponse{ {}, receiver };
        }
        Reply<const FetchResponse, FetchErrorCode> StandaloneFetchReceived(const std::shared_ptr<Session>& session,
                                                                           uint64_t id,
                                                                           const FullTrackName&,
                                                                           const StandaloneFetchAttributes&) override
        {
            if (reject) {
                return Unexpected<Error<FetchErrorCode>>(FetchErrorCode::kNoObjects, std::nullopt);
            }
            if (defer) {
                return Reply<const FetchResponse, FetchErrorCode>::Defer(
                  [this](auto&& done) { fetch_reply = std::move(done); });
            }
            fetch = PublishFetchHandler::Create(kTrack, 0, id, GroupOrder::kAscending, 1000);
            session->BindFetchTrack(fetch);
            return FetchResponse{ .largest_location = Location{ 1, 0 } };
        }
        Reply<const FetchResponse, FetchErrorCode> JoiningFetchReceived(
          const std::shared_ptr<Session>& session,
          uint64_t id,
          const FullTrackName& name,
          const JoiningFetchAttributes& attributes) override
        {
            joining_attributes = attributes;
            return StandaloneFetchReceived(session, id, name, {});
        }
        Reply<std::vector<TrackNamespace>, RequestErrorCode> SubscribeNamespaceReceived(
          const std::shared_ptr<Session>&,
          const TrackNamespace&,
          const SubscribeNamespaceAttributes&) override
        {
            if (reject) {
                return Unexpected<Error<RequestErrorCode>>(RequestErrorCode::kUnauthorized, std::nullopt);
            }
            if (defer) {
                return Reply<std::vector<TrackNamespace>, RequestErrorCode>::Defer(
                  [this](auto&& done) { namespace_reply = std::move(done); });
            }
            return std::vector<TrackNamespace>{};
        }
        Reply<void, PublishNamespaceErrorCode> PublishNamespaceReceived(const std::shared_ptr<Session>&,
                                                                        const TrackNamespace&,
                                                                        const PublishNamespaceAttributes&) override
        {
            if (reject) {
                return Unexpected<Error<PublishNamespaceErrorCode>>(PublishNamespaceErrorCode::kInternalError,
                                                                    std::nullopt);
            }
            if (defer) {
                return Reply<void, PublishNamespaceErrorCode>::Defer(
                  [this](auto&& done) { publish_namespace_reply = std::move(done); });
            }
            return {};
        }
        Reply<std::vector<TrackNamespace>, RequestErrorCode> SubscribeTracksReceived(
          const std::shared_ptr<Session>& session,
          const TrackNamespace& prefix,
          const SubscribeNamespaceAttributes& attributes) override
        {
            return SubscribeNamespaceReceived(session, prefix, attributes);
        }
        Reply<TrackStatusResponse, RequestErrorCode> TrackStatusReceived(const std::shared_ptr<Session>&,
                                                                         const FullTrackName&) override
        {
            if (defer) {
                return Reply<TrackStatusResponse, RequestErrorCode>::Defer(
                  [this](auto&& done) { status_reply = std::move(done); });
            }
            return Unexpected<Error<RequestErrorCode>>(RequestErrorCode::kUnauthorized, std::nullopt);
        }
        void FetchCancelReceived(const std::shared_ptr<Session>&, uint64_t) override { ++fetch_cancellations; }
        void UnsubscribeReceived(const std::shared_ptr<Session>&, uint64_t) override { ++cancellations; }
    };

    struct Endpoint
    {
        std::shared_ptr<RecordingTransport> transport = std::make_shared<RecordingTransport>();
        std::shared_ptr<MockConnection> connection = std::make_shared<MockConnection>();
        std::shared_ptr<Session> session;
        Endpoint(std::shared_ptr<ServerCallbacks> callbacks = nullptr)
        {
            if (callbacks) {
                session = Session::Create(ServerConfig{}, transport, connection, callbacks, nullptr);
            } else {
                session = Session::Create(ClientConfig{}, transport, connection, nullptr, nullptr);
            }
            connection->SetDelegate(session);
            connection->RecordSetupSent();
            connection->RecordSetupReceived();
        }
        void Receive(const RecordingTransport::Send& send)
        {
            auto stream = transport->Make(send.stream->GetStreamId(), connection);
            {
                std::lock_guard lock(stream->rx_mutex);
                stream->rx_data.Push(send.bytes);
            }
            connection->OnRecvStream(stream);
            if (send.fin) {
                connection->OnStreamClosed(stream, StreamClosedFlag::kFin);
            }
        }
        // Deliver the operation requested by the peer Session as directional transport events.
        void ReceiveClose(const std::pair<uint64_t, StreamOperation>& close)
        {
            const auto [id, operation] = close;
            switch (operation) {
                case StreamOperation::kCancel:
                    PeerClose(id, StreamClosedFlag::kStopSending);
                    PeerClose(id, StreamClosedFlag::kReset);
                    break;
                case StreamOperation::kReset:
                    PeerClose(id, StreamClosedFlag::kReset);
                    break;
                case StreamOperation::kStopSending:
                    PeerClose(id, StreamClosedFlag::kStopSending);
                    break;
                case StreamOperation::kFin:
                    PeerClose(id, StreamClosedFlag::kFin);
                    break;
            }
        }
        void PeerClose(uint64_t id, StreamClosedFlag flag)
        {
            connection->OnStreamClosed(transport->Make(id, connection), flag);
        }
    };

    struct Subscription
    {
        Endpoint client;
        std::shared_ptr<ServerCallbacks> callbacks = std::make_shared<ServerCallbacks>();
        Endpoint server{ callbacks };
        std::shared_ptr<SubscribeTrackHandler> subscriber = std::make_shared<Receiver>();
        uint64_t id = 0;
        Subscription(bool pending = false)
        {
            callbacks->defer = pending;
            client.session->SubscribeTrack(subscriber);
            id = *subscriber->GetRequestStreamId();
            server.Receive(client.transport->sends.back());
            if (!pending) {
                client.Receive(server.transport->sends.back());
            }
        }
    };
}

// Draft-18 sections 3.3.2 and 5.1.1.
TEST_CASE("Request lifecycle - Sent Unsubscribe - Clear request binding")
{
    Subscription pair;
    SUBCASE("Active") {}
    SUBCASE("Paused")
    {
        pair.subscriber->Pause();
    }
    const auto id = pair.id;
    pair.client.session->UnsubscribeTrack(pair.subscriber);
    CHECK(pair.client.transport->Closed(id, StreamOperation::kCancel));
    CHECK_FALSE(pair.subscriber->GetRequestId());
    CHECK_FALSE(pair.subscriber->GetRequestStreamId());
    CHECK_FALSE(pair.subscriber->GetReceivedTrackAlias());
    CHECK_NOTHROW(pair.client.session->UnsubscribeTrack(pair.subscriber));
}

// Draft-18 sections 5.1.1.
TEST_CASE("Request lifecycle - Received Unsubscribe - Reset all subgroups")
{
    Subscription pair;
    REQUIRE(pair.callbacks->publisher);
    for (uint64_t group : { 0, 1 }) {
        auto headers = kObject;
        headers.group_id = group;
        REQUIRE(pair.callbacks->publisher->PublishObject(headers, kPayload) ==
                PublishTrackHandler::PublishObjectStatus::kOk);
    }
    const auto first = pair.server.transport->sends.at(1).stream->GetStreamId();
    const auto second = pair.server.transport->sends.at(2).stream->GetStreamId();
    pair.client.session->UnsubscribeTrack(pair.subscriber);
    REQUIRE_FALSE(pair.client.transport->closes.empty());
    pair.server.ReceiveClose(pair.client.transport->closes.back());
    CHECK(pair.server.transport->Closed(pair.id, StreamOperation::kCancel));
    CHECK(pair.server.transport->Closed(first, StreamOperation::kReset));
    CHECK(pair.server.transport->Closed(second, StreamOperation::kReset));
    CHECK(pair.callbacks->publisher->PublishObject(kObject, kPayload) != PublishTrackHandler::PublishObjectStatus::kOk);
    // ReceiveClose delivers both STOP_SENDING and RESET, but notification must happen once.
    CHECK(pair.callbacks->cancellations == 1);
    std::weak_ptr<TrackHandler> released = pair.callbacks->publisher;
    pair.callbacks->publisher.reset();
    CHECK(released.expired());
}

// Draft-18 sections 3.3.2.
TEST_CASE("Request lifecycle - Received Unsubscribe - Ignore deferred subscribe response")
{
    Subscription pair(true);
    REQUIRE(pair.callbacks->subscribe_reply);
    pair.client.session->UnsubscribeTrack(pair.subscriber);
    REQUIRE_FALSE(pair.client.transport->closes.empty());
    pair.server.ReceiveClose(pair.client.transport->closes.back());
    CHECK(pair.server.transport->Closed(pair.id, StreamOperation::kCancel));
    std::exchange(pair.callbacks->subscribe_reply, {})(SubscribeResponse{});
    pair.server.transport->RunReplies();
    CHECK(pair.server.transport->sends.empty());
}

// Draft-18 sections 3.3.2 and 5.1.
TEST_CASE("Request lifecycle - Sent Unsubscribe - Cancel received publish")
{
    Endpoint publisher;
    auto callbacks = std::make_shared<ServerCallbacks>();
    Endpoint subscriber(callbacks);
    auto handler = PublishTrackHandler::Create(kTrack, TrackMode::kStream, 0, 0, 1000, { 0, 0 });
    publisher.session->PublishTrack(handler);
    subscriber.Receive(publisher.transport->sends.back());
    REQUIRE(callbacks->receiver);
    const auto id = callbacks->receiver->GetRequestStreamId();
    REQUIRE(id);
    publisher.Receive(subscriber.transport->sends.back());
    REQUIRE(handler->PublishObject(kObject, kPayload) == PublishTrackHandler::PublishObjectStatus::kOk);
    const auto data_id = publisher.transport->sends.back().stream->GetStreamId();
    subscriber.session->UnsubscribeTrack(callbacks->receiver);
    CHECK(subscriber.transport->Closed(*id, StreamOperation::kCancel));
    CHECK_FALSE(callbacks->receiver->GetRequestStreamId());
    REQUIRE_FALSE(subscriber.transport->closes.empty());
    publisher.ReceiveClose(subscriber.transport->closes.back());
    CHECK(publisher.transport->Closed(data_id, StreamOperation::kReset));
    CHECK(handler->PublishObject(kObject, kPayload) != PublishTrackHandler::PublishObjectStatus::kOk);
}

// Draft-18 sections 6.1.
TEST_CASE("Request lifecycle - Received Unsubscribe Namespace - Close remaining direction")
{
    for (const auto mode : { SubscribeNamespaceHandler::Mode::kNamespaces, SubscribeNamespaceHandler::Mode::kTracks }) {
        for (const auto flag : { StreamClosedFlag::kFin, StreamClosedFlag::kReset }) {
            CAPTURE(static_cast<int>(mode));
            CAPTURE(static_cast<int>(flag));
            Endpoint client;
            auto callbacks = std::make_shared<ServerCallbacks>();
            Endpoint server(callbacks);
            auto ns = SubscribeNamespaceHandler::Create(kTrack.name_space, mode);
            client.session->SubscribeNamespace(ns);
            const auto id = *ns->GetRequestStreamId();
            server.Receive(client.transport->sends.back());
            if (flag == StreamClosedFlag::kFin) {
                // UnsubscribeNamespace cancels abruptly; FIN is a separate permitted peer action.
                server.PeerClose(id, flag);
            } else {
                client.session->UnsubscribeNamespace(ns);
                REQUIRE_FALSE(client.transport->closes.empty());
                server.ReceiveClose(client.transport->closes.back());
            }
            const auto operation = flag == StreamClosedFlag::kFin ? StreamOperation::kFin : StreamOperation::kCancel;
            CHECK(server.transport->Closed(id, operation));
        }
    }
}

// Draft-18 sections 3.3.2.
TEST_CASE("Request lifecycle - Sent Unsubscribe - Cancel pending subscribe")
{
    Endpoint client;
    auto handler = std::make_shared<Receiver>();
    client.session->SubscribeTrack(handler);
    const auto id = *handler->GetRequestStreamId();
    client.session->UnsubscribeTrack(handler);
    CHECK(client.transport->Closed(id, StreamOperation::kCancel));
    CHECK_FALSE(handler->GetRequestStreamId());
    CHECK_FALSE(handler->GetRequestId());
}

// Draft-18 sections 3.3.2.
TEST_CASE("Request lifecycle - Received Publish Cancel - Ignore deferred publish response")
{
    Endpoint client;
    auto callbacks = std::make_shared<ServerCallbacks>();
    callbacks->defer = true;
    Endpoint server(callbacks);
    auto publisher = PublishTrackHandler::Create(kTrack, TrackMode::kStream, 0, 0, 1000, { 0, 0 });
    client.session->PublishTrack(publisher);
    const auto id = *publisher->GetRequestStreamId();
    server.Receive(client.transport->sends.back());
    REQUIRE(callbacks->publish_reply);
    // TODO: Replace direct injection with the public abrupt PUBLISH cancellation API when available.
    server.PeerClose(id, StreamClosedFlag::kReset);
    CHECK(server.transport->Closed(id, StreamOperation::kCancel));
    auto receiver = std::make_shared<Receiver>();
    std::exchange(callbacks->publish_reply, {})(PublishResponse{ {}, receiver });
    server.transport->RunReplies();
    CHECK(server.transport->sends.empty());
    CHECK_FALSE(receiver->GetRequestStreamId());
}

// Draft-18 sections 6.2.
TEST_CASE("Request lifecycle - Publish Namespace Done - Cancel namespace request")
{
    Endpoint client;
    auto callbacks = std::make_shared<ServerCallbacks>();
    Endpoint server(callbacks);
    auto handler = PublishNamespaceHandler::Create(kTrack.name_space);
    client.session->PublishNamespace(handler);
    const auto id = *handler->GetRequestStreamId();
    server.Receive(client.transport->sends.back());
    client.session->PublishNamespaceDone(handler);
    CHECK(client.transport->Closed(id, StreamOperation::kCancel));
    CHECK_FALSE(handler->GetRequestId());
    CHECK_FALSE(handler->GetRequestStreamId());
    CHECK_NOTHROW(client.session->PublishNamespaceDone(handler));
    REQUIRE_FALSE(client.transport->closes.empty());
    server.ReceiveClose(client.transport->closes.back());
    CHECK(server.transport->Closed(id, StreamOperation::kCancel));
}

// Draft-18 sections 3.3.2.
TEST_CASE("Request lifecycle - Request rejection - Send error before FIN")
{
    Endpoint client;
    auto callbacks = std::make_shared<ServerCallbacks>();
    callbacks->reject = true;
    Endpoint server(callbacks);
    std::shared_ptr<TrackHandler> handler;
    SUBCASE("SUBSCRIBE")
    {
        auto h = std::make_shared<Receiver>();
        handler = h;
        client.session->SubscribeTrack(h);
    }
    SUBCASE("PUBLISH")
    {
        auto h = PublishTrackHandler::Create(kTrack, TrackMode::kStream, 0, 0, 1000, { 0, 0 });
        handler = h;
        client.session->PublishTrack(h);
    }
    SUBCASE("FETCH")
    {
        auto h = FetchTrackHandler::Create(kTrack, 0, { 0, 0 }, { 1, 0 });
        handler = h;
        client.session->FetchTrack(h);
    }
    SUBCASE("SUBSCRIBE_NAMESPACE")
    {
        auto h = SubscribeNamespaceHandler::Create(kTrack.name_space, SubscribeNamespaceHandler::Mode::kNamespaces);
        handler = h;
        client.session->SubscribeNamespace(h);
    }
    SUBCASE("SUBSCRIBE_TRACKS")
    {
        auto h = SubscribeNamespaceHandler::Create(kTrack.name_space, SubscribeNamespaceHandler::Mode::kTracks);
        handler = h;
        client.session->SubscribeNamespace(h);
    }
    SUBCASE("PUBLISH_NAMESPACE")
    {
        auto h = PublishNamespaceHandler::Create(kTrack.name_space);
        handler = h;
        client.session->PublishNamespace(h);
    }
    SUBCASE("TRACK_STATUS")
    {
        auto h = TrackStatusHandler::Create(kTrack);
        handler = h;
        client.session->RequestTrackStatus(h);
    }
    REQUIRE(handler);
    const auto request = client.transport->sends.back();
    server.Receive(request);
    REQUIRE(server.transport->sends.size() == 1);
    const auto response = server.transport->sends.front();
    BytesSpan bytes(response.bytes);
    CHECK(Message::ParseField<uint64_t>(bytes) == static_cast<uint64_t>(ControlMessageType::kRequestError));
    CHECK(response.fin);
    CHECK(server.transport->closes.empty()); // FIN is ordered with the error bytes, not a direct close.
    client.Receive(response);
    CHECK_FALSE(handler->GetRequestStreamId());
    if (request.fin) {
        // TRACK_STATUS already ended the client's send direction with the request.
        CHECK(client.transport->closes.empty());
    } else {
        CHECK(client.transport->Closed(request.stream->GetStreamId(), StreamOperation::kFin));
        REQUIRE_FALSE(client.transport->closes.empty());
        server.ReceiveClose(client.transport->closes.back());
    }
    std::weak_ptr<TrackHandler> released = handler;
    handler.reset();
    CHECK(released.expired());
}

// Draft-18 sections 10.14 and 3.3.2.
TEST_CASE("Request lifecycle - Received Track Status FIN - Preserve deferred response")
{
    Endpoint client;
    auto callbacks = std::make_shared<ServerCallbacks>();
    callbacks->defer = true;
    Endpoint server(callbacks);
    auto query = TrackStatusHandler::Create(kTrack);
    client.session->RequestTrackStatus(query);
    const auto request = client.transport->sends.back();
    REQUIRE(request.fin);
    server.Receive(request);
    REQUIRE(callbacks->status_reply);
    bool cancelled = false;
    SUBCASE("Complete") {}
    SUBCASE("Cancel after FIN")
    {
        cancelled = true;
        client.session->CancelTrackStatus(query);
        CHECK(client.transport->Closed(request.stream->GetStreamId(), StreamOperation::kCancel));
        CHECK_FALSE(query->GetRequestStreamId());
        CHECK(query->GetStatus() == TrackStatusHandler::Status::kDoneByReset);
        CHECK_NOTHROW(client.session->CancelTrackStatus(query));
        REQUIRE_FALSE(client.transport->closes.empty());
        server.ReceiveClose(client.transport->closes.back());
        CHECK(server.transport->Closed(request.stream->GetStreamId(), StreamOperation::kCancel));
    }
    std::exchange(callbacks->status_reply, {})(TrackStatusResponse{});
    server.transport->RunReplies();
    if (cancelled) {
        CHECK(server.transport->sends.empty());
    } else {
        REQUIRE(server.transport->sends.size() == 1);
        CHECK(server.transport->sends.front().fin);
        client.Receive(server.transport->sends.front());
        CHECK(query->GetResponse().has_value());
        CHECK_FALSE(query->GetRequestStreamId());
    }
}

// Draft-18 sections 6.1 and 6.2.
TEST_CASE("Request lifecycle - Received Namespace Cancel - Ignore deferred namespace response")
{
    Endpoint client;
    auto callbacks = std::make_shared<ServerCallbacks>();
    callbacks->defer = true;
    Endpoint server(callbacks);
    std::shared_ptr<SubscribeNamespaceHandler> subscription;
    std::shared_ptr<PublishNamespaceHandler> publication;
    SUBCASE("SUBSCRIBE_NAMESPACE")
    {
        subscription =
          SubscribeNamespaceHandler::Create(kTrack.name_space, SubscribeNamespaceHandler::Mode::kNamespaces);
        client.session->SubscribeNamespace(subscription);
    }
    SUBCASE("SUBSCRIBE_TRACKS")
    {
        subscription = SubscribeNamespaceHandler::Create(kTrack.name_space, SubscribeNamespaceHandler::Mode::kTracks);
        client.session->SubscribeNamespace(subscription);
    }
    SUBCASE("PUBLISH_NAMESPACE")
    {
        publication = PublishNamespaceHandler::Create(kTrack.name_space);
        client.session->PublishNamespace(publication);
    }
    const auto request = client.transport->sends.back();
    server.Receive(request);
    REQUIRE(server.transport->sends.empty());
    if (publication) {
        client.session->PublishNamespaceDone(publication);
    } else {
        client.session->UnsubscribeNamespace(subscription);
    }
    REQUIRE_FALSE(client.transport->closes.empty());
    server.ReceiveClose(client.transport->closes.back());
    CHECK(server.transport->Closed(request.stream->GetStreamId(), StreamOperation::kCancel));
    if (publication) {
        REQUIRE(callbacks->publish_namespace_reply);
        std::exchange(callbacks->publish_namespace_reply, {})({});
    } else {
        REQUIRE(callbacks->namespace_reply);
        std::exchange(callbacks->namespace_reply, {})(std::vector<TrackNamespace>{});
    }
    server.transport->RunReplies();
    CHECK(server.transport->sends.empty());
}
