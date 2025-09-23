// SPDX-FileCopyrightText: Copyright (c) 2024 Cisco Systems
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

#include "quicr/version.h"
#include <quicr/detail/quic_transport.h>
#include <string>

namespace quicr {

    struct Config
    {
        std::string endpoint_id; ///< Endpoint ID for the client or server, should be unique
                                 ///< working to add to protocol: https://github.com/moq-wg/moq-transport/issues/461

        quicr::TransportConfig transport_config;
        uint64_t metrics_sample_ms{ 5000 };
        uint32_t unknown_stream_expiry_ms{ 3000 }; ///< Milliseconds age of how long a stream is kept when unknown
    };

    struct ClientConfig : Config
    {
        std::string connect_uri; ///< URI such as moqt://relay[:port][/path?query]
        std::uint64_t tick_service_sleep_delay_us{ 333 };
    };

    struct ServerConfig : Config
    {

        std::string server_bind_ip; ///< IP address to bind to, can be 0.0.0.0 or ::
                                    ///< Empty will be treated as ANY
        uint16_t server_port;       ///< Listening port for server
        std::uint64_t tick_service_sleep_delay_us{ 333 };
    };

} // namespace moq
