// SPDX-FileCopyrightText: Copyright (c) 2026 Cisco Systems
// SPDX-License-Identifier: BSD-2-Clause

#include "connect_uri.h"

#include <doctest/doctest.h>

using namespace quicr;

TEST_CASE("Connect URI parsing is anchored and validates the authority")
{
    const auto native = detail::ParseConnectUri("moqt://relay.example:4443/path", false);
    REQUIRE(native.has_value());
    CHECK(native->host == "relay.example");
    CHECK(native->port == 4443);
    CHECK(native->protocol == Connection::Protocol::kNativeQuic);
    CHECK(native->path == "/path");

    const auto webtransport = detail::ParseConnectUri("https://[2001:db8::1]:443/relay?x=1", false);
    REQUIRE(webtransport.has_value());
    CHECK(webtransport->host == "2001:db8::1");
    CHECK(webtransport->port == 443);
    CHECK(webtransport->protocol == Connection::Protocol::kWebTransport);
    CHECK(webtransport->path == "/relay?x=1");

    CHECK_FALSE(detail::ParseConnectUri("prefix-moqt://relay.example:4443", false));
    CHECK_FALSE(detail::ParseConnectUri("moqt://relay.example:70000", false));
    CHECK_FALSE(detail::ParseConnectUri("moqt://user@relay.example:4443", false));
    CHECK_FALSE(detail::ParseConnectUri("moqt://2001:db8::1:4443", false));
    CHECK_FALSE(detail::ParseConnectUri("moqt://relay.example:", false));
}

TEST_CASE("Connect URI parsing preserves QMUX and rejects WebTransport over QMUX")
{
    for (const auto* uri : { "moq://relay.example:4443/path", "moqt://relay.example:4443/path" }) {
        const auto qmux = detail::ParseConnectUri(uri, true);
        REQUIRE(qmux.has_value());
        CHECK(qmux->host == "relay.example");
        CHECK(qmux->port == 4443);
        CHECK(qmux->protocol == Connection::Protocol::kQmux);
        CHECK(qmux->path == "/path");
    }

    CHECK_FALSE(detail::ParseConnectUri("https://relay.example:443/relay", true));
}
