// SPDX-FileCopyrightText: Copyright (c) 2024 Cisco Systems
// SPDX-License-Identifier: BSD-2-Clause

#include <doctest/doctest.h>

#include <quicr/config.h>
#include <quicr/session.h>

#include <memory>
TEST_CASE("Multiple client creation")
{
    CHECK_NOTHROW({
        auto client = quicr::Session::Create(quicr::ClientConfig());
        client = nullptr;
        client = quicr::Session::Create(quicr::ClientConfig());
    });
}

TEST_CASE("Construction")
{
    CHECK_NOTHROW(quicr::Session::Create(quicr::ClientConfig()));
}

struct BadClient : public quicr::Session
{
    BadClient()
      : quicr::Session(quicr::ClientConfig())
    {
    }
};

TEST_CASE("Construction Non-shared")
{
    BadClient client;
    CHECK_THROWS(client.Connect());
}
