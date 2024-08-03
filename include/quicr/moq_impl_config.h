/*
 *  Copyright (C) 2024
 *  Cisco Systems, Inc.
 *  All Rights Reserved
 */

#pragma once

#include <quicr/metrics_exporter.h>
#include <quicr/quicr_common.h>
#include <transport/transport.h>

#include <quicr/moq_base_track_handler.h>
#include <quicr/moq_client_delegate.h>
#include <quicr/moq_server_delegate.h>

#include <unordered_map>
#include <map>
#include <string>
#include <string_view>

namespace quicr {
    constexpr uint64_t MOQT_VERSION = 0xff000004;             ///< draft-ietf-moq-transport-04
    constexpr uint64_t MOQT_SUBSCRIBE_EXPIRES = 0;            ///< Never expires
    constexpr int MOQT_READ_LOOP_MAX_PER_STREAM = 60;         ///< Support packet/frame bursts, but do not allow starving other streams

    using namespace qtransport;

    struct MoQImplConfig
    {
        std::string endpoint_id; ///< Endpoint ID for the client or server, should be unique
        TransportConfig transport_config;
    };

    struct MoQClientConfig : MoQImplConfig
    {
        std::string server_host_ip;     ///< Relay hostname or IP to connect to
        uint16_t server_port;           ///< Relay port to connect to
        TransportProtocol server_proto; ///< Protocol to use when connecting to relay
    };

    struct MoQServerConfig : MoQImplConfig
    {

        std::string server_bind_ip;     ///< IP address to bind to, can be 0.0.0.0
        uint16_t server_port;           ///< Listening port for server
        TransportProtocol server_proto; ///< Protocol to use
    };

} // namespace quicr
