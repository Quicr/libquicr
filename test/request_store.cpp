// SPDX-FileCopyrightText: Copyright (c) 2026 Cisco Systems
// SPDX-License-Identifier: BSD-2-Clause

#include "quicr/containers/request_store.h"
#include "quicr/handlers/subscribe_track_handler.h"

#include <doctest/doctest.h>

#include <stdexcept>

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
    CHECK(request->request_stream_id == 4);
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

TEST_CASE("Request store finds and removes data streams by request ID")
{
    quicr::RequestStore store;
    auto handler = quicr::SubscribeTrackHandler::Create({ {}, {} }, 0);
    REQUIRE_NOTHROW(store.Register(2, 4, handler));
    REQUIRE_NOTHROW(store.Register(4, 8, nullptr));
    REQUIRE_NOTHROW(store.AddDataStream(2, 3));
    REQUIRE_NOTHROW(store.AddDataStream(2, 7));
    REQUIRE_NOTHROW(store.AddDataStream(4, 11));
    REQUIRE_NOTHROW(store.AddDataStream(2, 3));

    for (const auto stream_id : { 3, 7 }) {
        const auto request = store.FindByStream(stream_id);
        REQUIRE(request.has_value());
        CHECK(request->request_id == 2);
        CHECK(request->request_stream_id == 4);
        CHECK(request->handler == handler);
    }
    const auto other = store.FindByStream(11);
    REQUIRE(other.has_value());
    CHECK(other->request_id == 4);
    CHECK(other->request_stream_id == 8);
    CHECK(other->handler == nullptr);
    CHECK_FALSE(store.FindByStream(15).has_value());

    REQUIRE_NOTHROW(store.RemoveDataStream(2, 3));
    CHECK_FALSE(store.FindByStream(3).has_value());
    CHECK(store.FindByStream(7).has_value());
    CHECK(store.FindByStream(4).has_value());
    REQUIRE_NOTHROW(store.RemoveDataStream(2, 3));

    // Removing a stream from another request must preserve its mapping.
    REQUIRE_NOTHROW(store.RemoveDataStream(2, 11));
    CHECK(store.FindByStream(11).has_value());
    REQUIRE_NOTHROW(store.RemoveDataStream(2, 4));
    CHECK(store.FindByStream(4).has_value());

    REQUIRE_NOTHROW(store.AddDataStream(4, 3));
    const auto reassigned = store.FindByStream(3);
    REQUIRE(reassigned.has_value());
    CHECK(reassigned->request_id == 4);
}

TEST_CASE("Request store rejects conflicting stream ownership and missing requests")
{
    quicr::RequestStore store;
    REQUIRE_NOTHROW(store.Register(2, 4, nullptr));
    REQUIRE_NOTHROW(store.Register(6, 8, nullptr));
    REQUIRE_NOTHROW(store.AddDataStream(2, 3));

    CHECK_THROWS_AS(store.AddDataStream(6, 3), std::invalid_argument);
    CHECK_THROWS_AS(store.AddDataStream(2, 4), std::invalid_argument);
    CHECK_THROWS_AS(store.AddDataStream(2, 8), std::invalid_argument);
    CHECK_THROWS_AS(store.Register(10, 3, nullptr), std::invalid_argument);
    CHECK_THROWS_AS(store.AddDataStream(10, 7), std::invalid_argument);
    CHECK_THROWS_AS(store.RemoveDataStream(10, 3), std::invalid_argument);
    CHECK_FALSE(store.FindByStream(7).has_value());

    const auto original = store.FindByStream(3);
    REQUIRE(original.has_value());
    CHECK(original->request_id == 2);
    // Rejected registration must not leave the request ID occupied.
    REQUIRE_NOTHROW(store.Register(10, 12, nullptr));
}

TEST_CASE("Request store unregisters all associated streams")
{
    quicr::RequestStore store;

    REQUIRE_NOTHROW(store.Register(2, 4, nullptr));
    CHECK(store.FindByStream(4).has_value());
    REQUIRE_NOTHROW(store.AddDataStream(2, 3));
    REQUIRE_NOTHROW(store.AddDataStream(2, 7));

    REQUIRE_NOTHROW(store.Register(6, 8, nullptr));
    CHECK(store.FindByStream(8).has_value());
    REQUIRE_NOTHROW(store.AddDataStream(6, 11));

    REQUIRE_NOTHROW(store.Unregister(2));
    for (const auto stream_id : { 4, 3, 7 }) {
        CHECK_FALSE(store.FindByStream(stream_id).has_value());
    }
    CHECK(store.FindByStream(8).has_value());
    CHECK(store.FindByStream(11).has_value());
    CHECK_THROWS_AS(store.Unregister(2), std::invalid_argument);
    CHECK_THROWS_AS(store.AddDataStream(2, 15), std::invalid_argument);

    // Both indices release their entries on unregister.
    REQUIRE_NOTHROW(store.Register(2, 4, nullptr));
    REQUIRE_NOTHROW(store.AddDataStream(2, 3));
    REQUIRE_NOTHROW(store.AddDataStream(2, 7));
    CHECK(store.FindByStream(3).has_value());
    CHECK(store.FindByStream(7).has_value());
}
