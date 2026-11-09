// SPDX-FileCopyrightText: Copyright (c) 2026 Cisco Systems
// SPDX-License-Identifier: BSD-2-Clause

#include "quicr/log.h"
#include "quicr/session_callbacks.h"
#include "quicr/session_manager.h"

#include <arpa/inet.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <string_view>
#include <thread>

namespace {
    class ConsoleLogger final : public quicr::Logger
    {
      public:
        void SetLevel(Level level) override { level_ = level; }
        bool ShouldLog(Level level) const noexcept override { return level >= level_.load(); }

        void Log(Level level, std::string_view message, std::source_location) override
        {
            if (!ShouldLog(level)) {
                return;
            }
            std::lock_guard lock(mutex_);
            std::cerr << message << '\n';
        }

      private:
        std::atomic<Level> level_{ Level::Info };
        std::mutex mutex_;
    };

    class ClientCallbacks final : public quicr::Session::ClientCallbacks
    {
      public:
        void StatusChanged(const std::shared_ptr<quicr::Session>& session, quicr::Session::Status status) override
        {
            if (status == quicr::Session::Status::kReady) {
                std::cout << "MoQ ready, connection " << session->GetConnection()->GetID() << '\n';
            } else if (status == quicr::Session::Status::kNotConnected) {
                std::cout << "MoQ disconnected\n";
            }
            status_ = status;
        }

        std::atomic<quicr::Session::Status> status_{ quicr::Session::Status::kConnecting };
    };
}

int
main(int argc, char** argv)
{
    if (argc != 2 && !(argc == 5 && std::string_view(argv[1]) == "--server")) {
        std::cerr << "Usage: quicr_path_probe <moq://relay:port or https://relay:port/relay>\n"
                     "       quicr_path_probe --server <port> <cert.pem> <key.pem>\n";
        return 2;
    }

    auto logger = std::make_shared<ConsoleLogger>();
    quicr::SessionManager manager(logger);
    if (argc == 5) {
        unsigned long port = 0;
        try {
            port = std::stoul(argv[2]);
        } catch (...) {
            std::cerr << "Invalid UDP port\n";
            return 2;
        }
        if (port == 0 || port > UINT16_MAX) {
            std::cerr << "Invalid UDP port\n";
            return 2;
        }
        quicr::ServerConfig server;
        server.server_bind_ip = "0.0.0.0";
        server.server_port = static_cast<std::uint16_t>(port);
        server.endpoint_id = "path-probe-server";
        server.transport_config.tls_cert_filename = argv[3];
        server.transport_config.tls_key_filename = argv[4];
        manager.AddTransport(server, std::make_shared<quicr::Session::ServerCallbacks>());
        std::cout << "Server listening on UDP " << server.server_port << "; type quit to exit.\n";
        std::string command;
        while (std::getline(std::cin, command) && command != "quit") {
        }
        return 0;
    }

    auto callbacks = std::make_shared<ClientCallbacks>();
    quicr::ClientConfig config;
    config.endpoint_id = "path-probe";
    config.connect_uri = argv[1];
    if (const char* qlog = std::getenv("LIBQUICR_PATH_PROBE_QLOG")) {
        config.transport_config.quic_qlog_path = qlog;
    }
    auto session = manager.AddTransport(config, callbacks).lock();
    if (!session) {
        std::cerr << "Could not start session\n";
        return 1;
    }

    for (int attempt = 0; attempt < 100 && callbacks->status_ != quicr::Session::Status::kReady; ++attempt) {
        if (callbacks->status_ == quicr::Session::Status::kNotConnected) {
            return 1;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    if (callbacks->status_ != quicr::Session::Status::kReady) {
        std::cerr << "MoQ SETUP did not complete\n";
        return 1;
    }

    std::cout << "Switch networks, then enter the new local IP address and optional interface index."
                 " Type quit to exit.\n";
    std::string line;
    while (std::getline(std::cin, line) && line != "quit") {
        std::istringstream input(line);
        std::string ip;
        unsigned int interface_index = 0;
        if (!(input >> ip)) {
            continue;
        }
        input >> interface_index;

        sockaddr_storage local{};
        auto* ipv4 = reinterpret_cast<sockaddr_in*>(&local);
        auto* ipv6 = reinterpret_cast<sockaddr_in6*>(&local);
        if (inet_pton(AF_INET, ip.c_str(), &ipv4->sin_addr) == 1) {
            ipv4->sin_family = AF_INET;
        } else if (inet_pton(AF_INET6, ip.c_str(), &ipv6->sin6_addr) == 1) {
            ipv6->sin6_family = AF_INET6;
        } else {
            std::cout << "Invalid IP address\n";
            continue;
        }

        if (!session->ProbeNetworkPath(local, interface_index)) {
            std::cout << "Probe could not be queued\n";
        } else {
            std::cout << "Probe queued; watch for 'QUIC path available' and the same connection ID.\n";
        }
    }
}
