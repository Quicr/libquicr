/*
 *  Copyright (C) 2024
 *  Cisco Systems, Inc.
 *  All Rights Reserved
 */

#pragma once

#include <transport/transport.h>
#include <quicr/moq_messages.h>

namespace quicr {
    using namespace qtransport;

    /**
     * @brief MOQ/MOQT callbacks
     *
     * @details MOQ instance is created and this delegate is passed to the instanct constructor.
     *    MOQ instance callbacks are defined in this delegate
     */
    class MoQInstanceDelegate
    {
    public:
        /**
         * @brief Notification on new connection
         * @details Notification that a new connection has been accepted
         *
         * @param conn_id          Transport connection ID
         * @param endpoint_id      Endpoint ID of client connection
         * @param remote           Transport remote connection information
         */
        virtual void cb_newConnection(TransportConnId conn_id,
                                      const std::span<uint8_t>& endpoint_id,
                                      const TransportRemote& remote) = 0;

        /**
         * @brief Notification for connection status/state change
         * @details Notification indicates state change of connection, such as disconnected
         *
         * @param conn_id          Transport connection ID
         * @param endpoint_id      Endpoint ID of remote side
         * @param status           Transport status of connection id
         */
        virtual void cb_connectionStatus(TransportConnId conn_id,
                                         const std::span<uint8_t>& endpoint_id,
                                         TransportStatus status) = 0;

        /**
         * @brief callback on client setup message
         * @detais In server mode, client will send a setup message on new connection.
         *         Server responds with server setup.
         *
         * @param conn_id          Transport connection ID
         * @param client_setup     Decoded client setup message
         */
        virtual void cb_clientSetup(TransportConnId conn_id, messages::MoqClientSetup client_setup) = 0;

        /**
         * @brief callback on server setup message
         * @details In client mode, server will send sever setup in response to client setup message sent.
         *
         * @param conn_id          Transport connection ID
         * @param server_setup     Decoded sever setup message
         */
        virtual void cb_serverSetup(TransportConnId conn_id, messages::MoqServerSetup server_setup) = 0;
    };

} // namespace quicr
