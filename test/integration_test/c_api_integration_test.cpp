// SPDX-FileCopyrightText: Copyright (c) 2026 Cisco Systems
// SPDX-License-Identifier: BSD-2-Clause

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include "c_api_scenarios.h"
#include "integration_test_support.h"
#include "test_server.h"

#include "quicr/config.h"
#include "quicr/handlers/publish_namespace_handler.h"
#include "quicr/handlers/publish_track_handler.h"
#include "quicr/handlers/subscribe_namespace_handler.h"
#include "quicr/session_manager.h"
#include "quicr/utilities/format.h"

#include <atomic>
#include <chrono>
#include <future>
#include <memory>
#include <new>
#include <semaphore>
#include <string>
#include <thread>
#include <variant>
#include <vector>

struct quicr_test_event
{
    std::counting_semaphore<> signals{ 0 };
};

extern "C" quicr_test_event_t*
quicr_test_event_create(void)
{
    return new (std::nothrow) quicr_test_event_t;
}

extern "C" void
quicr_test_event_destroy(quicr_test_event_t* event)
{
    delete event;
}

extern "C" void
quicr_test_event_signal(quicr_test_event_t* event)
{
    if (event == nullptr) {
        return;
    }

    event->signals.release();
}

extern "C" bool
quicr_test_event_wait(quicr_test_event_t* event, const uint32_t timeout_ms)
{
    if (event == nullptr) {
        return false;
    }

    return event->signals.try_acquire_for(std::chrono::milliseconds(timeout_ms));
}

namespace {

    class ReadyClientCallbacks final : public quicr::Session::ClientCallbacks
    {
      public:
        std::future<void> Ready() { return ready_.get_future(); }

      private:
        void StatusChanged(const std::shared_ptr<quicr::Session>&, const quicr::Session::Status status) override
        {
            if (status == quicr::Session::Status::kReady && !ready_signaled_.test_and_set()) {
                ready_.set_value();
            }
        }

        std::promise<void> ready_;
        std::atomic_flag ready_signaled_ = ATOMIC_FLAG_INIT;
    };

    class JoiningSubscribeHandler final : public quicr::SubscribeTrackHandler
    {
      public:
        static std::shared_ptr<JoiningSubscribeHandler> Create(const quicr::FullTrackName& full_track_name)
        {
            const JoiningFetch joining_fetch{
                .priority = 0,
                .group_order = quicr::messages::GroupOrder::kAscending,
                .parameters = {},
                .joining_start = 0,
                .absolute = true,
            };
            return std::shared_ptr<JoiningSubscribeHandler>(
              new JoiningSubscribeHandler(full_track_name, joining_fetch));
        }

        std::future<void> Ready() { return ready_.get_future(); }

      private:
        JoiningSubscribeHandler(const quicr::FullTrackName& full_track_name, const JoiningFetch& joining_fetch)
          : SubscribeTrackHandler(full_track_name,
                                  0,
                                  quicr::messages::GroupOrder::kAscending,
                                  quicr::messages::SubscriptionFilter{
                                    .type = quicr::messages::SubscriptionFilterType::kAbsoluteRange,
                                    .start_location = { 2, 3 },
                                    .end_group_delta = 4,
                                  },
                                  joining_fetch)
        {
        }

        void StatusChanged(const Status status) override
        {
            if (status == Status::kOk && !ready_signaled_.test_and_set()) {
                ready_.set_value();
            }
        }

        std::promise<void> ready_;
        std::atomic_flag ready_signaled_ = ATOMIC_FLAG_INIT;
    };

    class SubscribeTracksHandler final : public quicr::SubscribeNamespaceHandler
    {
      public:
        static std::shared_ptr<SubscribeTracksHandler> Create(const quicr::TrackNamespace& prefix,
                                                              const quicr::messages::TrackFilter& filter)
        {
            return std::shared_ptr<SubscribeTracksHandler>(new SubscribeTracksHandler(prefix, filter));
        }

        std::future<void> Failed() { return failed_.get_future(); }

      private:
        SubscribeTracksHandler(const quicr::TrackNamespace& prefix, const quicr::messages::TrackFilter& filter)
          : SubscribeNamespaceHandler(prefix, Mode::kTracks, filter)
        {
        }

        void StatusChanged(const Status status) override
        {
            if (status == Status::kError && !failed_signaled_.test_and_set()) {
                failed_.set_value();
            }
        }

        std::promise<void> failed_;
        std::atomic_flag failed_signaled_ = ATOMIC_FLAG_INIT;
    };

