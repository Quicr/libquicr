/*
 *  Copyright (C) 2024
 *  Cisco Systems, Inc.
 *  All Rights Reserved
 */

#pragma once

#include <quicr/moqt_messages.h>
#include <quicr/moqt_core.h>

namespace quicr {
    using namespace qtransport;

    /**
     * @brief MOQT Server
     *
     * @details MOQT Server is the handler of the MOQT QUIC listening socket
     */
    class MOQTServer : public MOQTCore
    {
      public:
        /**
         * @brief MoQ Server constructor to create the MOQ server mode instance
         *
         * @param cfg           MOQT Server Configuration
         * @param logger        MOQT Log pointer to parent logger
         */
        MOQTServer(const MOQTServerConfig& cfg,
                   const cantina::LoggerPointer& logger)
          : MOQTCore(cfg, logger)
        {
        }

        ~MOQTServer() = default;

        /**
         * @brief Runs server transport thread to listen for new connections
         *
         * @details Creates a new transport thread to listen for new connections. All control and track
         *   callbacks will be run based on events.
         *
         * @return Status indicating state or error. If successful, status will be
         *    READY.
         */
        Status run();


        /**
         * @brief Callback notification on new connection
         * @details Callback notification that a new connection has been accepted
         *
         * @param conn_id          Transport connection ID
         * @param remote           Transport remote connection information
         */
        virtual void newConnection([[maybe_unused]] TransportConnId conn_id,
                                   [[maybe_unused]] const TransportRemote& remote) = 0;

        /**
         * @brief Callback notification for connection status/state change
         * @details Callback notification indicates state change of connection, such as disconnected
         *
         * @param conn_id          Transport connection ID
         * @param status           Transport status of connection id
         */
        virtual void connectionChanged(TransportConnId conn_id,
                                      TransportStatus status) = 0;

        /**
         * @brief Callback on client setup message
         * @details In server mode, client will send a setup message on new connection.
         *         Server responds with server setup.
         *
         * @param conn_id          Transport connection ID
         * @param client_setup     Decoded client setup message
         */
        virtual void clientSetupReceived([[maybe_unused]] TransportConnId conn_id,
                                         [[maybe_unused]] messages::MoqClientSetup client_setup) = 0;

        /**
         * @brief Callback notification for new announce received that needs to be authorized
         *
         * @param conn_id                   Source connection ID
         * @param track_namespace           Track namespace
         *
         * @return True if authorized and announce OK will be sent, false if not
         */
        virtual bool announceReceived([[maybe_unused]] TransportConnId conn_id,
                                      [[maybe_unused]] const std::vector<uint8_t>& track_namespace) = 0;

        /**
         * @brief Callback notification for unannounce received
         *
         * @param conn_id                   Source connection ID
         * @param track_namespace           Track namespace
         *
         */
        virtual void unannounce([[maybe_unused]] TransportConnId conn_id,
                                [[maybe_unused]] const std::vector<uint8_t>& track_namespace) = 0;

        /**
         * @brief Callback notification for new subscribe received
         *
         * @param conn_id             Source connection ID
         * @param subscribe_id        Subscribe ID received
         * @param track_namespace     Track Namespace from subscription
         * @param track_name          Track name from subscription
         *
         * @return True if send announce should be sent, false if not
         */
        virtual bool subscribe([[maybe_unused]] TransportConnId conn_id,
                               [[maybe_unused]] uint64_t subscribe_id,
                               [[maybe_unused]] const std::vector<uint8_t>& track_namespace,
                               [[maybe_unused]] const std::vector<uint8_t>& track_name) = 0;

        /**
         * @brief Callback notification on unsubscribe received
         *
         * @param conn_id             Source connection ID
         * @param subscribe_id        Subscribe ID received
         */
        virtual void unsubscribe([[maybe_unused]] TransportConnId conn_id, [[maybe_unused]] uint64_t subscribe_id) = 0;

    };

} // namespace quicr
