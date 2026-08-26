// SPDX-FileCopyrightText: Copyright (c) 2026 Cisco Systems
// SPDX-License-Identifier: BSD-2-Clause

#include "transport_picoquic.h"

#include <doctest/doctest.h>

TEST_CASE("Picoquic shards own independent mark-active queues")
{
    quicr::PicoQuicTransport::Shard first;
    quicr::PicoQuicTransport::Shard second;
    quicr::PicoQuicTransport::ConnectionContext connection;

    first.datagram_mark_active_queue.Push(&connection);

    CHECK(first.datagram_mark_active_queue.Pop().value() == &connection);
    CHECK(second.datagram_mark_active_queue.Empty());
}
