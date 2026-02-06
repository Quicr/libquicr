#include "quicr/config.h"
#include "quicr/defer.h"
#include "quicr/fetch_track_handler.h"
#include "quicr/subscribe_track_handler.h"
#include "test_client.h"
#include "test_server.h"

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include <atomic>
#include <cstdlib>
#include <filesystem>
#include <future>
#include <iostream>
#include <mutex>
#include <spdlog/spdlog.h>
#include <string>
#include <thread>
#include <vector>

using namespace quicr;
using namespace quicr_test;

const std::string kIp = "127.0.0.1";
constexpr uint16_t kPort = 12345;
const std::string kServerId = "test-server";

/// @brief Get test timeout from environment or use default
/// @details Set LIBQUICR_TEST_TIMEOUT_MS environment variable to override (useful for CI)
static std::chrono::milliseconds
GetTestTimeout()
{
    const char* env_timeout = std::getenv("LIBQUICR_TEST_TIMEOUT_MS");
    if (env_timeout != nullptr) {
        try {
            return std::chrono::milliseconds(std::stoi(env_timeout));
        } catch (...) {
            // Fall through to default
        }
    }
    return std::chrono::milliseconds(300);
}

static const std::chrono::milliseconds kDefaultTimeout = GetTestTimeout();

/// @brief Wait for a condition to become true with polling
/// @param predicate Function returning true when condition is met
/// @param timeout Maximum time to wait
/// @param poll_interval How often to check the condition
/// @return true if condition was met, false if timeout
template<typename Predicate>
bool
WaitFor(Predicate predicate,
        std::chrono::milliseconds timeout = kDefaultTimeout,
        std::chrono::milliseconds poll_interval = std::chrono::milliseconds(10))
{
    const auto start = std::chrono::steady_clock::now();
    while (std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start) < timeout) {
        if (predicate()) {
            return true;
        }
        std::this_thread::sleep_for(poll_interval);
    }
    return predicate(); // Final check
}

static std::shared_ptr<TestServer>
MakeTestServer(const std::optional<std::string>& qlog_path = std::nullopt,
               std::optional<std::size_t> max_connections = std::nullopt)
{
    // Run the server.
    ServerConfig server_config;
    server_config.server_bind_ip = kIp;
    server_config.server_port = kPort;
    server_config.endpoint_id = kServerId;
    server_config.transport_config.debug = true;
    server_config.transport_config.tls_cert_filename = "server-cert.pem";
    server_config.transport_config.tls_key_filename = "server-key.pem";
    server_config.transport_config.time_queue_max_duration = 10000; // Support TTLs up to 10 seconds
    if (qlog_path.has_value()) {
        server_config.transport_config.quic_qlog_path = *qlog_path;
    }
    if (max_connections.has_value()) {
        server_config.transport_config.max_connections = *max_connections;
    }
    auto server = std::make_shared<TestServer>(server_config);
    const auto starting = server->Start();
    CHECK_EQ(starting, Transport::Status::kReady);

    // Wait for server to be ready instead of fixed sleep
    const bool ready = WaitFor([&server]() { return server->GetStatus() == Transport::Status::kReady; });
    CHECK(ready);

    return server;
}

std::shared_ptr<TestClient>
MakeTestClient(const bool connect = true,
               const std::optional<std::string>& qlog_path = std::nullopt,
               const std::string& protocol_scheme = "moq")
{
    // Connect a client.
    ClientConfig client_config;
    client_config.transport_config.debug = true;
    client_config.transport_config.time_queue_max_duration = 10000; // Support TTLs up to 10 seconds
    client_config.connect_uri = protocol_scheme + "://" + kIp + ":" + std::to_string(kPort) + "/relay";
    if (qlog_path.has_value()) {
        client_config.transport_config.quic_qlog_path = *qlog_path;
    }
    auto client = std::make_shared<TestClient>(client_config);
    if (connect) {
        client->Connect();
        // Wait for client to be connected instead of fixed sleep
        const bool connected = WaitFor([&client]() {
            const auto status = client->GetStatus();
            return status == Transport::Status::kReady || status == Transport::Status::kNotConnected;
        });
        CHECK(connected);
    }
    return client;
}

/// @brief Test subscribe handler that tracks received objects and exposes stream state
class TestSubscribeHandler : public SubscribeTrackHandler
{
  public:
    /// @brief Information about a received object
    struct ReceivedObject
    {
        uint64_t group_id;
        uint64_t subgroup_id;
        uint64_t object_id;
        ObjectStatus status;
        std::vector<uint8_t> data;
    };

    static std::shared_ptr<TestSubscribeHandler> Create(const FullTrackName& full_track_name,
                                                        messages::SubscriberPriority priority,
                                                        messages::GroupOrder group_order,
                                                        messages::FilterType filter_type)
    {
        return std::shared_ptr<TestSubscribeHandler>(
          new TestSubscribeHandler(full_track_name, priority, group_order, filter_type));
    }

    /// @brief Get all received objects
    std::vector<ReceivedObject> GetReceivedObjects() const
    {
        std::lock_guard lock(mutex_);
        return received_objects_;
    }

    /// @brief Get number of received objects
    std::size_t GetReceivedCount() const
    {
        std::lock_guard lock(mutex_);
        return received_objects_.size();
    }

    /// @brief Get number of active streams (exposes protected streams_ member)
    std::size_t GetActiveStreamCount() const
    {
        std::lock_guard lock(mutex_);
        return streams_.size();
    }

    /// @brief Set a promise to be fulfilled when a specific object count is reached
    void SetObjectCountPromise(std::size_t target_count, std::promise<void> promise)
    {
        std::lock_guard lock(mutex_);
        target_object_count_ = target_count;
        object_count_promise_ = std::move(promise);
    }

