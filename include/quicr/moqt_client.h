/*
 *  Copyright (C) 2024
 *  Cisco Systems, Inc.
 *  All Rights Reserved
 */

#pragma once

#include <quicr/moqt_core.h>

namespace moq {
    using namespace qtransport;

    /**
     * @brief MOQT Client
     *
     * @details MOQT Client is the handler of the MOQT QUIC transport IP connection.
     */
    class Client : public Core
    {
      public:
        /**
         * @brief MoQ Client Constructor to create the client mode instance
         *
         * @param cfg           MOQT Client Configuration
         * @param logger        MOQT Log pointer to parent logger
         */
        Client(const ClientConfig& cfg, const cantina::LoggerPointer& logger)
          : Core(cfg, logger)
        {
        }

        ~Client() = default;

        /**
         * @brief Starts a client connection via a transport thread (non-blocking)
         *
         * @details Makes a client connection session and runs in a newly created thread. All control and track
         *   callbacks will be run based on events.
         *
         * @return Status indicating state or error. If successful, status will be
         *    CLIENT_CONNECTING.
         */
        Status connect();

        /**
         * @brief Disconnect the client connection gracefully (blocking)
         *
         * @details Unsubscribes and unpublishes all remaining active ones, sends MOQT control messages
         *   for those and then closes the QUIC connection gracefully. Stops the transport thread. The class
         *   destructor calls this method as well. Status will be updated to reflect not connected.
         */
        void disconnect();

        /**
         * @brief Callback notification for connection status/state change
         * @details Callback notification indicates state change of connection, such as disconnected
         *
         * @param conn_id          Transport connection ID
         * @param status           Transport status of connection id
         */
        virtual void connectionChanged(TransportConnId conn_id, TransportStatus status) = 0;

        /**
         * @brief Callback on server setup message
         * @details Server will send sever setup in response to client setup message sent. This callback is called
         *  when a server setup has been received.
         *
         * @param conn_id          Transport connection ID
         * @param server_setup     Decoded sever setup message
         */
        virtual void serverSetup([[maybe_unused]] TransportConnId conn_id,
                                 [[maybe_unused]] transport::MoqServerSetup server_setup) = 0;

    };

} // namespace quicr
