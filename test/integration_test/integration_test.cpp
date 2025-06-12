#include "quicr/config.h"

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include "test_client.h"
#include "test_server.h"
#include <iostream>
#include <string>

using namespace quicr;
using namespace quicr_test;

TEST_SUITE("Integration Tests")
{
    TEST_CASE("Connect")
    {
        // Server ID.
        const std::string server_id = "test-server";
        const std::string ip = "127.0.0.1";
        constexpr uint16_t port = 12345;

        // Run the server.
        ServerConfig server_config;
        server_config.server_bind_ip = ip;
        server_config.server_port = port;
        server_config.endpoint_id = server_id;
        server_config.transport_config.debug = true;
        server_config.transport_config.tls_cert_filename = "server-cert.pem";
        server_config.transport_config.tls_key_filename = "server-key.pem";
        auto server = TestServer(server_config);
        const auto starting = server.Start();
        CHECK_EQ(starting, Transport::Status::kReady);

        // Wait for the server to start.
        std::this_thread::sleep_for(std::chrono::milliseconds(50));

        // Connect a client.
        ClientConfig client_config;
        client_config.connect_uri = "moq://" + ip + ":" + std::to_string(port);
        auto client = TestClient(client_config);
        std::optional<ServerSetupAttributes> recv_attributes;
        client.SetClientConnected([&recv_attributes, server_id](const ServerSetupAttributes& server_setup_attributes) {
            recv_attributes.emplace(server_setup_attributes);
            CHECK_EQ(server_setup_attributes.server_id, server_id);
        });
        client.Connect();

        // Wait for the client to connect.
        std::this_thread::sleep_for(std::chrono::milliseconds(50));

        // Ensure we've received the server setup attributes.
        CHECK_MESSAGE(recv_attributes.has_value(), "Client didn't receive server setup attributes");

        // Stop the server.
        client.Disconnect();
        server.Stop();
    }
}
