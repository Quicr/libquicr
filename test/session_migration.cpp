// SPDX-FileCopyrightText: Copyright (c) 2026 Cisco Systems
// SPDX-License-Identifier: BSD-2-Clause

#include "quicr/handlers/subscribe_track_handler.h"
#include "quicr/session.h"
#include "stream.h"

#include <doctest/doctest.h>
#include <timeq/tick_service.h>

#include <string>
#include <utility>

using namespace quicr;

namespace quicr {
    class SessionMigrationTestPeer
    {
      public:
        static void Migrate(const std::shared_ptr<Session>& session, std::string uri)
        {
            Session::Redirect redirect;
            redirect.uri = std::move(uri);
            session->Migrate(redirect);
        }

        static void PrepareMigration(const std::shared_ptr<Session>& session,
                                     const std::shared_ptr<TrackHandler>& handler,
                                     const std::shared_ptr<Stream>& new_stream,
                                     std::uint64_t request_id)
        {
            const auto old_binding = handler->GetBinding();
            REQUIRE(old_binding.request_id == request_id);
            REQUIRE(old_binding.request_stream != nullptr);

            std::lock_guard lock(session->state_mutex_);
            session->request_by_stream[{ new_stream->GetConnectionID(), new_stream->GetStreamId() }] = {
                .request_id = request_id,
                .is_request_stream = true,
            };
            session->pending_migrations_[{ new_stream->GetConnectionID(), request_id }] = {
                .old_connection_id = old_binding.connection_id,
                .new_connection_id = new_stream->GetConnectionID(),
                .request_id = request_id,
                .type = Session::MigrationRequestType::kSubscribe,
                .request_stream = new_stream,
            };
        }

        static void AddConnection(const std::shared_ptr<Session>& session,
                                  const std::shared_ptr<Connection>& connection)
        {
            std::lock_guard lock(session->connections_mutex_);
            session->connections_[connection->GetID()] = connection;
        }

        static void CompleteMigration(const std::shared_ptr<Session>& session,
                                      const std::shared_ptr<Connection>& connection,
                                      std::uint64_t request_id,
                                      std::uint64_t received_alias)
        {
            session->CompleteMigration(connection, request_id, received_alias);
        }

        static bool FailMigration(const std::shared_ptr<Session>& session,
                                  const std::shared_ptr<Connection>& connection,
                                  std::uint64_t request_id)
        {
            return session->FailMigration(connection, request_id);
        }
    };
}

namespace {
    class TestConnection final : public Connection
    {
      public:
        explicit TestConnection(std::uint64_t id)
          : Connection(id)
        {
        }

        QuicMetricsSample TakeMetricsSample() override { return {}; }
        void ReportMetricsSample(const MetricsTimeStamp&, const QuicMetricsSample&) override {}
    };

    class TestStream final : public Stream
    {
      public:
        TestStream(std::uint64_t stream_id, const std::shared_ptr<Connection>& connection)
          : Stream(stream_id, connection)
        {
        }
    };

    class TestTickService final : public timeq::tick_service
    {
      public:
        std::chrono::microseconds get() const override { return {}; }
    };

    class TestTransport final : public Transport
    {
      public:
        explicit TestTransport(std::shared_ptr<TestConnection> redirected)
          : redirected_(std::move(redirected))
        {
        }

        TransportStatus Status() const override { return TransportStatus::kReady; }
        std::shared_ptr<Connection> Start() override { return nullptr; }
        std::shared_ptr<Connection> Connect(const TransportRemote& remote) override
        {
            connect_remote = remote;
            connect_count++;
            return redirected_;
        }
        void Close(const std::shared_ptr<Connection>& connection, AppReasonForClose) override
        {
            closed_connections.push_back(connection->GetID());
        }
        void CloseStream(const std::shared_ptr<Stream>& stream, StreamOperation) override
        {
            closed_streams.emplace_back(stream->GetConnectionID(), stream->GetStreamId());
        }
        bool GetPeerAddrInfo(const std::shared_ptr<Connection>&, sockaddr_storage*) override { return false; }
        TransportError Enqueue(const std::shared_ptr<Connection>& connection,
                               const std::shared_ptr<Stream>& stream,
                               std::shared_ptr<const std::vector<uint8_t>>,
                               uint8_t,
                               uint32_t,
                               EnqueueFlags) override
        {
            enqueue_connection = connection;
            enqueue_stream = stream;
            return TransportError::kNone;
        }
        TransportError EnqueueDatagram(const std::shared_ptr<Connection>&,
                                       std::shared_ptr<const std::vector<uint8_t>>,
                                       uint8_t,
                                       uint32_t) override
        {
            return TransportError::kNone;
        }
        std::shared_ptr<const std::vector<uint8_t>> Dequeue(const std::shared_ptr<Connection>&) override
        {
            return nullptr;
        }
        int CloseWebTransportSession(const std::shared_ptr<Connection>&, uint32_t, const char*) override { return 0; }
        int DrainWebTransportSession(const std::shared_ptr<Connection>&) override { return 0; }
        std::shared_ptr<Stream> CreateDataStream(const std::shared_ptr<Connection>& connection, uint8_t) override
        {
            return std::make_shared<TestStream>(2, connection);
        }
        std::shared_ptr<Stream> CreateControlStream(const std::shared_ptr<Connection>& connection) override
        {
            return std::make_shared<TestStream>(2, connection);
        }
        std::shared_ptr<Stream> CreateRequestStream(const std::shared_ptr<Connection>& connection) override
        {
            const auto stream_id = next_request_stream_id_;
            next_request_stream_id_ += 4;
            return std::make_shared<TestStream>(stream_id, connection);
        }

