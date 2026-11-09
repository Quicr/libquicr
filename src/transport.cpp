// SPDX-FileCopyrightText: Copyright (c) 2024 Cisco Systems
// SPDX-License-Identifier: BSD-2-Clause

#include "quicr/transport.h"
#include "quicr/clock.h"
#include "quicr/log.h"
#include "transport_picoquic.h"

#include <memory>
#include <utility>

using namespace quicr;

namespace quicr {

    std::shared_ptr<Transport> Transport::MakeClientTransport(const TransportRemote& server,
                                                              const TransportConfig& tcfg,
                                                              std::shared_ptr<Clock> clock,
                                                              std::shared_ptr<Logger> logger)
    {
        return std::make_shared<PicoQuicTransport>(server, tcfg, false, std::move(clock), std::move(logger));
    }

    std::shared_ptr<Transport> Transport::MakeServerTransport(const TransportRemote& server,
                                                              const TransportConfig& tcfg,
                                                              std::shared_ptr<Clock> clock,
                                                              std::shared_ptr<Logger> logger)
    {
        // Server mode supports BOTH raw QUIC (moq-00) and WebTransport (h3) simultaneously.
        //
        // The transport mode is automatically determined per connection based on the
        // ALPN negotiated with each client:
        //   - Client sends ALPN "moq-00" -> native QUIC
        //   - Client sends ALPN "h3"     -> WebTransport
        //
        // See PqAlpnSelectCb() in transport_picoquic.cpp for ALPN selection logic.
        // See CreateConnection() in transport_picoquic.cpp for per-connection mode assignment.
        return std::make_shared<PicoQuicTransport>(server, tcfg, true, std::move(clock), std::move(logger));
    }

} // namespace quicr
