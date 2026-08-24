// SPDX-FileCopyrightText: Copyright (c) 2024 Cisco Systems
// SPDX-License-Identifier: BSD-2-Clause

#include "quicr/transport.h"
#include "transport_picoquic.h"
#if defined(QUICR_HAS_MSQUIC)
#include "transport_msquic.h"
#endif

#include <spdlog/logger.h>

#include <memory>
#include <stdexcept>
#include <utility>

using namespace quicr;

namespace quicr {

    std::shared_ptr<Transport> Transport::MakeClientTransport(const TransportRemote& server,
                                                              const TransportConfig& tcfg,
                                                              std::shared_ptr<timeq::tick_service> tick_service,
                                                              std::shared_ptr<spdlog::logger> logger)
    {
        if (tcfg.transport_backend == TransportBackend::kMsQuic) {
#if defined(QUICR_HAS_MSQUIC)
            if (server.proto != TransportProtocol::kQuic) {
                throw std::runtime_error("MsQuic transport supports raw QUIC only");
            }
            return std::make_shared<MsQuicTransport>(server, tcfg, false, std::move(tick_service), std::move(logger));
#else
            throw std::runtime_error("MsQuic transport support was not built");
#endif
        }

        switch (server.proto) {
            case TransportProtocol::kQuic:
                return std::make_shared<PicoQuicTransport>(
                  server, tcfg, false, std::move(tick_service), std::move(logger), Connection::API::kNativeQuic);
            case TransportProtocol::kWebTransport:
                return std::make_shared<PicoQuicTransport>(
                  server, tcfg, false, std::move(tick_service), std::move(logger), Connection::API::kWebTransport);
            default:
                throw std::runtime_error("make_client_transport: Protocol not implemented");
                break;
        }

        return nullptr;
    }

    std::shared_ptr<Transport> Transport::MakeServerTransport(const TransportRemote& server,
                                                              const TransportConfig& tcfg,
                                                              std::shared_ptr<timeq::tick_service> tick_service,
                                                              std::shared_ptr<spdlog::logger> logger)
    {
        if (tcfg.transport_backend == TransportBackend::kMsQuic) {
#if defined(QUICR_HAS_MSQUIC)
            return std::make_shared<MsQuicTransport>(server, tcfg, true, std::move(tick_service), std::move(logger));
#else
            throw std::runtime_error("MsQuic transport support was not built");
#endif
        }

        // Server mode supports BOTH raw QUIC (moqt-18) and WebTransport (h3) simultaneously.
        //
        // The server.proto field is IGNORED - the transport mode is automatically determined
        // per-connection based on the ALPN negotiated with each client:
        //   - Client sends ALPN "moqt-18" -> ConnectionContext.transport_mode = Connection::API::kNativeQuic
        //   - Client sends ALPN "h3"     -> ConnectionContext.transport_mode = Connection::API::kWebTransport
        //
        // See PqAlpnSelectCb() in transport_picoquic.cpp for ALPN selection logic.
        // See CreateConnection() in transport_picoquic.cpp for per-connection mode assignment.
        //
        // The Connection::API parameter passed to PicoQuicTransport constructor is only used as
        // a default/fallback and is overridden for each connection based on ALPN.
        return std::make_shared<PicoQuicTransport>(
          server, tcfg, true, std::move(tick_service), std::move(logger), Connection::API::kNativeQuic);
    }

} // namespace quicr
