/*
 *  Copyright (C) 2024
 *  Cisco Systems, Inc.
 *  All Rights Reserved
 */

#pragma once

#include <transport/transport.h>
#include <string>

namespace moq::transport {
    constexpr uint64_t MOQT_VERSION = 0xff000004;  ///< draft-ietf-moq-transport-04
    constexpr uint64_t SUBSCRIBE_EXPIRES = 0; ///< Never expires
    constexpr int READ_LOOP_MAX_PER_STREAM =
      60; ///< Support packet/frame bursts, but do not allow starving other streams

    using namespace qtransport;

    struct Config
    {
        std::string endpoint_id; ///< Endpoint ID for the client or server, should be unique
        TransportConfig transport_config;
    };

    struct ClientConfig : Config
    {
        std::string server_host_ip;     ///< Relay hostname or IP to connect to
        uint16_t server_port;           ///< Relay port to connect to
        TransportProtocol server_proto; ///< Protocol to use when connecting to relay
    };

    struct ServerConfig : Config
    {

        std::string server_bind_ip;     ///< IP address to bind to, can be 0.0.0.0
        uint16_t server_port;           ///< Listening port for server
        TransportProtocol server_proto; ///< Protocol to use
    };

} // namespace quicr
