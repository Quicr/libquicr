// SPDX-FileCopyrightText: Copyright (c) 2026 Cisco Systems
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

#include "quicr/connection.h"

#include <algorithm>
#include <charconv>
#include <cstdint>
#include <iterator>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

namespace quicr::detail {

    struct ParsedConnectUri
    {
        std::string host;
        std::uint16_t port;
        Connection::Protocol protocol;
        std::string path;
    };

    inline std::optional<ParsedConnectUri> ParseConnectUri(const std::string& connect_uri)
    {
        if (connect_uri.empty() || connect_uri.size() > 2048 ||
            std::ranges::any_of(connect_uri, [](unsigned char ch) { return ch <= 0x20 || ch == 0x7f; })) {
            return std::nullopt;
        }

        // Support moq://, moqt://, and https:// (for WebTransport).
        std::string_view remainder;
        Connection::Protocol protocol = Connection::Protocol::kNativeQuic;

        if (connect_uri.starts_with("moq://")) {
            remainder = std::string_view(connect_uri).substr(6);
        } else if (connect_uri.starts_with("moqt://")) {
            remainder = std::string_view(connect_uri).substr(7);
        } else if (connect_uri.starts_with("https://")) {
            remainder = std::string_view(connect_uri).substr(8);
            protocol = Connection::Protocol::kWebTransport;
        } else {
            return std::nullopt;
        }

        const auto path_pos = remainder.find_first_of("/?");
        const auto authority = remainder.substr(0, path_pos);
        if (authority.empty() || authority.find('@') != std::string_view::npos) {
            return std::nullopt;
        }

        std::string_view host_view;
        std::string_view port_view;
        if (authority.front() == '[') {
            const auto close = authority.find(']');
            if (close == std::string_view::npos) {
                return std::nullopt;
            }
            host_view = authority.substr(1, close - 1);
            if (close + 1 < authority.size()) {
                if (authority[close + 1] != ':') {
                    return std::nullopt;
                }
                port_view = authority.substr(close + 2);
            }
        } else {
            const auto colon = authority.rfind(':');
            if (colon != std::string_view::npos) {
                if (authority.find(':') != colon) {
                    return std::nullopt; // IPv6 literals must use brackets.
                }
                host_view = authority.substr(0, colon);
                port_view = authority.substr(colon + 1);
            } else {
                host_view = authority;
            }
        }
        if (host_view.empty()) {
            return std::nullopt;
        }

        std::uint16_t port = 0;
        if (!port_view.empty()) {
            unsigned int parsed_port = 0;
            const auto [end, error] =
              std::from_chars(port_view.data(), port_view.data() + port_view.size(), parsed_port);
            if (error != std::errc{} || end != port_view.data() + port_view.size() || parsed_port == 0 ||
                parsed_port > std::numeric_limits<std::uint16_t>::max()) {
                return std::nullopt;
            }
            port = static_cast<std::uint16_t>(parsed_port);
        } else if (authority.ends_with(':')) {
            return std::nullopt;
        }

        std::string path;
        if (path_pos != std::string_view::npos) {
            path.assign(remainder.substr(path_pos));
        }

        return ParsedConnectUri{
            .host = std::string(host_view),
            .port = port,
            .protocol = protocol,
            .path = std::move(path),
        };
    }

} // namespace quicr::detail
