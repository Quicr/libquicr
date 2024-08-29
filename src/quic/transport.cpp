// SPDX-FileCopyrightText: Copyright (c) 2024 Cisco Systems
// SPDX-License-Identifier: BSD-2-Clause

#include <moq/detail/quic_transport.h>
#include "transport_picoquic.h"
#include <memory>
#include <stdexcept>
#include <spdlog/logger.h>
#include <utility>

namespace qtransport {

std::shared_ptr<ITransport>
ITransport::MakeClientTransport(const TransportRemote& server,
                                  const TransportConfig& tcfg,
                                  TransportDelegate& delegate,
                                  std::shared_ptr<spdlog::logger> logger)
{
  switch (server.proto) {
     case TransportProtocol::kQuic:
      return std::make_shared<PicoQuicTransport>(server,
                                                 tcfg,
                                                 delegate,
                                                 false,
                                                 std::move(logger));
    default:
      throw std::runtime_error("make_client_transport: Protocol not implemented");
      break;
  }

  return nullptr;
}

std::shared_ptr<ITransport>
ITransport::MakeServerTransport(const TransportRemote& server,
                                  const TransportConfig& tcfg,
                                  TransportDelegate& delegate,
                                  std::shared_ptr<spdlog::logger> logger)
{
  switch (server.proto) {

    case TransportProtocol::kQuic:
      return std::make_shared<PicoQuicTransport>(server,
                                                 tcfg,
                                                 delegate,
                                                 true,
                                                 std::move(logger));
    default:
      throw std::runtime_error("make_server_transport: Protocol not implemented");
      break;
  }

  return nullptr;
}

} // namespace qtransport
