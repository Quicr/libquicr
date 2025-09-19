// SPDX-FileCopyrightText: Copyright (c) 2024 Cisco Systems
// SPDX-License-Identifier: BSD-2-Clause

#include <doctest/doctest.h>

#include <memory>
#include <quicr/client.h>
#include <quicr/config.h>

TEST_CASE("Multiple client creation")
{
    CHECK_NOTHROW({
        auto client = quicr::Client::Create(quicr::ClientConfig());
        client = nullptr;
        client = quicr::Client::Create(quicr::ClientConfig());
    });
}

TEST_CASE("Construction")
{
    CHECK_NOTHROW(quicr::Client::Create(quicr::ClientConfig()));
}

struct BadClient : public quicr::Client
{
    BadClient()
      : quicr::Client(quicr::ClientConfig())
    {
    }
};

TEST_CASE("Construction Non-shared")
{
    BadClient client;
    CHECK_THROWS(client.Connect());
}
