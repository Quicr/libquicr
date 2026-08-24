// SPDX-FileCopyrightText: Copyright (c) 2026 Cisco Systems
// SPDX-License-Identifier: BSD-2-Clause

#include "quicr/transport.h"
#include "quicr/config.h"

#include <doctest/doctest.h>
#include <spdlog/spdlog.h>
#include <timeq/tick_service.h>

#include <memory>

#if defined(QUICR_HAS_MSQUIC)
#include "msquic_connection.h"
#endif

TEST_CASE("Transport factory honours the configured backend")
{
    quicr::TransportRemote remote{ "127.0.0.1", 4443, quicr::TransportProtocol::kQuic, "/relay" };
    quicr::TransportConfig config;
    auto tick_service = std::make_shared<timeq::threaded_tick_service>();

    auto transport = quicr::Transport::MakeClientTransport(remote, config, tick_service, spdlog::default_logger());
    REQUIRE(transport != nullptr);
    CHECK_EQ(config.transport_backend, quicr::TransportBackend::kPicoQuic);

#if defined(QUICR_HAS_MSQUIC)
    config.transport_backend = quicr::TransportBackend::kMsQuic;
    remote.proto = quicr::TransportProtocol::kWebTransport;
    CHECK_THROWS_WITH_AS(quicr::Transport::MakeClientTransport(remote, config, tick_service, spdlog::default_logger()),
                         "MsQuic transport supports raw QUIC only",
                         std::runtime_error);

    remote.proto = quicr::TransportProtocol::kQuic;
    transport = quicr::Transport::MakeClientTransport(remote, config, tick_service, spdlog::default_logger());
    CHECK(transport != nullptr);
#else
    config.transport_backend = quicr::TransportBackend::kMsQuic;
    CHECK_THROWS_WITH_AS(quicr::Transport::MakeClientTransport(remote, config, tick_service, spdlog::default_logger()),
                         "MsQuic transport support was not built",
                         std::runtime_error);
#endif
}

#if defined(QUICR_HAS_MSQUIC)
TEST_CASE("MsQuic connection retains a stream while start callbacks are pending")
{
    const auto handle = reinterpret_cast<HQUIC>(static_cast<std::uintptr_t>(1));
    auto connection = std::make_shared<quicr::MsQuicConnection>(nullptr, nullptr);
    auto stream = std::make_shared<quicr::MsQuicStream>();
    stream->handle = handle;
    stream->native_handle = handle;
    std::weak_ptr<quicr::MsQuicStream> weak_stream = stream;

    connection->RetainPendingStream(stream);
    stream.reset();
    CHECK_FALSE(weak_stream.expired());

    connection->ReleasePendingStream(handle);
    CHECK(weak_stream.expired());
}
#endif