  protected:
    TestSubscribeHandler(const FullTrackName& full_track_name,
                         messages::SubscriberPriority priority,
                         messages::GroupOrder group_order,
                         messages::FilterType filter_type)
      : SubscribeTrackHandler(full_track_name, priority, group_order, filter_type)
    {
    }

    void ObjectReceived(const ObjectHeaders& object_headers, BytesSpan data) override
    {
        std::lock_guard lock(mutex_);
        if (!data.empty()) {
            received_objects_.push_back({ .group_id = object_headers.group_id,
                                          .subgroup_id = object_headers.subgroup_id,
                                          .object_id = object_headers.object_id,
                                          .status = object_headers.status,
                                          .data = std::vector<uint8_t>(data.begin(), data.end()) });

            // Check if we've reached the target count
            if (object_count_promise_.has_value() && received_objects_.size() >= target_object_count_) {
                object_count_promise_->set_value();
                object_count_promise_.reset();
            }
        }
    }

  private:
    mutable std::mutex mutex_;
    std::vector<ReceivedObject> received_objects_;
    std::size_t target_object_count_{ 0 };
    std::optional<std::promise<void>> object_count_promise_;
};

TEST_CASE("Integration - Connection")
{
    auto server = MakeTestServer();

    auto test_connection = [&](const std::string& protocol_scheme) {
        auto client = MakeTestClient(false, std::nullopt, protocol_scheme);
        std::promise<ServerSetupAttributes> recv_attributes;
        auto future = recv_attributes.get_future();
        client->SetConnectedPromise(std::move(recv_attributes));
        client->Connect();
        auto status = future.wait_for(kDefaultTimeout);
        REQUIRE(status == std::future_status::ready);
        const auto& [moqt_version, server_id] = future.get();
        CHECK_EQ(server_id, kServerId);
    };

    SUBCASE("Raw QUIC")
    {
        CAPTURE("Raw QUIC");
        test_connection("moq");
    }

    SUBCASE("WebTransport")
    {
        CAPTURE("WebTransport");
        test_connection("https");
    }
}

TEST_CASE("Integration - Subscribe")
{
    auto server = MakeTestServer();

    auto test_subscribe = [&](const std::string& protocol_scheme) {
        auto client = MakeTestClient(true, std::nullopt, protocol_scheme);

        // Make a subscription.
        FullTrackName ftn;
        ftn.name_space = TrackNamespace({ "namespace" });
        ftn.name = { 1, 2, 3 };
        constexpr auto filter_type = messages::FilterType::kLargestObject;
        const auto handler =
          SubscribeTrackHandler::Create(ftn, 0, messages::GroupOrder::kOriginalPublisherOrder, filter_type);

        // When we subscribe, server should receive a subscribe.
        std::promise<TestServer::SubscribeDetails> promise;
        std::future<TestServer::SubscribeDetails> future = promise.get_future();
        server->SetSubscribePromise(std::move(promise));

        // Subscribe.
        CHECK_NOTHROW(client->SubscribeTrack(handler));

        // Server should receive the subscribe.
        auto status = future.wait_for(kDefaultTimeout);
        REQUIRE(status == std::future_status::ready);
        const auto& details = future.get();
        CHECK_EQ(details.track_full_name.name, ftn.name);
        CHECK_EQ(details.track_full_name.name_space, ftn.name_space);
        CHECK_EQ(details.subscribe_attributes.filter_type, filter_type);

        // Server should respond, track should go live.
        const bool track_live =
          WaitFor([&handler]() { return handler->GetStatus() == SubscribeTrackHandler::Status::kOk; });
        CHECK(track_live);
        CHECK_EQ(handler->GetStatus(), SubscribeTrackHandler::Status::kOk);

        // Test is complete, unsubscribe while we are connected.
        CHECK_NOTHROW(client->UnsubscribeTrack(handler));

        // Check track handler cleanup / strong reference cycles.
        CHECK_EQ(handler.use_count(), 1);
    };

    SUBCASE("Raw QUIC")
    {
        CAPTURE("Raw QUIC");
        test_subscribe("moq");
    }

    SUBCASE("WebTransport")
    {
        CAPTURE("WebTransport");
        test_subscribe("https");
    }
}

TEST_CASE("Integration - Fetch")
{
    auto server = MakeTestServer();

    auto test_fetch = [&](const std::string& protocol_scheme) {
        auto client = MakeTestClient(true, std::nullopt, protocol_scheme);
        FullTrackName ftn;
        ftn.name_space = TrackNamespace({ "namespace" });
        ftn.name = { 1, 2, 3 };
        const auto handler = FetchTrackHandler::Create(
          ftn, 0, messages::GroupOrder::kOriginalPublisherOrder, { 0, 0 }, { 0, std::nullopt });
        client->FetchTrack(handler);
    };

    SUBCASE("Raw QUIC")
    {
        CAPTURE("Raw QUIC");
        test_fetch("moq");
    }

    SUBCASE("WebTransport")
    {
        CAPTURE("WebTransport");
        test_fetch("https");
    }
}