    class NamespaceNotificationHandler final : public quicr::SubscribeNamespaceHandler
    {
      public:
        static std::shared_ptr<NamespaceNotificationHandler> Create(const quicr::TrackNamespace& prefix)
        {
            return std::shared_ptr<NamespaceNotificationHandler>(new NamespaceNotificationHandler(prefix));
        }

        std::size_t NamespacesReceived() const noexcept { return namespaces_received_.load(); }
        std::size_t NamespacesDone() const noexcept { return namespaces_done_.load(); }
        std::future<void> Ready() { return ready_.get_future(); }

      private:
        explicit NamespaceNotificationHandler(const quicr::TrackNamespace& prefix)
          : SubscribeNamespaceHandler(prefix, Mode::kNamespaces)
        {
        }

        void StatusChanged(const Status status) override
        {
            if (status == Status::kOk && !ready_signaled_.test_and_set()) {
                ready_.set_value();
            }
        }

        void NamespaceReceived(const quicr::TrackNamespace&) override { ++namespaces_received_; }
        void NamespaceDoneReceived(const quicr::TrackNamespace&) override { ++namespaces_done_; }

        std::atomic<std::size_t> namespaces_received_{ 0 };
        std::atomic<std::size_t> namespaces_done_{ 0 };
        std::promise<void> ready_;
        std::atomic_flag ready_signaled_ = ATOMIC_FLAG_INIT;
    };

    class AwaitablePublishNamespaceHandler final : public quicr::PublishNamespaceHandler
    {
      public:
        static std::shared_ptr<AwaitablePublishNamespaceHandler> Create(const quicr::TrackNamespace& prefix)
        {
            return std::shared_ptr<AwaitablePublishNamespaceHandler>(new AwaitablePublishNamespaceHandler(prefix));
        }

        std::future<void> Ready() { return ready_.get_future(); }

      private:
        explicit AwaitablePublishNamespaceHandler(const quicr::TrackNamespace& prefix)
          : PublishNamespaceHandler(prefix)
        {
        }

        void StatusChanged(const Status status) override
        {
            if (status == Status::kOk && !ready_signaled_.test_and_set()) {
                ready_.set_value();
            }
        }

        std::promise<void> ready_;
        std::atomic_flag ready_signaled_ = ATOMIC_FLAG_INIT;
    };

    void CheckResult(const quicr_c_test_result_t result)
    {
        INFO(quicr::format("C integration check failed at line {}: {}",
                           result.line,
                           result.expression == nullptr ? "<unknown>" : result.expression));
        REQUIRE(result.line == 0);
    }

} // namespace

