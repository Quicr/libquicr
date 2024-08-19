/*
 *  Copyright (C) 2024
 *  Cisco Systems, Inc.
 *  All Rights Reserved
 */

#pragma once

#include <transport/transport.h>
#include <string>

namespace moq::transport {

    constexpr uint64_t kMoqtVersion = 0xff000004;  ///< draft-ietf-moq-transport-04
    constexpr uint64_t kSubscribeExpires = 0; ///< Never expires
    constexpr int kReadLoopMaxPerStream = 60; ///< Support packet/frame bursts, but do not allow starving other streams

    using namespace qtransport;

    struct Config
    {
        std::string endpoint_id;                 ///< Endpoint ID for the client or server, should be unique
        TransportConfig transport_config;
    };

    struct ClientConfig : Config
    {
        std::string moq_uri;                     ///< MoQT URI such as moqt://<relay>[:port][/path?query]
    };

    struct ServerConfig : Config
    {

        std::string server_bind_ip;             ///< IP address to bind to, can be 0.0.0.0
        uint16_t server_port;                   ///< Listening port for server
    };

} // namespace moq::transport