TEST_CASE("Integration - Handlers with no transport")
{
    // Subscribe.
    {
        const auto handler = SubscribeTrackHandler::Create(
          FullTrackName(), 0, messages::GroupOrder::kOriginalPublisherOrder, messages::FilterType::kLargestObject);
        handler->Pause();
        handler->Resume();
        handler->RequestNewGroup();
    }

    // Publish.
    {
        const auto handler = PublishTrackHandler::Create(FullTrackName(), TrackMode::kStream, 0, 0);
        ObjectHeaders headers = { .group_id = 0,
                                  .object_id = 0,
                                  .payload_length = 1,
                                  .status = ObjectStatus::kAvailable,
                                  .priority = 0,
                                  .ttl = 100,
                                  .track_mode = TrackMode::kStream,
                                  .extensions = std::nullopt,
                                  .immutable_extensions = std::nullopt };
        const auto status = handler->PublishObject(headers, std::vector<uint8_t>(1));
        CHECK_EQ(status, PublishTrackHandler::PublishObjectStatus::kInternalError);
    }

    // Fetch.
    {
        const auto handler = FetchTrackHandler::Create(
          FullTrackName(), 0, messages::GroupOrder::kOriginalPublisherOrder, { 0, 0 }, { 0, std::nullopt });
        handler->Pause();
        handler->Resume();
        handler->RequestNewGroup();
    }
}

TEST_CASE("Group ID Gap")
{
    auto server = MakeTestServer();

    auto test_group_id_gap = [&](const std::string& protocol_scheme) {
        auto client = MakeTestClient(true, std::nullopt, protocol_scheme);
        FullTrackName ftn;
        ftn.name_space = TrackNamespace({ "namespace" });
        ftn.name = { 1, 2, 3 };

        // Pub.
        const auto pub = PublishTrackHandler::Create(ftn, TrackMode::kStream, 0, 500);
        client->PublishTrack(pub);

        // Wait for publisher to be ready
        const bool pub_ready = WaitFor([&pub]() { return pub->CanPublish(); });
        CHECK(pub_ready);

        constexpr messages::GroupId expected_gap = 1758273157;

        // TODO: Re-enable when data roundtrip support.
        // Sub.
        // int received = 0;
        // TestSubscribeHandler::ObjectReceivedCallback callback;
        // const auto sub = std::make_shared<TestSubscribeHandler>(ftn, [&received, expected_gap](ObjectHeaders headers,
        // BytesSpan) {
        //     received += 1;
        //     constexpr auto gap_key = static_cast<std::uint64_t>(messages::ExtensionHeaderType::kPriorGroupIdGap);
        //     switch (received) {
        //         case 1: {
        //             // No Gap.
        //             const bool no_gap = !headers.extensions.has_value() || !headers.extensions->contains(gap_key);
        //             CHECK(no_gap);
        //             break;
        //         }
        //         case 2: {
        //             // Gap, group gap should be set.
        //             REQUIRE(headers.extensions.has_value());
        //             REQUIRE(headers.extensions->contains(gap_key));
        //             const auto bytes = headers.extensions->at(gap_key);
        //             std::uint64_t gap;
        //             memcpy(&gap, bytes.data(), bytes.size());
        //             CHECK_EQ(gap, expected_gap);
        //             break;
        //         }
        //         default:
        //             FAIL("Unexpected object received");
        //             break;
        //     }
        // });
        // client->SubscribeTrack(sub);
        // std::this_thread::sleep_for(std::chrono::milliseconds(kDefaultTimeout));

        REQUIRE(pub->CanPublish());
        const auto payload = std::vector<std::uint8_t>(1);
        ObjectHeaders headers{ .group_id = 0, .object_id = 0, .payload_length = payload.size() };
        REQUIRE_EQ(pub->PublishObject(headers, payload), PublishTrackHandler::PublishObjectStatus::kOk);
        headers.group_id = expected_gap + 1;
        REQUIRE_EQ(pub->PublishObject(headers, payload), PublishTrackHandler::PublishObjectStatus::kOk);
        // CHECK_EQ(received, 2);
    };

    SUBCASE("Raw QUIC")
    {
        CAPTURE("Raw QUIC");
        test_group_id_gap("moq");
    }

    SUBCASE("WebTransport")
    {
        CAPTURE("WebTransport");
        test_group_id_gap("https");
    }
}

TEST_CASE("Qlog Generation")
{
    auto test_qlog = [](const std::string& protocol_scheme) {
        // Create temporary destination for QLOG files.
        const auto temp_dir = std::filesystem::temp_directory_path() / "libquicr_qlog_test";
        std::filesystem::create_directories(temp_dir);
        defer(std::filesystem::remove_all(temp_dir));

        // Enable qlog.
        auto server = MakeTestServer(temp_dir.string());
        auto client = MakeTestClient(true, temp_dir.string(), protocol_scheme);

        // Check that above directory now has the two (server + client) qlog files.
        int qlogs = 0;
        for (const auto& entry : std::filesystem::directory_iterator(temp_dir)) {
            if (entry.path().extension() == ".log") {
                ++qlogs;
            } else {
                FAIL("Unexpected file in qlog directory: {}", entry.path().string());
            }
        }
        CHECK_EQ(qlogs, 2);
    };

    SUBCASE("Raw QUIC")
    {
        CAPTURE("Raw QUIC");
        test_qlog("moq");
    }

    SUBCASE("WebTransport")
    {
        CAPTURE("WebTransport");
        test_qlog("https");
    }
}

