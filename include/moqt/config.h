/*
 *  Copyright (C) 2024
 *  Cisco Systems, Inc.
 *  All Rights Reserved
 */

#pragma once

#include <transport/transport.h>
#include <string>

namespace moq::transport {

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
