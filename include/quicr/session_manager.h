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

    /**
     * @brief Owns transports and sessions and routes generic track handlers to a session.
     *
     * @details The manager retains every server session until its connection closes. A client session is
     *      likewise retained internally; callers receive a weak reference. Destruction shuts down all transports.
     */
    class SessionManager
    {
      public:
        /**
         * @brief Notifications for sessions owned by a manager.
         *
         * @details Invoked on the corresponding transport's notification thread. Implementations must return
         *      promptly. `OnSessionRemoved` is called while the manager serializes its session state and must not
         *      re-enter the same manager.
         */
        struct Callbacks
        {
            /// @brief Destroy the callback interface.
            virtual ~Callbacks() = default;

            /// @brief Notify that a server accepted and created a session.
            /// @param new_session Session retained by the manager for the lifetime of its connection.
            virtual void OnNewServerSession(const std::shared_ptr<Session>& new_session);

            /// @brief Notify immediately before a closed session is removed from the manager.
            /// @param session Session being removed; the callback may retain it if needed.
            virtual void OnSessionRemoved(const std::shared_ptr<Session>& session);
        };

      public:
        /// @brief Construct with default callbacks and an internally created tick service per transport.
        /// @param logger Optional logger shared by created transports and sessions.
        SessionManager(std::shared_ptr<Logger> logger = nullptr);

        /// @brief Construct with callbacks and an internally created tick service per transport.
        /// @param callbacks Callback receiver.
        /// @param logger Optional logger shared by created transports and sessions.
        /// @pre @p callbacks must not be null.
        SessionManager(std::shared_ptr<Callbacks> callbacks, std::shared_ptr<Logger> logger = nullptr);

        /// @brief Construct with default callbacks and a shared tick service.
        /// @param tick_service Service shared by created transports; null selects an internal service per transport.
        /// @param logger Optional logger shared by created transports and sessions.
        SessionManager(std::shared_ptr<timeq::tick_service> tick_service, std::shared_ptr<Logger> logger = nullptr);

        /// @brief Construct with callbacks and a shared tick service.
        /// @param callbacks Callback receiver.
        /// @param tick_service Service shared by created transports; null selects an internal service per transport.
        /// @param logger Optional logger shared by created transports and sessions.
        /// @pre @p callbacks must not be null.
        SessionManager(std::shared_ptr<Callbacks> callbacks,
                       std::shared_ptr<timeq::tick_service> tick_service,
                       std::shared_ptr<Logger> logger = nullptr);

        /// @brief Shut down all transports owned by the manager.
        ~SessionManager();

        /**
         * @brief Create a client transport and wait for its session to be established.
         *
         * @param config Client and transport configuration.
         * @param callbacks Session callbacks; null uses no application callbacks.
         * @return Weak reference to the manager-owned session, or an empty reference if the URI is invalid,
         *      transport startup fails, or no session appears before the configured idle timeout.
         */
        std::weak_ptr<Session> AddTransport(const ClientConfig& config,
                                            std::shared_ptr<Session::ClientCallbacks> callbacks = nullptr);

        /**
         * @brief Create and start a server transport.
         * @param config Server and transport configuration.
         * @param callbacks Callbacks shared by sessions accepted by this transport; null uses no session callbacks.
         *
         * @note Accepted sessions are reported asynchronously through `Callbacks::OnNewServerSession`.
         */
        void AddTransport(const ServerConfig& config, std::shared_ptr<Session::ServerCallbacks> callbacks = nullptr);

        /**
         * @brief Register a concrete track or namespace handler with a session.
         * @param session Target session; the manager does not take additional ownership.
         * @param handler Handler whose dynamic type selects the session operation.
         * @throws std::invalid_argument if @p session is null or @p handler has an unsupported dynamic type.
         */
        void AddHandler(const std::shared_ptr<Session>& session, std::shared_ptr<TrackHandler> handler);

        /**
         * @brief Remove a concrete track or namespace handler from a session.
         * @param session Target session.
         * @param handler Previously registered handler whose dynamic type selects the removal operation.
         * @throws std::invalid_argument if @p session is null or @p handler has an unsupported dynamic type.
         */
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