TEST_CASE("Integration - Raw Subscribe Namespace")
{
    auto server = MakeTestServer();

    auto test_subscribe_namespace = [&](const std::string& protocol_scheme) {
        auto client = MakeTestClient(true, std::nullopt, protocol_scheme);

        // Set up the prefix namespace we want to subscribe to
        TrackNamespace prefix_namespace(std::vector<std::string>{ "foo", "bar" });

        // Set up promise to capture server-side callback
        std::promise<TestServer::SubscribeNamespaceDetails> server_promise;
        std::future<TestServer::SubscribeNamespaceDetails> server_future = server_promise.get_future();
        server->SetSubscribeNamespacePromise(std::move(server_promise));

        // Set up promise to verify client does NOT receive PUBLISH_NAMESPACE
        std::promise<TrackNamespace> publish_namespace_promise;
        std::future<TrackNamespace> publish_namespace_future = publish_namespace_promise.get_future();
        client->SetPublishNamespaceReceivedPromise(std::move(publish_namespace_promise));

        // Set up promise to verify client does NOT receive PUBLISH
        std::promise<FullTrackName> publish_promise;
        auto publish_future = publish_promise.get_future();
        client->SetPublishReceivedPromise(std::move(publish_promise));

        // Client sends SUBSCRIBE_NAMESPACE
        auto handler = SubscribeNamespaceHandler::Create(prefix_namespace);
        CHECK_NOTHROW(client->SubscribeNamespace(handler));

        // Server should receive the SUBSCRIBE_NAMESPACE message
        auto server_status = server_future.wait_for(kDefaultTimeout);
        REQUIRE(server_status == std::future_status::ready);
        const auto& details = server_future.get();
        CHECK_EQ(details.prefix_namespace, prefix_namespace);

        // Client should receive SUBSCRIBE_NAMESPACE_OK from relay
        std::this_thread::sleep_for(std::chrono::milliseconds(kDefaultTimeout));
        CHECK_EQ(handler->GetStatus(), SubscribeNamespaceHandler::Status::kOk);

        // Client should NOT receive PUBLISH_NAMESPACE because there are no matching namespaces.
        auto publish_namespace_status = publish_namespace_future.wait_for(kDefaultTimeout);
        CHECK(publish_namespace_status == std::future_status::timeout);

        // Client should NOT receive PUBLISH because there are no matching tracks.
        auto publish_status = publish_future.wait_for(kDefaultTimeout);
        CHECK(publish_status == std::future_status::timeout);
    };

    SUBCASE("Raw QUIC")
    {
        CAPTURE("Raw QUIC");
        test_subscribe_namespace("moq");
    }

    SUBCASE("WebTransport")
    {
        CAPTURE("WebTransport");
        test_subscribe_namespace("https");
    }
}

TEST_CASE("Integration - Subscribe Namespace with matching namespace")
{
    auto server = MakeTestServer();

    auto test_matching_namespace = [&](const std::string& protocol_scheme) {
        auto client = MakeTestClient(true, std::nullopt, protocol_scheme);

        // Target namespace.
        TrackNamespace prefix_namespace(std::vector<std::string>{ "foo", "bar" });

        // Set up promise to verify client received matching PUBLISH_NAMESPACE.
        std::promise<TrackNamespace> publish_namespace_promise;
        std::future<TrackNamespace> publish_namespace_future = publish_namespace_promise.get_future();
        server->AddKnownPublishedNamespace(prefix_namespace);
        client->SetPublishNamespaceReceivedPromise(std::move(publish_namespace_promise));

        // SUBSCRIBE_NAMESPACE to prefix.
        CHECK_NOTHROW(client->SubscribeNamespace(SubscribeNamespaceHandler::Create(prefix_namespace)));

        // Client should receive matched PUBLISH_NAMESPACE.
        auto publish_namespace_status = publish_namespace_future.wait_for(kDefaultTimeout);
        REQUIRE(publish_namespace_status == std::future_status::ready);
        const auto& received_namespace = publish_namespace_future.get();
        CHECK_EQ(received_namespace, prefix_namespace);
    };

    SUBCASE("Raw QUIC")
    {
        CAPTURE("Raw QUIC");
        test_matching_namespace("moq");
    }

    SUBCASE("WebTransport")
    {
        CAPTURE("WebTransport");
        test_matching_namespace("https");
    }
}

TEST_CASE("Integration - Subscribe Namespace with matching track")
{
    auto server = MakeTestServer();

    auto test_matching_track = [&](const std::string& protocol_scheme) {
        auto client = MakeTestClient(true, std::nullopt, protocol_scheme);

        // Track.
        TrackNamespace prefix_namespace(std::vector<std::string>{ "foo", "bar" });

        // Existing track.
        const FullTrackName existing_track{ prefix_namespace, { 0x01 } };
        const auto existing_track_hash = TrackHash(existing_track);

        // Set up promise to verify client received matching PUBLISH_NAMESPACE.
        std::promise<FullTrackName> publish_promise;
        auto publish_future = publish_promise.get_future();
        messages::PublishAttributes publish_attributes;
        publish_attributes.group_order = quicr::messages::GroupOrder::kOriginalPublisherOrder;
        publish_attributes.track_alias = existing_track_hash.track_fullname_hash;

        // TODO: Validate full attribute round-trip.
        server->AddKnownPublishedTrack(existing_track, std::nullopt, publish_attributes);
        client->SetPublishReceivedPromise(std::move(publish_promise));

        // Set up promise to verify server gets accepted publish.
        std::promise<TestServer::SubscribeDetails> publish_ok_promise;
        auto publish_ok_future = publish_ok_promise.get_future();
        server->SetPublishAcceptedPromise(std::move(publish_ok_promise));

        // SUBSCRIBE_NAMESPACE to prefix.
        CHECK_NOTHROW(client->SubscribeNamespace(SubscribeNamespaceHandler::Create(prefix_namespace)));

        // Client should receive matched PUBLISH for existing track.
        auto publish_status = publish_future.wait_for(kDefaultTimeout);
        REQUIRE(publish_status == std::future_status::ready);
        const auto& received_name = publish_future.get();
        CHECK_EQ(received_name.name_space, existing_track.name_space);
        CHECK_EQ(received_name.name, existing_track.name);

        // Client accepts, server should receive PUBLISH_OK (wired to SubscribeReceived).
        auto publish_ok_status = publish_ok_future.wait_for(kDefaultTimeout);
        REQUIRE(publish_ok_status == std::future_status::ready);
        const auto& received_publish_ok = publish_ok_future.get();
        CHECK_EQ(received_publish_ok.track_full_name.name_space, existing_track.name_space);
        CHECK_EQ(received_publish_ok.track_full_name.name, existing_track.name);

        // TODO: Test the Error / reject path.
    };

    SUBCASE("Raw QUIC")
    {
        CAPTURE("Raw QUIC");
        test_matching_track("moq");
    }

    SUBCASE("WebTransport")
    {
        CAPTURE("WebTransport");
        test_matching_track("https");
    }
}

