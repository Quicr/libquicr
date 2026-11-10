// SPDX-FileCopyrightText: Copyright (c) 2026 Cisco Systems
// SPDX-License-Identifier: BSD-2-Clause

#include "quicr/session_manager.h"

#include "connect_uri.h"
#include "quicr/clock.h"
#include "quicr/handlers/fetch_track_handler.h"
#include "quicr/handlers/publish_fetch_handler.h"
#include "quicr/handlers/publish_namespace_handler.h"
#include "quicr/handlers/publish_track_handler.h"
#include "quicr/handlers/subscribe_namespace_handler.h"
#include "quicr/handlers/subscribe_track_handler.h"
#include "quicr/log.h"
#include "quicr/session.h"
#include "quicr/transport.h"

#include <algorithm>

namespace quicr {

    void SessionManager::Callbacks::OnNewServerSession(const std::shared_ptr<Session>&) {}

    void SessionManager::Callbacks::OnSessionRemoved(const std::shared_ptr<Session>&) {}

    SessionManager::SessionManager(std::shared_ptr<Logger> logger)
      : SessionManager(std::make_shared<Callbacks>(), nullptr, std::move(logger))
    {
    }

    SessionManager::SessionManager(std::shared_ptr<Callbacks> callbacks, std::shared_ptr<Logger> logger)
      : SessionManager(std::move(callbacks), nullptr, std::move(logger))
    {
    }

    SessionManager::SessionManager(std::shared_ptr<Clock> clock, std::shared_ptr<Logger> logger)
      : SessionManager(std::make_shared<Callbacks>(), std::move(clock), std::move(logger))
    {
    }

    SessionManager::SessionManager(std::shared_ptr<Callbacks> callbacks,
                                   std::shared_ptr<Clock> clock,
                                   std::shared_ptr<Logger> logger)
      : callbacks_(std::move(callbacks))
      , clock_(std::move(clock))
      , logger_(std::move(logger))
    {
        on_connection_closed_ = [this](const auto& connection) {
            connection->SetDelegate(nullptr);

            std::shared_ptr<Session> removed_session;
            bool session_has_other_connections = false;
            {
                std::lock_guard<std::mutex> lock(mutex_);

                auto it = sessions_.find(connection->GetID());
                if (it == sessions_.end()) {
                    QUICR_LOGGER_DEBUG(logger_,
                                       "Received Close for connection that has no associated session (conn_id={})",
                                       connection->GetID());
                    return;
                }

                removed_session = it->second;
                sessions_.erase(it);
                session_has_other_connections =
                  std::ranges::any_of(sessions_, [&](const auto& item) { return item.second == removed_session; });
            }

            if (!session_has_other_connections) {
                callbacks_->OnSessionRemoved(removed_session);
            }
        };
    }

    SessionManager::~SessionManager()
    {
        for (const auto& [_, transport] : transports_) {
            transport->Shutdown();
        }
    }

    std::weak_ptr<Session> SessionManager::AddTransport(const ClientConfig& config,
                                                        std::shared_ptr<Session::ClientCallbacks> callbacks)
    {
        TransportRemote relay;
        auto parse_result = detail::ParseConnectUri(config.connect_uri);
        if (!parse_result) {
            return {};
        }

        auto [address, port, protocol, path] = parse_result.value();
        relay.host_or_ip = address;
        relay.port = port;
        if (protocol == Connection::Protocol::kWebTransport) {
            relay.web_transport_options = WebTransportOptions{ .path = std::move(path) };
        }

        auto clock = clock_ ? clock_ : Clock::Create(config.tick_service_sleep_delay_us);
        auto transport = Transport::MakeClientTransport(relay, config.transport_config, clock, logger_);

        transport->OnConnectionClosed = on_connection_closed_;

        std::unique_lock lock(mutex_);
        std::condition_variable cv;
        bool cancelled = false;

        transport->OnNewConnection = [&, clock = std::move(clock)](const auto& connection) {
            auto session = Session::Create(config, transport, connection, std::move(callbacks), clock, logger_);
            session->SetConnectionAddedCallback([this](const auto& added_connection, const auto& owning_session) {
                std::lock_guard lock(mutex_);
                sessions_[added_connection->GetID()] = owning_session;
            });
            connection->SetDelegate(session);

            {
                std::lock_guard _(mutex_);
                if (cancelled) {
                    connection->SetDelegate(nullptr);
                    return;
                }
                transports_.try_emplace(reinterpret_cast<std::uintptr_t>(transport.get()), transport);
                sessions_[connection->GetID()] = std::move(session);
                cv.notify_all();
            }
        };

        auto connection = transport->Start();
        if (!connection || !cv.wait_for(lock,
                                        std::chrono::milliseconds(config.transport_config.idle_timeout_ms),
                                        [this, id = connection->GetID()] { return sessions_.contains(id); })) {
            cancelled = true;
            lock.unlock();
            transport->Shutdown();
            lock.lock();
            transports_.erase(reinterpret_cast<std::uintptr_t>(transport.get()));
            return {};
        }

        transport->OnNewConnection = nullptr;

        return sessions_.at(connection->GetID());
    }

