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

static quicr::Client::Status
Connect(std::string uri)
{
    quicr::ClientConfig config = { .connect_uri = uri };
    const auto client = std::make_shared<quicr::Client>(config);
    return client->Connect();
}

TEST_CASE("URI Parse")
{
    // IPv4 valid.
    CHECK_EQ(Connect("moq://127.0.0.1:8080"), quicr::Client::Status::kConnecting);
    // IPv6 wrapped.
    CHECK_EQ(Connect("moq://[fe80::1004:921d:48e:9a7d]:8080"), quicr::Client::Status::kConnecting);
    // IPv6 raw.
    CHECK_EQ(Connect("moq://fe80::1004:921d:48e:9a7d:8080"), quicr::Client::Status::kConnecting);
    // No protocol is invalid.
    CHECK_EQ(Connect("127.0.0.1:8080"), quicr::Client::Status::kInvalidParams);
}