TEST_CASE("Integration - Subscribe Namespace with ongoing match")
{
    auto server = MakeTestServer(std::nullopt, 2);

    auto test_ongoing_match = [&](const std::string& protocol_scheme) {
        auto client = MakeTestClient(true, std::nullopt, protocol_scheme);
        auto publisher = MakeTestClient(true, std::nullopt, protocol_scheme);

        // Track.
        TrackNamespace prefix_namespace(std::vector<std::string>{ "foo", "bar" });

        // Existing track.
        FullTrackName existing_track;
        existing_track.name_space = prefix_namespace;
        existing_track.name = { 0x01 };

        // Set up promise to verify client received matching PUBLISH_NAMESPACE.
        std::promise<FullTrackName> publish_promise;
        auto publish_future = publish_promise.get_future();
        client->SetPublishReceivedPromise(std::move(publish_promise));

        // Set up promise to verify server gets accepted publish.
        std::promise<TestServer::SubscribeDetails> publish_ok_promise;
        auto publish_ok_future = publish_ok_promise.get_future();
        server->SetPublishAcceptedPromise(std::move(publish_ok_promise));

        // SUBSCRIBE_NAMESPACE to prefix.
        CHECK_NOTHROW(client->SubscribeNamespace(SubscribeNamespaceHandler::Create(prefix_namespace)));

        // In the future, a PUBLISH arrives.
        std::this_thread::sleep_for(kDefaultTimeout);
        const auto publish = PublishTrackHandler::Create(existing_track, TrackMode::kStream, 10, 5000);
        publisher->PublishTrack(publish);

        // Client should receive matched PUBLISH for existing track.
        auto publish_status = publish_future.wait_for(kDefaultTimeout);
        REQUIRE(publish_status == std::future_status::ready);
        const auto& received_name = publish_future.get();
        CHECK_EQ(received_name.name_space, existing_track.name_space);
        CHECK_EQ(received_name.name, existing_track.name);

        // Client accepts, server should receive PUBLISH_OK (wired to SubscribeReceived).
        auto publish_ok_status = publish_ok_future.wait_for(kDefaultTimeout);
        REQUIRE(publish_ok_status == std::future_status::ready);
        const auto& received_publish_ok = publish_ok_future.get();
        CHECK_EQ(received_publish_ok.track_full_name.name_space, existing_track.name_space);
        CHECK_EQ(received_publish_ok.track_full_name.name, existing_track.name);
    };

    SUBCASE("Raw QUIC")
    {
        CAPTURE("Raw QUIC");
        test_ongoing_match("moq");
    }

    SUBCASE("WebTransport")
    {
        CAPTURE("WebTransport");
        test_ongoing_match("https");
    }
}

TEST_CASE("Integration - Subscribe Namespace with non-matching namespace")
{
    auto server = MakeTestServer();

    auto test_non_matching = [&](const std::string& protocol_scheme) {
        auto client = MakeTestClient(true, std::nullopt, protocol_scheme);

        // Target namespace.
        TrackNamespace prefix_namespace(std::vector<std::string>{ "foo", "bar" });
        TrackNamespace non_match({ "baz" });

        // Set up promise to verify client received matching PUBLISH_NAMESPACE.
        std::promise<TrackNamespace> publish_namespace_promise;
        std::future<TrackNamespace> publish_namespace_future = publish_namespace_promise.get_future();
        server->AddKnownPublishedNamespace(non_match);
        client->SetPublishNamespaceReceivedPromise(std::move(publish_namespace_promise));

        // SUBSCRIBE_NAMESPACE to prefix.
        CHECK_NOTHROW(client->SubscribeNamespace(SubscribeNamespaceHandler::Create(prefix_namespace)));

        // Client should NOT receive PUBLISH_NAMESPACE.
        auto publish_namespace_status = publish_namespace_future.wait_for(kDefaultTimeout);
        REQUIRE(publish_namespace_status == std::future_status::timeout);
    };

    SUBCASE("Raw QUIC")
    {
        CAPTURE("Raw QUIC");
        test_non_matching("moq");
    }

    SUBCASE("WebTransport")
    {
        CAPTURE("WebTransport");
        test_non_matching("https");
    }
}

