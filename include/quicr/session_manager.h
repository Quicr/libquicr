// SPDX-FileCopyrightText: Copyright (c) 2026 Cisco Systems
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

#include "quicr/config.h"
#include "quicr/session.h"

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <mutex>

namespace timeq {
    struct tick_service;
}

namespace quicr {

    class Connection;
    class Transport;
    class TrackHandler;
    class Logger;

    class SessionManager
    {
      public:
        struct Callbacks
        {
            virtual ~Callbacks() = default;

            virtual void OnNewServerSession(const std::shared_ptr<Session>& new_session);

            virtual void OnSessionRemoved(const std::shared_ptr<Session>& session);
        };

      public:
        SessionManager(std::shared_ptr<Logger> logger = nullptr);

        SessionManager(std::shared_ptr<Callbacks> callbacks, std::shared_ptr<Logger> logger = nullptr);

        SessionManager(std::shared_ptr<timeq::tick_service> tick_service, std::shared_ptr<Logger> logger = nullptr);

        SessionManager(std::shared_ptr<Callbacks> callbacks,
                       std::shared_ptr<timeq::tick_service> tick_service,
                       std::shared_ptr<Logger> logger = nullptr);

        ~SessionManager();

        std::weak_ptr<Session> AddTransport(const ClientConfig& config,
                                            std::shared_ptr<Session::ClientCallbacks> callbacks = nullptr);

        void AddTransport(const ServerConfig& config, std::shared_ptr<Session::ServerCallbacks> callbacks = nullptr);

        void AddHandler(const std::shared_ptr<Session>& session, std::shared_ptr<TrackHandler> handler);

        void RemoveHandler(const std::shared_ptr<Session>& session, const std::shared_ptr<TrackHandler>& handler);

      private:
        std::shared_ptr<Callbacks> callbacks_;

        std::shared_ptr<timeq::tick_service> tick_service_;

        std::shared_ptr<Logger> logger_;

        std::mutex mutex_;

        std::map<std::uint64_t, std::shared_ptr<Transport>> transports_;

        std::map<std::uint64_t, std::shared_ptr<Session>> sessions_;

        std::function<void(const std::shared_ptr<Connection>&)> on_connection_closed_;
    };

}