        std::size_t connect_count{ 0 };
        TransportRemote connect_remote;
        std::shared_ptr<Connection> enqueue_connection;
        std::shared_ptr<Stream> enqueue_stream;
        std::vector<std::uint64_t> closed_connections;
        std::vector<std::pair<std::uint64_t, std::uint64_t>> closed_streams;

      private:
        void QueueDeferredReply(std::function<void()>&&) override {}

        std::shared_ptr<TestConnection> redirected_;
        std::uint64_t next_request_stream_id_{ 0 };
    };
}

TEST_CASE("Session opens a redirect on its existing transport")
{
    const auto initial = std::make_shared<TestConnection>(1);
    const auto redirected = std::make_shared<TestConnection>(2);
    const auto transport = std::make_shared<TestTransport>(redirected);
    ClientConfig config;
    config.connect_uri = "moqt://old.example:4443";
    const auto session = Session::Create(config, transport, initial, nullptr, std::make_shared<TestTickService>());
    initial->SetDelegate(session);

    SessionMigrationTestPeer::Migrate(session, "moqt://new.example:5555");

    CHECK(transport->connect_count == 1);
    CHECK(transport->connect_remote.host_or_ip == "new.example");
    CHECK(transport->connect_remote.port == 5555);

    redirected->SetStatus(Connection::Status::kReady);
    CHECK(transport->enqueue_connection == redirected);
    REQUIRE(transport->enqueue_stream != nullptr);
    CHECK(transport->enqueue_stream->GetConnectionID() == redirected->GetID());
    CHECK(redirected->GetSetupState() == Connection::SetupState::kSent);
}

TEST_CASE("Session cuts handlers over independently and drains the old connection last")
{
    const auto initial = std::make_shared<TestConnection>(1);
    const auto redirected = std::make_shared<TestConnection>(2);
    const auto transport = std::make_shared<TestTransport>(redirected);
    ClientConfig config;
    config.connect_uri = "moqt://old.example:4443";
    const auto session = Session::Create(config, transport, initial, nullptr, std::make_shared<TestTickService>());
    SessionMigrationTestPeer::AddConnection(session, redirected);

    const auto first = SubscribeTrackHandler::Create({ {}, {} }, 0);
    const auto second = SubscribeTrackHandler::Create({ {}, {} }, 0);
    const auto first_new_stream = std::make_shared<TestStream>(0, redirected);
    const auto second_new_stream = std::make_shared<TestStream>(4, redirected);
    session->SubscribeTrack(first);
    session->SubscribeTrack(second);
    SessionMigrationTestPeer::PrepareMigration(session, first, first_new_stream, 0);
    SessionMigrationTestPeer::PrepareMigration(session, second, second_new_stream, 2);

    SessionMigrationTestPeer::CompleteMigration(session, redirected, 0, 10);

    CHECK(first->GetConnectionId() == redirected->GetID());
    CHECK(first->GetRequestId() == 0);
    CHECK(first->GetBinding().request_stream == first_new_stream);
    CHECK(second->GetConnectionId() == initial->GetID());
    CHECK(transport->closed_connections.empty());

    SessionMigrationTestPeer::CompleteMigration(session, redirected, 2, 11);

    CHECK(second->GetConnectionId() == redirected->GetID());
    CHECK(second->GetRequestId() == 2);
    CHECK(second->GetBinding().request_stream == second_new_stream);
    REQUIRE(transport->closed_connections.size() == 1);
    CHECK(transport->closed_connections.front() == initial->GetID());
}

TEST_CASE("Session migration failure leaves the old handler binding active")
{
    const auto initial = std::make_shared<TestConnection>(1);
    const auto redirected = std::make_shared<TestConnection>(2);
    const auto transport = std::make_shared<TestTransport>(redirected);
    ClientConfig config;
    config.connect_uri = "moqt://old.example:4443";
    const auto session = Session::Create(config, transport, initial, nullptr, std::make_shared<TestTickService>());
    SessionMigrationTestPeer::AddConnection(session, redirected);

    const auto handler = SubscribeTrackHandler::Create({ {}, {} }, 0);
    session->SubscribeTrack(handler);
    const auto old_stream = handler->GetBinding().request_stream;
    const auto new_stream = std::make_shared<TestStream>(0, redirected);
    SessionMigrationTestPeer::PrepareMigration(session, handler, new_stream, 0);

    CHECK(SessionMigrationTestPeer::FailMigration(session, redirected, 0));
    CHECK(handler->GetConnectionId() == initial->GetID());
    CHECK(handler->GetRequestId() == 0);
    CHECK(handler->GetBinding().request_stream == old_stream);
    REQUIRE(transport->closed_connections.size() == 1);
    CHECK(transport->closed_connections.front() == redirected->GetID());
}