TEST_CASE("Integration - Annouce Flow")
{
    auto server = MakeTestServer();

    auto test_announce = [&](const std::string& protocol_scheme) {
        auto client = MakeTestClient(true, std::nullopt, protocol_scheme);

        // Create a track with announce enabled.
        const TrackNamespace prefix(std::vector<std::string>{ "test", "namespace" });
        auto ns_handler = PublishNamespaceHandler::Create(prefix);

        // Set up promise to capture server receiving PUBLISH_NAMESPACE.
        std::promise<TestServer::PublishNamespaceDetails> server_promise;
        std::future<TestServer::PublishNamespaceDetails> server_future = server_promise.get_future();
        server->SetPublishNamespacePromise(std::move(server_promise));

        // Publish with announce, PUBLISH_NAMESPACE sent.
        CHECK_NOTHROW(client->PublishNamespace(ns_handler));

        // Server should receive the PUBLISH_NAMESPACE for the namespace.
        auto server_status = server_future.wait_for(kDefaultTimeout);
        REQUIRE_EQ(server_status, std::future_status::ready);

        // Verify the publish track handler transitions to kNoSubscribers (PUBLISH_NAMESPACE_OK).
        std::this_thread::sleep_for(kDefaultTimeout);
        CHECK_EQ(ns_handler->GetStatus(), PublishNamespaceHandler::Status::kOk);

        const std::string name = "test";
        const FullTrackName ftn(prefix, std::vector<uint8_t>{ name.begin(), name.end() });

        std::weak_ptr<PublishTrackHandler> w_pub_handler;
        REQUIRE_NOTHROW(w_pub_handler = ns_handler->PublishTrack(ftn, TrackMode::kStream, 1, 5000));

        auto pub_handler = w_pub_handler.lock();
        REQUIRE_NE(pub_handler, nullptr);

        CHECK_EQ(pub_handler->GetStatus(), PublishTrackHandler::Status::kPendingPublishOk);

        std::this_thread::sleep_for(kDefaultTimeout);
        CHECK_EQ(pub_handler->GetStatus(), PublishTrackHandler::Status::kOk);
    };

    SUBCASE("Raw QUIC")
    {
        CAPTURE("Raw QUIC");
        test_announce("moq");
    }

    SUBCASE("WebTransport")
    {
        CAPTURE("WebTransport");
        test_announce("https");
    }
}

class TestFetchTrackHandler final : public FetchTrackHandler
{
  public:
    struct ReceivedObject
    {
        const ObjectHeaders headers;
        const std::vector<uint8_t> payload;
    };

    TestFetchTrackHandler(const FullTrackName& full_track_name,
                          const messages::SubscriberPriority priority,
                          const messages::GroupOrder group_order,
                          const messages::Location& start_location,
                          const messages::FetchEndLocation& end_location)
      : FetchTrackHandler(full_track_name, priority, group_order, start_location, end_location)
    {
    }

    static std::shared_ptr<TestFetchTrackHandler> Create(const FullTrackName& full_track_name,
                                                         const messages::SubscriberPriority priority,
                                                         const messages::GroupOrder group_order,
                                                         const messages::Location& start_location,
                                                         const messages::FetchEndLocation& end_location)
    {
        return std::make_shared<TestFetchTrackHandler>(
          full_track_name, priority, group_order, start_location, end_location);
    }

    void ObjectReceived(const ObjectHeaders& headers, BytesSpan data) override
    {
        std::lock_guard lock(mutex_);
        received_objects_.push_back(
          ReceivedObject{ .headers = headers, .payload = std::vector<uint8_t>(data.begin(), data.end()) });
    }

    std::vector<ReceivedObject> GetReceivedObjects()
    {
        std::lock_guard lock(mutex_);
        return received_objects_;
    }

    std::size_t GetReceivedCount()
    {
        std::lock_guard lock(mutex_);
        return received_objects_.size();
    }

  private:
    std::mutex mutex_;
    std::vector<ReceivedObject> received_objects_;
};

TEST_CASE("Integration - Fetch object roundtrip")
{
    const auto server = MakeTestServer();
    auto test_fetch_roundtrip = [&](const std::string& protocol_scheme) {
        auto client = MakeTestClient(true, std::nullopt, protocol_scheme);

        FullTrackName ftn;
        ftn.name_space = TrackNamespace(std::vector<std::string>{ "test", "namespace" });
        ftn.name = { 1, 2, 3 };

        // Set up test data with specific values for all fields
        std::vector<TestServer::FetchResponseData> cached;
        constexpr messages::GroupId fetch_group = 100;
        constexpr messages::ObjectId max_object = 100;
        for (messages::ObjectId object = 0; object <= max_object; object++) {
            TestServer::FetchResponseData response_data{};
            response_data.headers.group_id = fetch_group;
            response_data.headers.subgroup_id = 0;
            response_data.headers.object_id = object;
            response_data.headers.status = ObjectStatus::kAvailable;
            response_data.headers.priority = 5;
            response_data.payload = { static_cast<uint8_t>(object) };
            response_data.headers.payload_length = response_data.payload.size();
            cached.push_back(response_data);
        }

        server->SetFetchResponseData(cached);

        auto fetch_handler = TestFetchTrackHandler::Create(
          ftn, 0, messages::GroupOrder::kOriginalPublisherOrder, { fetch_group, 0 }, { fetch_group, std::nullopt });

        client->FetchTrack(fetch_handler);

        // Wait for all objects to be received
        const auto expected_count = cached.size();
        const bool all_received =
          WaitFor([&fetch_handler, expected_count]() { return fetch_handler->GetReceivedCount() >= expected_count; },
                  std::chrono::milliseconds(3000));
        REQUIRE_EQ(fetch_handler->GetReceivedCount(), expected_count);
        REQUIRE(all_received);

        // Verify each object's payload matches its object_id
        const auto received_objects = fetch_handler->GetReceivedObjects();
        CHECK_EQ(received_objects.size(), expected_count);
        for (const auto& received : received_objects) {
            CHECK_EQ(received.headers.group_id, fetch_group);
            CHECK_EQ(received.headers.subgroup_id, 0);
            const std::vector expected_payload = { static_cast<uint8_t>(received.headers.object_id) };
            CHECK_EQ(received.payload, expected_payload);
        }
    };

    SUBCASE("Raw QUIC")
    {
        CAPTURE("Raw QUIC");
        test_fetch_roundtrip("moq");
    }

    SUBCASE("WebTransport")
    {
        CAPTURE("WebTransport");
        test_fetch_roundtrip("https");
    }
}

