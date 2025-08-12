// SPDX-FileCopyrightText: Copyright (c) 2024 Cisco Systems
// SPDX-License-Identifier: BSD-2-Clause

#include "transport_picoquic.h"
#include <memory>
#include <quicr/detail/quic_transport.h>
#include <spdlog/logger.h>
#include <stdexcept>
#include <utility>

namespace quicr {
    TransportException::TransportException(TransportError error, std::source_location location)
      : std::runtime_error("Error in transport (error=" + std::to_string(static_cast<int>(error)) + ", " +
                           std::to_string(location.line()) + ", " + location.file_name() + ")")
      , Error(error)
    {
    }

    std::shared_ptr<ITransport> ITransport::MakeClientTransport(const TransportRemote& server,
                                                                const TransportConfig& tcfg,
                                                                TransportDelegate& delegate,
                                                                std::shared_ptr<TickService> tick_service,
                                                                std::shared_ptr<spdlog::logger> logger)
    {
        switch (server.proto) {
            case TransportProtocol::kQuic:
                return std::make_shared<PicoQuicTransport>(
                  server, tcfg, delegate, false, std::move(tick_service), std::move(logger));
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
        switch (server.proto) {

            case TransportProtocol::kQuic:
                return std::make_shared<PicoQuicTransport>(
                  server, tcfg, delegate, true, std::move(tick_service), std::move(logger));
            default:
                throw std::runtime_error("make_server_transport: Protocol not implemented");
                break;
        }

        return nullptr;
    }

} // namespace quicr
