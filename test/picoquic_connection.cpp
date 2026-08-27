// SPDX-FileCopyrightText: Copyright (c) 2026 Cisco Systems
// SPDX-License-Identifier: BSD-2-Clause

#include <doctest/doctest.h>

#include "picoquic_connection.h"

using namespace quicr;

namespace {
    /// The connection only stores the picoquic pointer, so a stand-in address is enough for these tests.
    std::shared_ptr<PicoQuicConnection> MakeConnection()
    {
        return std::make_shared<PicoQuicConnection>(reinterpret_cast<picoquic_cnx_t*>(0x5000));
    }
}

TEST_CASE("Removing a stream closes the handle its holders already have")
{
    const auto connection = MakeConnection();

    const auto stream = connection->AddStream(4, nullptr);
    const auto other = connection->AddStream(8, nullptr);

    REQUIRE(stream->IsOpen());
    REQUIRE(other->IsOpen());

    CHECK(connection->RemoveStream(4) == stream);

    // A holder outside the transport learns the stream is gone through the handle it already has.
    CHECK_FALSE(stream->IsOpen());
    CHECK(other->IsOpen());

    CHECK(connection->GetStream(4) == nullptr);

    // A second removal must report that there is nothing left to tear down.
    CHECK(connection->RemoveStream(4) == nullptr);
}

TEST_CASE("Streams are addressable by ID")
{
    const auto connection = MakeConnection();

    const auto first = connection->AddStream(4, nullptr);
    const auto second = connection->AddStream(8, nullptr);

    // A remote-initiated stream is adopted the first time data arrives on it.
    const auto received = connection->GetOrAddStream(11, nullptr);

    CHECK(connection->GetStreams().size() == 3);
    CHECK(connection->GetStream(4) == first);
    CHECK(connection->GetStream(11) == received);

    // Adopting a stream that already exists must hand back the same object, not replace it.
    CHECK(connection->GetOrAddStream(8, nullptr) == second);

    CHECK(connection->RemoveStream(8) == second);
    CHECK(connection->GetStreams().size() == 2);
}

TEST_CASE("A bidirectional stream is one object carrying both directions")
{
    const auto connection = MakeConnection();

    const auto stream = connection->AddStream(0, nullptr);

    // Receive state is attached lazily by the receive path, on the same object that sends.
    REQUIRE(stream->rx_ctx == nullptr);
    stream->rx_ctx = std::make_shared<StreamRxContext>();

    CHECK(connection->GetStream(0)->rx_ctx == stream->rx_ctx);
}
