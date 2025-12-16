#include "quicr/config.h"
#include "quicr/defer.h"
#include "test_client.h"
#include "test_server.h"

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include <filesystem>
#include <future>
#include <iostream>
#include <string>

using namespace quicr;
using namespace quicr_test;

const std::string kIp = "127.0.0.1";
constexpr uint16_t kPort = 12345;
const std::string kServerId = "test-server";
constexpr std::chrono::milliseconds kDefaultTimeout(50);

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
    if (qlog_path.has_value()) {
        server_config.transport_config.quic_qlog_path = *qlog_path;
    }
    if (max_connections.has_value()) {
        server_config.transport_config.max_connections = *max_connections;
    }
    auto server = std::make_shared<TestServer>(server_config);
    const auto starting = server->Start();
    CHECK_EQ(starting, Transport::Status::kReady);
    std::this_thread::sleep_for(std::chrono::milliseconds(kDefaultTimeout));
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
    client_config.connect_uri = protocol_scheme + "://" + kIp + ":" + std::to_string(kPort) + "/relay";
    if (qlog_path.has_value()) {
        client_config.transport_config.quic_qlog_path = *qlog_path;
    }
    auto client = std::make_shared<TestClient>(client_config);
    if (connect) {
        client->Connect();
        std::this_thread::sleep_for(kDefaultTimeout);
    }
    return client;
}

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
        std::this_thread::sleep_for(std::chrono::milliseconds(kDefaultTimeout));
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
        const auto handler =
          FetchTrackHandler::Create(ftn, 0, messages::GroupOrder::kOriginalPublisherOrder, 0, 0, 0, 0);
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
        const auto handler =
          FetchTrackHandler::Create(FullTrackName(), 0, messages::GroupOrder::kOriginalPublisherOrder, 0, 0, 0, 0);
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
        std::this_thread::sleep_for(std::chrono::milliseconds(kDefaultTimeout));

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

        // Set up promise to capture client-side SUBSCRIBE_NAMESPACE_OK
        std::promise<TrackNamespace> client_promise;
        std::future<TrackNamespace> client_future = client_promise.get_future();
        client->SetSubscribeNamespaceOkPromise(std::move(client_promise));

        // Set up promise to verify client does NOT receive PUBLISH_NAMESPACE
        std::promise<TrackNamespace> publish_namespace_promise;
        std::future<TrackNamespace> publish_namespace_future = publish_namespace_promise.get_future();
        client->SetPublishNamespaceReceivedPromise(std::move(publish_namespace_promise));

        // Set up promise to verify client does NOT receive PUBLISH
        std::promise<FullTrackName> publish_promise;
        auto publish_future = publish_promise.get_future();
        client->SetPublishReceivedPromise(std::move(publish_promise));

        // Client sends SUBSCRIBE_NAMESPACE
        CHECK_NOTHROW(client->SubscribeNamespace(prefix_namespace));

        // Server should receive the SUBSCRIBE_NAMESPACE message
        auto server_status = server_future.wait_for(kDefaultTimeout);
        REQUIRE(server_status == std::future_status::ready);
        const auto& details = server_future.get();
        CHECK_EQ(details.prefix_namespace, prefix_namespace);

        // Client should receive SUBSCRIBE_NAMESPACE_OK from relay
        auto client_status = client_future.wait_for(kDefaultTimeout);
        REQUIRE(client_status == std::future_status::ready);
        const auto& received_namespace = client_future.get();
        CHECK_EQ(received_namespace, prefix_namespace);

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
        CHECK_NOTHROW(client->SubscribeNamespace(prefix_namespace));

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

        // Set up promise to verify client received matching PUBLISH_NAMESPACE.
        std::promise<FullTrackName> publish_promise;
        auto publish_future = publish_promise.get_future();
        messages::PublishAttributes publish_attributes;
        // TODO: Validate full attribute round-trip.
        server->AddKnownPublishedTrack(existing_track, std::nullopt, publish_attributes);
        client->SetPublishReceivedPromise(std::move(publish_promise));

        // Set up promise to verify server gets accepted publish.
        std::promise<TestServer::SubscribeDetails> publish_ok_promise;
        auto publish_ok_future = publish_ok_promise.get_future();
        server->SetPublishAcceptedPromise(std::move(publish_ok_promise));

        // SUBSCRIBE_NAMESPACE to prefix.
        CHECK_NOTHROW(client->SubscribeNamespace(prefix_namespace));

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
        CHECK_NOTHROW(client->SubscribeNamespace(prefix_namespace));

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
        CHECK_NOTHROW(client->SubscribeNamespace(prefix_namespace));

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
        FullTrackName ftn;
        ftn.name_space = TrackNamespace(std::vector<std::string>{ "test", "namespace" });
        ftn.name = { 1, 2, 3 };
        const auto pub_handler = PublishTrackHandler::Create(ftn, TrackMode::kStream, 0, 500);
        pub_handler->SetUseAnnounce(true);

        // Set up promise to capture server receiving PUBLISH_NAMESPACE.
        std::promise<TestServer::PublishNamespaceDetails> server_promise;
        std::future<TestServer::PublishNamespaceDetails> server_future = server_promise.get_future();
        server->SetPublishNamespacePromise(std::move(server_promise));

        // Publush with announce, PUBLISH_NAMESPACE sent.
        CHECK_NOTHROW(client->PublishTrack(pub_handler));

        // Server should receive the PUBLISH_NAMESPACE for the namespace.
        auto server_status = server_future.wait_for(kDefaultTimeout);
        REQUIRE(server_status == std::future_status::ready);
        const auto& server_details = server_future.get();
        CHECK_EQ(server_details.track_namespace, ftn.name_space);

        // Wait for PUBLISH_NAMESPACE_OK to land.
        std::this_thread::sleep_for(kDefaultTimeout);

        // Verify the publish track handler transitions to kNoSubscribers (PUBLISH_NAMESPACE_OK).
        CHECK_EQ(pub_handler->GetStatus(), PublishTrackHandler::Status::kNoSubscribers);
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
