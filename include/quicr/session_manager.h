// SPDX-FileCopyrightText: Copyright (c) 2026 Cisco Systems
// SPDX-License-Identifier: BSD-2-Clause

#include "quicr/config.h"

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <mutex>

namespace timeq {
    struct tick_service;
}

namespace spdlog {
    class logger;
}

namespace quicr {

    class Connection;
    class Session;
    class Transport;
    class TrackHandler;

    class SessionManager
    {
      public:
        using CreateClientSessionCallbackType =
          std::function<std::shared_ptr<Session>(const ClientConfig& cfg,
                                                 std::shared_ptr<Transport> transport,
                                                 std::shared_ptr<Connection> connection,
                                                 std::shared_ptr<timeq::tick_service> tick_service)>;
        using CreateServerSessionCallbackType =
          std::function<std::shared_ptr<Session>(const ServerConfig& cfg,
                                                 std::shared_ptr<Transport> transport,
                                                 std::shared_ptr<Connection> connection,
                                                 std::shared_ptr<timeq::tick_service> tick_service)>;

      public:
        SessionManager();

        SessionManager(std::shared_ptr<timeq::tick_service> tick_service);

        ~SessionManager();

        std::pair<std::weak_ptr<Transport>, std::weak_ptr<Session>> AddTransport(
          const ClientConfig& config,
          CreateClientSessionCallbackType&& create_session = nullptr);

        std::weak_ptr<Transport> AddTransport(
          const ServerConfig& config,
          CreateServerSessionCallbackType&& create_session,
          std::function<void(const std::shared_ptr<Session>&)>&& on_new_session = nullptr);

        void AddHandler(const std::shared_ptr<Session>& session, std::shared_ptr<TrackHandler> handler);

        void RemoveHandler(const std::shared_ptr<Session>& session, const std::shared_ptr<TrackHandler>& handler);

      private:
        std::shared_ptr<timeq::tick_service> tick_service_;

        std::shared_ptr<spdlog::logger> logger_;

        std::mutex mutex_;

        std::map<std::uint64_t, std::shared_ptr<Transport>> transports_;

        std::map<std::uint64_t, std::shared_ptr<Session>> sessions_;

        std::function<void(const std::shared_ptr<Connection>&)> on_connection_closed_;
    };

}
