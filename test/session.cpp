// SPDX-FileCopyrightText: Copyright (c) 2024 Cisco Systems
// SPDX-License-Identifier: BSD-2-Clause

#include "quicr/session.h"
#include "quicr/config.h"

#include <doctest/doctest.h>

#include <memory>

TEST_CASE("Multiple client creation")
{
    CHECK_NOTHROW({
        auto client = quicr::Session::Create(quicr::ClientConfig(), nullptr, nullptr, nullptr);
        client = nullptr;
        client = quicr::Session::Create(quicr::ClientConfig(), nullptr, nullptr, nullptr);
    });
}

TEST_CASE("Construction")
{
    CHECK_NOTHROW(quicr::Session::Create(quicr::ClientConfig(), nullptr, nullptr, nullptr));
}

struct BadClient : public quicr::Session
{
    BadClient()
      : quicr::Session(quicr::ClientConfig(), nullptr, nullptr, nullptr)
    {
    }
};

TEST_CASE("Construction Non-shared")
{
    BadClient client;
}
