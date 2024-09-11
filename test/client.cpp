// SPDX-FileCopyrightText: Copyright (c) 2024 Cisco Systems
// SPDX-License-Identifier: BSD-2-Clause

#include <doctest/doctest.h>

#include <memory>
#include <quicr/client.h>
#include <quicr/config.h>

TEST_CASE("Multiple client creation")
{
    auto client = std::make_shared<quicr::Client>(quicr::ClientConfig());
    client = nullptr;
    client = std::make_shared<quicr::Client>(quicr::ClientConfig());
}
