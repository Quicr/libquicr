// SPDX-FileCopyrightText: Copyright (c) 2024 Cisco Systems
// SPDX-License-Identifier: BSD-2-Clause

#include "transport_picoquic.h"
#include <memory>
#include <quicr/detail/quic_transport.h>
#include <spdlog/logger.h>
#include <stdexcept>
#include <utility>

using namespace quicr;

namespace quicr {

    std::shared_ptr<ITransport> ITransport::MakeClientTransport(const TransportRemote& server,
                                                                const TransportConfig& tcfg,
                                                                TransportDelegate& delegate,
                                                                std::shared_ptr<TickService> tick_service,
                                                                std::shared_ptr<spdlog::logger> logger)
    {
        switch (server.proto) {
            case TransportProtocol::kQuic:
                return std::make_shared<PicoQuicTransport>(
                  server, tcfg, delegate, false, std::move(tick_service), std::move(logger), TransportMode::kQuic);
            case TransportProtocol::kWebTransport:
                return std::make_shared<PicoQuicTransport>(
                  server, tcfg, delegate, false, std::move(tick_service), std::move(logger), TransportMode::kWebTransport);
            default:
                throw std::runtime_error("make_client_transport: Protocol not implemented");
                break;
        }

        return nullptr;
    }

    std::shared_ptr<ITransport> ITransport::MakeServerTransport(const TransportRemote& server,
                                                                const TransportConfig& tcfg,
                                                                TransportDelegate& delegate,
                                                                std::shared_ptr<TickService> tick_service,
                                                                std::shared_ptr<spdlog::logger> logger)
    {
        // Server mode supports BOTH raw QUIC (moq-00) and WebTransport (h3) simultaneously.
        //
        // The server.proto field is IGNORED - the transport mode is automatically determined
        // per-connection based on the ALPN negotiated with each client:
        //   - Client sends ALPN "moq-00" -> ConnectionContext.transport_mode = TransportMode::kQuic
        //   - Client sends ALPN "h3"     -> ConnectionContext.transport_mode = TransportMode::kWebTransport
        //
        // See PqAlpnSelectCb() in transport_picoquic.cpp for ALPN selection logic.
        // See CreateConnContext() in transport_picoquic.cpp for per-connection mode assignment.
        //
        // The TransportMode parameter passed to PicoQuicTransport constructor is only used as
        // a default/fallback and is overridden for each connection based on ALPN.
        return std::make_shared<PicoQuicTransport>(
          server, tcfg, delegate, true, std::move(tick_service), std::move(logger), TransportMode::kQuic);
    }

} // namespace quicr
