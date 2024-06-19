/*
 *  Copyright (C) 2024
 *  Cisco Systems, Inc.
 *  All Rights Reserved
 */

#pragma once

#include <quicr/moq_messages.h>
#include <transport/transport.h>

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
         * @brief Server Mode; Notification on new connection
         * @details Notification that a new connection has been accepted
         *
         * @param conn_id          Transport connection ID
         * @param endpoint_id      Endpoint ID of client connection
         * @param remote           Transport remote connection information
         */
        virtual void cb_newConnection([[maybe_unused]] TransportConnId conn_id,
                                      [[maybe_unused]] const std::span<uint8_t>& endpoint_id,
                                      [[maybe_unused]] const TransportRemote& remote) {};

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
         * @brief Server Mode; callback on client setup message
         * @detais In server mode, client will send a setup message on new connection.
         *         Server responds with server setup.
         *
         * @param conn_id          Transport connection ID
         * @param client_setup     Decoded client setup message
         */
        virtual void cb_clientSetup([[maybe_unused]] TransportConnId conn_id,
                                    [[maybe_unused]] messages::MoqClientSetup client_setup) {}

        /**
         * @brief Client Mode; callback on server setup message
         * @details In client mode, server will send sever setup in response to client setup message sent.
         *
         * @param conn_id          Transport connection ID
         * @param server_setup     Decoded sever setup message
         */
        virtual void cb_serverSetup([[maybe_unused]] TransportConnId conn_id,
                                    [[maybe_unused]] messages::MoqServerSetup server_setup) {}

        /**
         * @brief Server Mode; Callback notification for new announce received
         *
         * @param conn_id                   Source connection ID
         * @param track_namespace_hash      Track namespace hash
         *
         * @return True if send announce should be sent, false if not
         */
        virtual bool cb_announce([[maybe_unused]] TransportConnId conn_id,
                                 [[maybe_unused]] uint64_t track_namespace_hash) { return true; }

        /**
         * @brief Callback notification for new subscribe received
         *
         * @param conn_id             Source connection ID
         * @param subscribe_id        Subscribe ID received
         * @param name_space          Track Namespace from subscription
         * @param name                Track name from subscription
         *
         * @return True if send announce should be sent, false if not
         */
        virtual bool cb_subscribe([[maybe_unused]] TransportConnId conn_id,
                                  [[maybe_unused]] uint64_t subscribe_id,
                                  [[maybe_unused]] std::span<uint8_t const> name_space,
                                  [[maybe_unused]] std::span<uint8_t const> name)
        {
            return true;
        }

        /**
         * @brief Server Mode; Callback notification on new object received to be relayed
         *
         * @param conn_id              Source connection Id
         * @param subscribe_id         Subscribe ID received
         * @param track_alias          Track alias received
         * @param group_id             Group ID received
         * @param object_id            Object ID received
         * @param data                 Data received
         */
        virtual void cb_objectReceived([[maybe_unused]] TransportConnId conn_id,
                                       [[maybe_unused]] uint64_t subscribe_id,
                                       [[maybe_unused]] uint64_t track_alias,
                                       [[maybe_unused]] uint64_t group_id,
                                       [[maybe_unused]] uint64_t object_id,
                                       [[maybe_unused]] std::vector<uint8_t>&& data) {}

    };

} // namespace quicr
