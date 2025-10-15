#include "quicr/config.h"
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
MakeTestServer(const std::optional<std::string>& qlog_path = std::nullopt)
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
    auto server = std::make_shared<TestServer>(server_config);
    const auto starting = server->Start();
    CHECK_EQ(starting, Transport::Status::kReady);
    std::this_thread::sleep_for(std::chrono::milliseconds(kDefaultTimeout));
    return server;
}

std::shared_ptr<TestClient>
MakeTestClient(const bool connect = true, const std::optional<std::string>& qlog_path = std::nullopt)
{
    // Connect a client.
    ClientConfig client_config;
    client_config.transport_config.debug = true;
    client_config.connect_uri = "moq://" + kIp + ":" + std::to_string(kPort);
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
    auto client = MakeTestClient(false);
    std::promise<ServerSetupAttributes> recv_attributes;
    auto future = recv_attributes.get_future();
    client->SetConnectedPromise(std::move(recv_attributes));
    client->Connect();
    auto status = future.wait_for(kDefaultTimeout);
    REQUIRE(status == std::future_status::ready);
    const auto& [moqt_version, server_id] = future.get();
    CHECK_EQ(server_id, kServerId);
}

TEST_CASE("Integration - Subscribe")
{
    auto server = MakeTestServer();
    auto client = MakeTestClient();

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
    CHECK_EQ(details.filter_type, filter_type);

    // Server should respond, track should go live.
    std::this_thread::sleep_for(std::chrono::milliseconds(kDefaultTimeout));
    CHECK_EQ(handler->GetStatus(), SubscribeTrackHandler::Status::kOk);

    // Test is complete, unsubscribe while we are connected.
    CHECK_NOTHROW(client->UnsubscribeTrack(handler));

    // Check track handler cleanup / strong reference cycles.
    CHECK_EQ(handler.use_count(), 1);
}

TEST_CASE("Integration - Fetch")
{
    auto server = MakeTestServer();
    auto client = MakeTestClient();
    FullTrackName ftn;
    ftn.name_space = TrackNamespace({ "namespace" });
    ftn.name = { 1, 2, 3 };
    const auto handler = FetchTrackHandler::Create(ftn, 0, messages::GroupOrder::kOriginalPublisherOrder, 0, 0, 0, 0);
    client->FetchTrack(handler);
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
    auto client = MakeTestClient();
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
}

TEST_CASE("Qlog Generation")
{
    // Create temporary destination for QLOG files.
    const auto temp_dir = std::filesystem::temp_directory_path() / "libquicr_qlog_test";
    std::filesystem::create_directories(temp_dir);
    defer(std::filesystem::remove_all(temp_dir));

    // Enable qlog.
    auto server = MakeTestServer(temp_dir.string());
    auto client = MakeTestClient(true, temp_dir.string());

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
}