TEST_CASE("Integration - Subgroup and Stream Testing")
{
    // Server needs to support 2 connections (subscriber + publisher)
    auto server = MakeTestServer(std::nullopt, 2);

    auto test_subgroups = [&](const std::string& protocol_scheme) {
        // Create subscriber and publisher clients
        auto subscriber_client = MakeTestClient(true, std::nullopt, protocol_scheme);
        auto publisher_client = MakeTestClient(true, std::nullopt, protocol_scheme);

        // Track configuration
        FullTrackName ftn;
        ftn.name_space = TrackNamespace(std::vector<std::string>{ "test", "subgroups" });
        ftn.name = { 0x01, 0x02, 0x03 };

        // Constants for test
        constexpr std::size_t num_groups = 2;
        constexpr std::size_t num_subgroups = 3;
        constexpr std::size_t messages_per_phase = 10;

        // Message totals per subgroup (all subgroups run simultaneously each phase):
        // - Subgroup 0: 10 messages (runs in phase 1 only, then closes)
        // - Subgroup 1: 20 messages (runs in phases 1 and 2, then closes)
        // - Subgroup 2: 30 messages (runs in phases 1, 2, and 3, then closes with end_of_group)
        // Per group total: 10 + 20 + 30 = 60
        // Total for 2 groups: 120
        constexpr std::size_t subgroup_0_messages = messages_per_phase;     // 10
        constexpr std::size_t subgroup_1_messages = messages_per_phase * 2; // 20
        constexpr std::size_t subgroup_2_messages = messages_per_phase * 3; // 30
        constexpr std::size_t messages_per_group =
          subgroup_0_messages + subgroup_1_messages + subgroup_2_messages;      // 60
        constexpr std::size_t total_messages = num_groups * messages_per_group; // 120

        // Create subscribe handler that tracks received objects
        auto sub_handler = TestSubscribeHandler::Create(
          ftn, 3, messages::GroupOrder::kOriginalPublisherOrder, messages::FilterType::kLargestObject);

        // Set up promise for subscriber receiving all messages
        std::promise<void> all_received_promise;
        auto all_received_future = all_received_promise.get_future();
        sub_handler->SetObjectCountPromise(total_messages, std::move(all_received_promise));

        // Subscribe to the track
        subscriber_client->SubscribeTrack(sub_handler);

        // Wait for subscription to be ready
        const bool sub_ready =
          WaitFor([&sub_handler]() { return sub_handler->GetStatus() == SubscribeTrackHandler::Status::kOk; });
        REQUIRE(sub_ready);

        // Create publisher with stream mode (explicit subgroup ID)
        auto pub_handler = PublishTrackHandler::Create(ftn, TrackMode::kStream, 3, 1000);
        publisher_client->PublishTrack(pub_handler);

        // Wait for publisher to be ready
        const bool pub_ready = WaitFor([&pub_handler]() { return pub_handler->CanPublish(); });
        REQUIRE(pub_ready);

        // Helper to publish an object
        auto publish_object = [&](uint64_t group_id, uint64_t subgroup_id, uint64_t object_id) {
            std::vector<uint8_t> payload = { static_cast<uint8_t>(group_id),
                                             static_cast<uint8_t>(subgroup_id),
                                             static_cast<uint8_t>(object_id) };
            payload.resize(100);

            ObjectHeaders headers = { .group_id = group_id,
                                      .object_id = object_id,
                                      .subgroup_id = subgroup_id,
                                      .payload_length = payload.size(),
                                      .status = ObjectStatus::kAvailable,
                                      .priority = 3,
                                      .ttl = 1000,
                                      .track_mode = TrackMode::kStream,
                                      .extensions = std::nullopt,
                                      .immutable_extensions = std::nullopt };

            auto status = pub_handler->PublishObject(headers, payload);
            REQUIRE_EQ(status, PublishTrackHandler::PublishObjectStatus::kOk);
        };

        // Track object IDs per group+subgroup
        std::map<std::pair<uint64_t, uint64_t>, uint64_t> next_object_id;
        for (uint64_t group = 0; group < num_groups; ++group) {
            for (uint64_t subgroup = 0; subgroup < num_subgroups; ++subgroup) {
                next_object_id[{ group, subgroup }] = 0;
            }
        }

        // Helper to get and increment object ID for a group+subgroup
        auto get_next_obj_id = [&](uint64_t group, uint64_t subgroup) -> uint64_t {
            return next_object_id[{ group, subgroup }]++;
        };

        // ================================================================================
        // Phase 1: Publish 10 messages to ALL subgroups (0, 1, 2) in both groups
        // Then close subgroup 0 with end_of_subgroup
        // After phase 1:
        //   - Subgroup 0: 10 messages (closed)
        //   - Subgroup 1: 10 messages (still open)
        //   - Subgroup 2: 10 messages (still open)
        // ================================================================================
        for (uint64_t msg = 0; msg < messages_per_phase; ++msg) {
            bool is_last_in_phase = (msg == messages_per_phase - 1);

            for (uint64_t group = 0; group < num_groups; ++group) {
                for (uint64_t subgroup = 0; subgroup < num_subgroups; ++subgroup) {
                    publish_object(group, subgroup, get_next_obj_id(group, subgroup));

                    if (is_last_in_phase && (subgroup == 0)) {
                        pub_handler->EndSubgroup(group, subgroup, true);
                    }
                }
            }
        }

        // Wait for all 6 streams to be created (2 groups × 3 subgroups)
        const bool streams_created = WaitFor([&sub_handler]() { return sub_handler->GetActiveStreamCount() >= 4; },
                                             std::chrono::milliseconds(1000));
        INFO("Active streams after publishing phase 1: ", sub_handler->GetActiveStreamCount());
        CHECK(streams_created);

        // Verify subgroup 0 is closed (4 streams remain)
        const bool subgroup0_closed = WaitFor([&sub_handler]() { return sub_handler->GetActiveStreamCount() <= 4; },
                                              std::chrono::milliseconds(1000));
        INFO("Active streams after phase 1 (subgroup 0 closed): ", sub_handler->GetActiveStreamCount());
        CHECK(subgroup0_closed);

        // ================================================================================
        // Phase 2: Publish 10 more messages to subgroups 1 and 2 in both groups
        // Then close subgroup 1 with end_of_subgroup
        // After phase 2:
        //   - Subgroup 0: 10 messages (already closed)
        //   - Subgroup 1: 20 messages (closed)
        //   - Subgroup 2: 20 messages (still open)
        // ================================================================================
        for (uint64_t msg = 0; msg < messages_per_phase; ++msg) {
            bool is_last_in_phase = (msg == messages_per_phase - 1);

            for (uint64_t group = 0; group < num_groups; ++group) {
                for (uint64_t subgroup = 1; subgroup < num_subgroups; ++subgroup) {
                    publish_object(group, subgroup, get_next_obj_id(group, subgroup));

                    if (is_last_in_phase && (subgroup == 1)) {
                        pub_handler->EndSubgroup(group, subgroup, true);
                    }
                }
            }
        }

        // Verify subgroup 1 is closed (2 streams remain - subgroup 2 in both groups)
        const bool subgroup1_closed = WaitFor([&sub_handler]() { return sub_handler->GetActiveStreamCount() <= 2; },
                                              std::chrono::milliseconds(1000));
        INFO("Active streams after phase 2 (subgroup 1 closed): ", sub_handler->GetActiveStreamCount());
        CHECK(subgroup1_closed);

        // ================================================================================
        // Phase 3: Publish 10 more messages to subgroup 2 in both groups
        // Then close subgroup 2 with end_of_subgroup AND end_of_group
        // After phase 3:
        //   - Subgroup 0: 10 messages (already closed)
        //   - Subgroup 1: 20 messages (already closed)
        //   - Subgroup 2: 30 messages (closed with end_of_group)
        // ================================================================================
        for (uint64_t msg = 0; msg < messages_per_phase; ++msg) {
            bool is_last_in_phase = (msg == messages_per_phase - 1);

            for (uint64_t group = 0; group < num_groups; ++group) {
                uint64_t subgroup = 2;

                publish_object(group, subgroup, get_next_obj_id(group, subgroup));

                if (is_last_in_phase) {
                    pub_handler->EndSubgroup(group, subgroup, true);
                }
            }
        }

        // Wait for all streams to be closed
        const bool all_streams_closed = WaitFor([&sub_handler]() { return sub_handler->GetActiveStreamCount() == 0; },
                                                std::chrono::milliseconds(1000));
        INFO("Active streams after phase 3 (all closed): ", sub_handler->GetActiveStreamCount());
        CHECK(all_streams_closed);

        // Wait for all messages to be received
        auto receive_status = all_received_future.wait_for(std::chrono::milliseconds(3000));
        REQUIRE(receive_status == std::future_status::ready);

        // Verify total received count
        const auto received_objects = sub_handler->GetReceivedObjects();
        INFO("Total messages received: ", received_objects.size(), ", expected: ", total_messages);
        CHECK_EQ(received_objects.size(), total_messages);

        // Verify we received messages from all groups and subgroups
        std::map<uint64_t, std::map<uint64_t, std::size_t>> counts_by_group_subgroup;
        for (const auto& obj : received_objects) {
            counts_by_group_subgroup[obj.group_id][obj.subgroup_id]++;
        }

        // Should have received from 2 groups
        CHECK_EQ(counts_by_group_subgroup.size(), num_groups);

        for (uint64_t group = 0; group < num_groups; ++group) {
            // Should have received from 3 subgroups per group
            CHECK_EQ(counts_by_group_subgroup[group].size(), num_subgroups);

            // Verify per-subgroup message counts
            // Subgroup 0: ran for 1 phase = 10 messages
            INFO(
              "Group ", group, " subgroup 0: ", counts_by_group_subgroup[group][0], " expected: ", subgroup_0_messages);
            CHECK_EQ(counts_by_group_subgroup[group][0], subgroup_0_messages);

            // Subgroup 1: ran for 2 phases = 20 messages
            INFO(
              "Group ", group, " subgroup 1: ", counts_by_group_subgroup[group][1], " expected: ", subgroup_1_messages);
            CHECK_EQ(counts_by_group_subgroup[group][1], subgroup_1_messages);

            // Subgroup 2: ran for 3 phases = 30 messages
            INFO(
              "Group ", group, " subgroup 2: ", counts_by_group_subgroup[group][2], " expected: ", subgroup_2_messages);
            CHECK_EQ(counts_by_group_subgroup[group][2], subgroup_2_messages);
        }

        INFO("Successfully verified ", total_messages, " messages:");
        INFO("  - Per group: ", messages_per_group, " messages");
        INFO("  - Subgroup 0: ", subgroup_0_messages, " messages (ran 1 phase)");
        INFO("  - Subgroup 1: ", subgroup_1_messages, " messages (ran 2 phases)");
        INFO("  - Subgroup 2: ", subgroup_2_messages, " messages (ran 3 phases)");
    };

    SUBCASE("Raw QUIC")
    {
        CAPTURE("Raw QUIC");
        test_subgroups("moq");
    }

    SUBCASE("WebTransport")
    {
        CAPTURE("WebTransport");
        test_subgroups("https");
    }
}
