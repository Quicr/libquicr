#include "quicr/session_manager.h"

#include "quicr/handlers/fetch_track_handler.h"
#include "quicr/handlers/publish_fetch_handler.h"
#include "quicr/handlers/publish_namespace_handler.h"
#include "quicr/handlers/publish_track_handler.h"
#include "quicr/handlers/subscribe_namespace_handler.h"
#include "quicr/handlers/subscribe_track_handler.h"
#include "quicr/log.h"
#include "quicr/session.h"
#include "quicr/transport.h"

#include <timeq/tick_service.h>

namespace quicr {

    namespace {
        static std::optional<std::tuple<std::string, uint16_t, TransportProtocol, std::string>> ParseConnectUri(
          const std::string& connect_uri)
        {
            // Support moq://, moqt://, https:// (for WebTransport)
            std::string proto;
            TransportProtocol transport_proto = TransportProtocol::kQuic;

            const std::string moq_proto = "moq://";
            const std::string moqt_proto = "moqt://";
            const std::string https_proto = "https://";

            auto it = connect_uri.begin();

            if (auto moq_it = std::search(it, connect_uri.end(), moq_proto.begin(), moq_proto.end());
                moq_it != connect_uri.end()) {
                proto = moq_proto;
                transport_proto = TransportProtocol::kQuic;
                it = moq_it;
            } else if (auto moqt_it = std::search(it, connect_uri.end(), moqt_proto.begin(), moqt_proto.end());
                       moqt_it != connect_uri.end()) {
                proto = moqt_proto;
                transport_proto = TransportProtocol::kQuic;
                it = moqt_it;
            } else if (auto https_it = std::search(it, connect_uri.end(), https_proto.begin(), https_proto.end());
                       https_it != connect_uri.end()) {
                proto = https_proto;
                transport_proto = TransportProtocol::kWebTransport;
                it = https_it;
            } else {
                return std::nullopt;
            }

            // move to end of proto://
            std::advance(it, proto.length());

            std::string address_str;
            std::string path_str;
            uint16_t port = 0; // 0 indicates no port specified

            // Find first ':' (port) or '/' (path)
            auto colon_it = std::find(it, connect_uri.end(), ':');
            auto slash_it = std::find(it, connect_uri.end(), '/');

            // Determine where address ends
            auto address_end_it = std::min(colon_it, slash_it);

            // Parse address (everything before ':' or '/' or end)
            address_str.assign(it, address_end_it);

            if (address_str.empty()) {
                return std::nullopt;
            }

            it = address_end_it;

            // Parse port if present (: comes before /)
            if (it != connect_uri.end() && *it == ':') {
                ++it; // skip ':'

                // Find where port ends (at '/' or end of string)
                auto port_end_it = std::find(it, connect_uri.end(), '/');

                std::string port_str(it, port_end_it);
                if (!port_str.empty()) {
                    try {
                        port = static_cast<uint16_t>(std::stoi(port_str));
                    } catch (...) {
                        return std::nullopt; // Invalid port number
                    }
                }

                it = port_end_it;
            }

            // Parse path if present (starts with '/')
            if (it != connect_uri.end() && *it == '/') {
                path_str.assign(it, connect_uri.end()); // Include the leading '/'
            }

            return std::make_tuple(address_str, port, transport_proto, path_str);
        }
    }

    void SessionManager::Callbacks::OnNewServerSession(const std::shared_ptr<Session>&) {}

    void SessionManager::Callbacks::OnSessionRemoved(const std::shared_ptr<Session>&) {}

    SessionManager::SessionManager(std::shared_ptr<Logger> logger)
      : SessionManager(std::make_shared<Callbacks>(),
                       std::make_shared<timeq::threaded_tick_service>(),
                       std::move(logger))
    {
    }

    SessionManager::SessionManager(std::shared_ptr<Callbacks> callbacks, std::shared_ptr<Logger> logger)
      : SessionManager(std::move(callbacks), std::make_shared<timeq::threaded_tick_service>(), std::move(logger))
    {
    }

