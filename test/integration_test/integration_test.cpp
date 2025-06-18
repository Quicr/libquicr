#include "quicr/config.h"

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include "test_client.h"
#include "test_server.h"
#include <future>
#include <iostream>
#include <string>

using namespace quicr;
using namespace quicr_test;

const std::string kIp = "127.0.0.1";
constexpr uint16_t kPort = 12345;
const std::string kServerId = "test-server";
constexpr std::chrono::milliseconds kDefaultTimeout(50);

static std::unique_ptr<TestServer>
MakeTestServer()
{
    // Run the server.
    ServerConfig server_config;
    server_config.server_bind_ip = kIp;
    server_config.server_port = kPort;
    server_config.endpoint_id = kServerId;
    server_config.transport_config.debug = true;
    server_config.transport_config.tls_cert_filename = "server-cert.pem";
    server_config.transport_config.tls_key_filename = "server-key.pem";
    auto server = std::make_unique<TestServer>(server_config);
    const auto starting = server->Start();
    CHECK_EQ(starting, Transport::Status::kReady);
    std::this_thread::sleep_for(std::chrono::milliseconds(kDefaultTimeout));
    return server;
}

std::unique_ptr<TestClient>
MakeTestClient(const bool connect = true)
{
    // Connect a client.
    ClientConfig client_config;
    client_config.connect_uri = "moq://" + kIp + ":" + std::to_string(kPort);
    auto client = std::make_unique<TestClient>(client_config);
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
    std::optional<ServerSetupAttributes> recv_attributes;
    client->SetClientConnectedCallback([&recv_attributes](const ServerSetupAttributes& server_setup_attributes) {
        recv_attributes.emplace(server_setup_attributes);
        CHECK_EQ(server_setup_attributes.server_id, kServerId);
    });
    client->Connect();

    // Wait for the client to connect.
    std::this_thread::sleep_for(kDefaultTimeout);

    // Ensure we've received the server setup attributes.
    CHECK_MESSAGE(recv_attributes.has_value(), "Client didn't receive server setup attributes");
}

TEST_CASE("Integration - Subscribe")
{
    auto server = MakeTestServer();
    auto client = MakeTestClient();

    // Make a subscription.
    FullTrackName ftn;
    ftn.name_space = TrackNamespace({ "namespace" });
    ftn.name = { 1, 2, 3 };
    constexpr auto filter_type = messages::FilterType::kLatestObject;
    const auto handler =
      SubscribeTrackHandler::Create(ftn, 0, messages::GroupOrder::kOriginalPublisherOrder, filter_type);

    // When we subscribe, server should receive a subscribe.
    std::promise<TestServer::SubscribeDetails> promise;
    std::future<TestServer::SubscribeDetails> future = promise.get_future();
    server->SetSubscribePromise(std::move(promise));

    // Subscribe.
    client->SubscribeTrack(handler);

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
}
