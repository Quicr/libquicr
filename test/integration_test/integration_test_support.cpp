// SPDX-FileCopyrightText: Copyright (c) 2026 Cisco Systems
// SPDX-License-Identifier: BSD-2-Clause

#include "integration_test_support.h"

#include <cstdlib>
#include <limits>

namespace quicr_test {

    namespace {
        constexpr std::uint16_t kDefaultPort = 12345;
    }

    std::uint16_t GetTestPort()
    {
        const auto* env_port = std::getenv("LIBQUICR_TEST_PORT");
        if (env_port == nullptr) {
            return kDefaultPort;
        }

        try {
            const auto parsed_port = std::stoul(env_port);
            if (parsed_port > 0 && parsed_port <= std::numeric_limits<std::uint16_t>::max()) {
                return static_cast<std::uint16_t>(parsed_port);
            }
        } catch (...) {
        }

        return kDefaultPort;
    }

    quicr::ServerConfig MakeTestServerConfig(const std::optional<std::uint16_t> server_port)
    {
        quicr::ServerConfig server_config;
        server_config.server_bind_ip = kIp;
        server_config.server_port = server_port.value_or(GetTestPort());
        server_config.endpoint_id = kServerId;
        server_config.transport_config.debug = true;
        server_config.transport_config.tls_cert_filename = "server-cert.pem";
        server_config.transport_config.tls_key_filename = "server-key.pem";
        server_config.transport_config.time_queue_max_duration = 10000;
        return server_config;
    }

    quicr::ClientConfig MakeTestClientConfig(const std::string& protocol_scheme, const std::string& host)
    {
        quicr::ClientConfig client_config;
        client_config.transport_config.debug = true;
        client_config.endpoint_id = "client";
        client_config.transport_config.time_queue_max_duration = 10000;
        client_config.connect_uri = protocol_scheme + "://" + host + ":" + std::to_string(GetTestPort()) + "/relay";
        return client_config;
    }

} // namespace quicr_test
