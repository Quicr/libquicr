#include "transport_udp.h"
#include <transport/transport.h>
#if not defined(PLATFORM_ESP)
  #include "transport_picoquic.h"
#endif
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
    case TransportProtocol::kUdp:
      return std::make_shared<UDPTransport>(server, tcfg, delegate, false, std::move(logger));
#ifndef PLATFORM_ESP
    case TransportProtocol::kQuic:
      return std::make_shared<PicoQuicTransport>(server,
                                                 tcfg,
                                                 delegate,
                                                 false,
                                                 std::move(logger));
#endif
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
    case TransportProtocol::kUdp:
      return std::make_shared<UDPTransport>(server, tcfg, delegate, true, std::move(logger));

#ifndef PLATFORM_ESP
    case TransportProtocol::kQuic:
      return std::make_shared<PicoQuicTransport>(server,
                                                 tcfg,
                                                 delegate,
                                                 true,
                                                 std::move(logger));
#endif
    default:
      throw std::runtime_error("make_server_transport: Protocol not implemented");
      break;
  }

  return nullptr;
}

} // namespace qtransport