TEST_CASE("C sessions and track handlers interoperate with the C++ relay")
{
    auto config = quicr_test::MakeTestServerConfig();
    config.transport_config.metrics_sample_ms = 10;
    config.transport_config.max_connections = 32;

    quicr::SessionManager manager;
    const auto server = std::make_shared<quicr_test::TestServer>();

    quicr_test::TestServer::FetchResponseData fetch_data;
    fetch_data.headers.group_id = 1;
    fetch_data.headers.object_id = 2;
    fetch_data.headers.payload_length = 3;
    fetch_data.payload = { 1, 2, 3 };
    server->SetFetchResponseData({ fetch_data });
    server->AddKnownPublishedNamespace(quicr::TrackNamespace(std::vector<std::string>{ "c-api", "known" }));
    server->SetTrackStatusResponse(quicr::TrackStatusResponse{
      .largest_location = quicr::messages::Location{ 1, 2 },
    });

    manager.AddTransport(config, server);

    const auto connect_uri = quicr::format("moq://{}:{}/relay", quicr_test::kIp, quicr_test::GetTestPort());
    CheckResult(quicr_test_c_connection(connect_uri.c_str()));
    CheckResult(quicr_test_c_fetch_namespace_status(connect_uri.c_str()));
    CheckResult(quicr_test_c_publish_subscribe(connect_uri.c_str(), false, false));
    CheckResult(quicr_test_c_publish_subscribe(connect_uri.c_str(), true, false));
    CheckResult(quicr_test_c_publish_subscribe(connect_uri.c_str(), false, true));

    const auto webtransport_uri = quicr::format("moqt://{}:{}/relay", quicr_test::kIp, quicr_test::GetTestPort());
    CheckResult(quicr_test_c_connection(webtransport_uri.c_str()));
    CheckResult(quicr_test_c_publish_subscribe(webtransport_uri.c_str(), false, false));

    server->SetSubscribeError(quicr::RequestErrorCode::kDoesNotExist, "intentional subscribe rejection");
    server->SetPublishError(quicr::PublishErrorCode::kRejected, "intentional publish rejection");
    server->SetTrackStatusError(quicr::Error<quicr::RequestErrorCode>{
      quicr::RequestErrorCode::kInternalError,
      "intentional track-status rejection",
    });
    CheckResult(quicr_test_c_rejections(connect_uri.c_str()));

    const auto c_server_port = static_cast<std::uint16_t>(quicr_test::GetTestPort() + 1);
    const auto c_server_uri = quicr::format("moq://{}:{}/relay", quicr_test::kIp, c_server_port);
    CheckResult(quicr_test_c_server_fetch(c_server_uri.c_str(), c_server_port));

    const auto joining_server_port = static_cast<std::uint16_t>(c_server_port + 1);
    const auto joining_server_uri = quicr::format("moq://{}:{}/relay", quicr_test::kIp, joining_server_port);
    quicr_test_joining_server_t* joining_server = nullptr;
    CheckResult(quicr_test_c_joining_server_start(joining_server_port, &joining_server));
    const std::unique_ptr<quicr_test_joining_server_t, decltype(&quicr_test_c_joining_server_destroy)>
      joining_server_owner(joining_server, quicr_test_c_joining_server_destroy);

    {
        quicr::SessionManager joining_client_manager;
        auto joining_client_callbacks = std::make_shared<ReadyClientCallbacks>();
        auto client_ready = joining_client_callbacks->Ready();

        auto joining_config = quicr_test::MakeTestClientConfig("moq");
        joining_config.connect_uri = joining_server_uri;
        const auto joining_session =
          joining_client_manager.AddTransport(joining_config, joining_client_callbacks).lock();
        REQUIRE(joining_session != nullptr);
        REQUIRE(client_ready.wait_for(std::chrono::seconds(10)) == std::future_status::ready);

        const quicr::FullTrackName joining_track{
            quicr::TrackNamespace(std::vector<std::string>{ "c-api", "joining" }),
            { 1, 2, 3 },
        };
        const auto namespace_handler = SubscribeTracksHandler::Create(joining_track.name_space,
                                                                      quicr::messages::TrackFilter{
                                                                        .property_type = 5,
                                                                        .max_tracks_selected = 6,
                                                                        .timeout = 7,
                                                                      });
        auto namespace_failed = namespace_handler->Failed();
        joining_session->SubscribeNamespace(namespace_handler);
        const auto notification_handler = NamespaceNotificationHandler::Create(joining_track.name_space);
        auto notification_ready = notification_handler->Ready();
        joining_session->SubscribeNamespace(notification_handler);
        const auto joining_handler = JoiningSubscribeHandler::Create(joining_track);
        auto joining_ready = joining_handler->Ready();
        joining_client_manager.AddHandler(joining_session, joining_handler);

        REQUIRE(namespace_failed.wait_for(std::chrono::seconds(10)) == std::future_status::ready);
        REQUIRE(namespace_handler->GetError().has_value());
        CHECK(namespace_handler->GetError()->reason_code == quicr::ErrorCode::kGoingAway);

        REQUIRE(notification_ready.wait_for(std::chrono::seconds(10)) == std::future_status::ready);
        REQUIRE(joining_ready.wait_for(std::chrono::seconds(10)) == std::future_status::ready);
        REQUIRE(quicr_test_c_joining_server_send_namespace(joining_server));

        for (std::size_t attempt = 0; attempt < 10000 && (notification_handler->NamespacesReceived() != 1 ||
                                                          notification_handler->NamespacesDone() != 1);
             ++attempt) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }

        CHECK(notification_handler->NamespacesReceived() == 1);
        CHECK(notification_handler->NamespacesDone() == 1);

        const auto publish_handler =
          quicr::PublishTrackHandler::Create(joining_track, quicr::TrackMode::kStream, 0, 0, 1000, { 0, 0 });
        joining_client_manager.AddHandler(joining_session, publish_handler);
        for (std::size_t attempt = 0;
             attempt < 10000 && publish_handler->GetStatus() != quicr::PublishTrackHandler::Status::kOk;
             ++attempt) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }

        REQUIRE(publish_handler->GetStatus() == quicr::PublishTrackHandler::Status::kOk);
        joining_client_manager.RemoveHandler(joining_session, publish_handler);

        const auto publish_namespace_handler = AwaitablePublishNamespaceHandler::Create(joining_track.name_space);
        auto publish_namespace_ready = publish_namespace_handler->Ready();
        joining_session->PublishNamespace(publish_namespace_handler);
        REQUIRE(publish_namespace_ready.wait_for(std::chrono::seconds(10)) == std::future_status::ready);
        joining_session->PublishNamespaceDone(publish_namespace_handler);

        joining_session->UnsubscribeNamespace(notification_handler);
        joining_client_manager.RemoveHandler(joining_session, joining_handler);
        CHECK(quicr_test_c_joining_server_wait(joining_server, 10000U));
    }
}
