// SPDX-FileCopyrightText: Copyright (c) 2024 Cisco Systems
// SPDX-License-Identifier: BSD-2-Clause

#include <doctest/doctest.h>

#include <moq/client.h>
#include <moq/common.h>
#include <moq/publish_track_handler.h>
#include <moq/server.h>
#include <moq/subscribe_track_handler.h>

#include <spdlog/sinks/stdout_color_sinks.h>

#include <memory>

using namespace moq;

TEST_CASE("Client Constructor")
{
    auto logger = spdlog::stderr_color_mt("TEST");
    auto client = std::make_shared<moq::Client>(moq::ClientConfig{}, logger);
    client.reset(new Client({}, logger));
}
