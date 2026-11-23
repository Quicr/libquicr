// SPDX-FileCopyrightText: Copyright (c) 2026 Cisco Systems
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

#include "quicr/config.h"

#include <cstdint>
#include <optional>
#include <string>

namespace quicr_test {

    inline const std::string kIp = "127.0.0.1";
    inline const std::string kServerId = "test-server";

    std::uint16_t GetTestPort();

    quicr::ServerConfig MakeTestServerConfig(std::optional<std::uint16_t> server_port = std::nullopt,
                                             bool enable_qmux = false);

    quicr::ClientConfig MakeTestClientConfig(const std::string& protocol_scheme,
                                             const std::string& host = kIp,
                                             std::uint16_t port = GetTestPort());

} // namespace quicr_test
