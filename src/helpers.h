#pragma once

#include "quicr/quicr_common.h"

#include <transport/transport.h>

#include <sstream>

namespace {
constexpr quicr::RelayInfo::Protocol
convert_protocol(qtransport::TransportProtocol protocol)
{
  switch (protocol) {
    case qtransport::TransportProtocol::UDP:
      return quicr::RelayInfo::Protocol::UDP;
    case qtransport::TransportProtocol::QUIC:
      return quicr::RelayInfo::Protocol::QUIC;
    default:
      throw std::invalid_argument("Unsupported transport protocol");
  }
}

constexpr qtransport::TransportProtocol
convert_protocol(quicr::RelayInfo::Protocol protocol)
{
  switch (protocol) {
    case quicr::RelayInfo::Protocol::UDP:
      return qtransport::TransportProtocol::UDP;
    case quicr::RelayInfo::Protocol::QUIC:
      return qtransport::TransportProtocol::QUIC;
    default:
      throw std::invalid_argument("Unsupported relay protocol");
  }
}
}

// TODO(trigaux): Getting ready for swapping to some fancier logging lib.
// clang-format off
#define LOG(logger, level, msg) do { std::ostringstream os; os << msg; logger.log(level, os.str()); } while(0)
#define LOG_FATAL(logger, msg) LOG(logger, qtransport::LogLevel::fatal, msg)
#define LOG_CRITICAL(logger, msg) LOG(logger, qtransport::LogLevel::fatal, msg)
#define LOG_ERROR(logger, msg) LOG(logger, qtransport::LogLevel::error, msg)
#define LOG_WARNING(logger, msg)  LOG(logger, qtransport::LogLevel::warn, msg)
#define LOG_INFO(logger, msg)  LOG(logger, qtransport::LogLevel::info, msg)
#define LOG_DEBUG(logger, msg) LOG(logger, qtransport::LogLevel::debug, msg)
// clang-format on