    void SessionManager::AddTransport(const ServerConfig& config, std::shared_ptr<Session::ServerCallbacks> callbacks)
    {
        TransportRemote server;
        server.host_or_ip = config.server_bind_ip;
        server.port = config.server_port;
        server.web_transport_options = WebTransportOptions{ .path = "/relay" };

        auto clock = clock_ ? clock_ : Clock::Create(config.tick_service_sleep_delay_us);
        auto transport = Transport::MakeServerTransport(server, config.transport_config, clock, logger_);

        transport->OnNewConnection =
          [=, this, wtransport = std::weak_ptr(transport), callbacks = std::move(callbacks), clock = std::move(clock)](
            const auto& connection) {
              auto transport = wtransport.lock();
              auto session = Session::Create(config, transport, connection, callbacks, clock, logger_);
              session->SetConnectionAddedCallback([this](const auto& added_connection, const auto& owning_session) {
                  std::lock_guard lock(mutex_);
                  sessions_[added_connection->GetID()] = owning_session;
              });
              connection->SetDelegate(session);

              {
                  std::lock_guard<std::mutex> lock(mutex_);
                  sessions_[connection->GetID()] = session;
              }

              callbacks_->OnNewServerSession(session);
          };

        transport->OnConnectionClosed = on_connection_closed_;

        std::shared_ptr<Transport> transport_ptr;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            auto [transport_it, _] =
              transports_.try_emplace(reinterpret_cast<std::uintptr_t>(transport.get()), transport);
            transport_ptr = transport_it->second;
        }

        transport->Start();
    }

    void SessionManager::AddHandler(const std::shared_ptr<Session>& session, std::shared_ptr<TrackHandler> handler)
    {
        if (!session) {
            throw std::invalid_argument("Session cannot be null");
        }

        if (auto h = std::dynamic_pointer_cast<PublishTrackHandler>(handler)) {
            session->PublishTrack(std::move(h));
        } else if (auto h = std::dynamic_pointer_cast<PublishNamespaceHandler>(handler)) {
            session->PublishNamespace(std::move(h));
        } else if (auto h = std::dynamic_pointer_cast<FetchTrackHandler>(handler)) {
            session->FetchTrack(std::move(h));
        } else if (auto h = std::dynamic_pointer_cast<SubscribeTrackHandler>(handler)) {
            session->SubscribeTrack(std::move(h));
        } else if (auto h = std::dynamic_pointer_cast<SubscribeNamespaceHandler>(handler)) {
            session->SubscribeNamespace(std::move(h));
        } else {
            throw std::invalid_argument("Unknown track handler type");
        }
    }

    void SessionManager::RemoveHandler(const std::shared_ptr<Session>& session,
                                       const std::shared_ptr<TrackHandler>& handler)
    {
        if (!session) {
            throw std::invalid_argument("Session cannot be null");
        }

        if (auto h = std::dynamic_pointer_cast<PublishTrackHandler>(handler)) {
            session->UnpublishTrack(h);
        } else if (auto h = std::dynamic_pointer_cast<PublishNamespaceHandler>(handler)) {
            session->PublishNamespaceDone(h);
        } else if (auto h = std::dynamic_pointer_cast<FetchTrackHandler>(handler)) {
            session->CancelFetchTrack(h);
        } else if (auto h = std::dynamic_pointer_cast<SubscribeTrackHandler>(handler)) {
            session->UnsubscribeTrack(h);
        } else if (auto h = std::dynamic_pointer_cast<SubscribeNamespaceHandler>(handler)) {
            session->UnsubscribeNamespace(h);
        } else {
            throw std::invalid_argument("Unknown track handler type");
        }
    }

}
