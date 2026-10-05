// SPDX-FileCopyrightText: Copyright (c) 2026 Cisco Systems
// SPDX-License-Identifier: BSD-2-Clause

#include "quicr/containers/request_store.h"
#include "quicr/handlers/subscribe_track_handler.h"

#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <barrier>
#include <vector>

TEST_CASE("Request store finds requests by stream")
{
    quicr::RequestStore store;

    // Store and lookup with handler.
    CHECK_FALSE(store.FindByStream(4).has_value());
    auto handler = quicr::SubscribeTrackHandler::Create({ {}, {} }, 0);
    REQUIRE_NOTHROW(store.Register(2, 4, handler));
    auto request = store.FindByStream(4);
    REQUIRE(request.has_value());
    CHECK(request->request_id == 2);
    CHECK(request->stream_id == 4);
    CHECK(request->handler == handler);

    // Store and lookup without handler.
    REQUIRE_NOTHROW(store.Register(6, 8, nullptr));
    const auto unbound = store.FindByStream(8);
    REQUIRE(unbound.has_value());
    CHECK(unbound->request_id == 6);
    CHECK(unbound->handler == nullptr);
}

TEST_CASE("Request store rejects duplicates")
{
    // Valid store.
    quicr::RequestStore store;
    auto handler = quicr::SubscribeTrackHandler::Create({ {}, {} }, 0);
    REQUIRE_NOTHROW(store.Register(2, 4, handler));

    // Restore with differing IDs should throw.
    auto replacement = quicr::SubscribeTrackHandler::Create({ {}, {} }, 0);
    CHECK_THROWS(store.Register(2, 8, replacement));
    CHECK_FALSE(store.FindByStream(8).has_value());
    CHECK_THROWS(store.Register(6, 4, replacement));

    // Original lookup still works.
    const auto original = store.FindByStream(4);
    REQUIRE(original.has_value());
    CHECK(original->request_id == 2);
    CHECK(original->handler == handler);

    // New lookup still works.
    REQUIRE_NOTHROW(store.Register(6, 8, replacement));
    const auto added = store.FindByStream(8);
    REQUIRE(added.has_value());
    CHECK(added->request_id == 6);
    CHECK(added->handler == replacement);
}