    SessionManager::SessionManager(std::shared_ptr<timeq::tick_service> tick_service, std::shared_ptr<Logger> logger)
      : SessionManager(std::make_shared<Callbacks>(), std::move(tick_service), std::move(logger))
    {
    }

    SessionManager::SessionManager(std::shared_ptr<Callbacks> callbacks,
                                   std::shared_ptr<timeq::tick_service> tick_service,
                                   std::shared_ptr<Logger> logger)
      : callbacks_(std::move(callbacks))
      , tick_service_(std::move(tick_service))
      , logger_(std::move(logger))
    {
        on_connection_closed_ = [this](const auto& connection) {
            connection->SetDelegate(nullptr);

            std::lock_guard<std::mutex> lock(mutex_);

            auto it = sessions_.find(connection->GetID());
            if (it == sessions_.end()) {
                QUICR_LOGGER_ERROR(logger_,
                                   "Received Close for connection that has no associated session (conn_id={})",
                                   connection->GetID());
                return;
            }

            callbacks_->OnSessionRemoved(it->second);

            sessions_.erase(it);
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
        auto parse_result = ParseConnectUri(config.connect_uri);
        if (!parse_result) {
            return {};
        }

        auto [address, port, protocol, path] = parse_result.value();
        relay.host_or_ip = address;
        relay.port = port;
        relay.proto = protocol;
        relay.path = path;

        auto transport = Transport::MakeClientTransport(relay, config.transport_config, tick_service_, logger_);

        transport->OnConnectionClosed = on_connection_closed_;

        std::unique_lock lock(mutex_);
        std::condition_variable cv;

        transport->OnNewConnection = [&](const auto& connection) {
            auto session = Session::Create(config, transport, connection, std::move(callbacks), tick_service_, logger_);
            connection->SetDelegate(session);

            {
                std::lock_guard _(mutex_);
                transports_.try_emplace(reinterpret_cast<std::uintptr_t>(transport.get()), transport);
                sessions_[connection->GetID()] = std::move(session);
            }

            cv.notify_all();
        };

        auto connection = transport->Start();
        if (!connection) {
            transport->Shutdown();
            return {};
        }

        if (!cv.wait_for(lock,
                         std::chrono::milliseconds(config.transport_config.idle_timeout_ms),
                         [this, id = connection->GetID()] { return sessions_.contains(id); })) {
            transport->Shutdown();
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
        // Note: server.proto is ignored in server mode - the server automatically
        // supports both raw QUIC (moq-00) and WebTransport (h3) simultaneously.
        // The transport mode is determined per-connection based on ALPN negotiation.
        // Any value can be set here; it won't affect server behavior.
        server.proto = TransportProtocol::kQuic; // Ignored by server
        server.path = "/relay";

        auto transport = Transport::MakeServerTransport(server, config.transport_config, tick_service_, logger_);

        transport->OnNewConnection =
          [=, this, wtransport = std::weak_ptr(transport), callbacks = std::move(callbacks)](const auto& connection) {
              auto transport = wtransport.lock();
              auto session = Session::Create(config, transport, connection, callbacks, tick_service_, logger_);
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
        } else if (auto h = std::dynamic_pointer_cast<SubscribeTrackHandler>(handler)) {
            session->SubscribeTrack(std::move(h));
        } else if (auto h = std::dynamic_pointer_cast<SubscribeNamespaceHandler>(handler)) {
            session->SubscribeNamespace(std::move(h));
        } else if (auto h = std::dynamic_pointer_cast<FetchTrackHandler>(handler)) {
            session->FetchTrack(std::move(h));
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
        } else if (auto h = std::dynamic_pointer_cast<SubscribeTrackHandler>(handler)) {
            session->UnsubscribeTrack(h);
        } else if (auto h = std::dynamic_pointer_cast<SubscribeNamespaceHandler>(handler)) {
            session->UnsubscribeNamespace(h);
        } else if (auto h = std::dynamic_pointer_cast<FetchTrackHandler>(handler)) {
            session->CancelFetchTrack(h);
        } else {
            throw std::invalid_argument("Unknown track handler type");
        }
    }

}
