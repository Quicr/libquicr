// SPDX-FileCopyrightText: Copyright (c) 2026 Cisco Systems
// SPDX-License-Identifier: BSD-2-Clause

#include "connect_uri.h"

#include <doctest/doctest.h>

using namespace quicr;

TEST_CASE("Connect URI parsing is anchored and validates the authority")
{
    const auto native = detail::ParseConnectUri("moqt://relay.example:4443/path");
    REQUIRE(native.has_value());
    CHECK(native->host == "relay.example");
    CHECK(native->port == 4443);
    CHECK(native->protocol == Connection::Protocol::kNativeQuic);
    CHECK(native->path == "/path");

    const auto webtransport = detail::ParseConnectUri("https://[2001:db8::1]:443/relay?x=1");
    REQUIRE(webtransport.has_value());
    CHECK(webtransport->host == "2001:db8::1");
    CHECK(webtransport->port == 443);
    CHECK(webtransport->protocol == Connection::Protocol::kWebTransport);
    CHECK(webtransport->path == "/relay?x=1");

    CHECK_FALSE(detail::ParseConnectUri("prefix-moqt://relay.example:4443"));
    CHECK_FALSE(detail::ParseConnectUri("moqt://relay.example:70000"));
    CHECK_FALSE(detail::ParseConnectUri("moqt://user@relay.example:4443"));
    CHECK_FALSE(detail::ParseConnectUri("moqt://2001:db8::1:4443"));
    CHECK_FALSE(detail::ParseConnectUri("moqt://relay.example:"));
}
