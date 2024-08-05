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
     * @brief MoQ/MOQT server callbacks
     *
     * @details MoQ server callback delegate for connection and MOQT control message handling.
     */

    class MoQServerDelegate
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
        virtual void cb_newConnection([[maybe_unused]] TransportConnId conn_id,
                                      [[maybe_unused]] std::span<uint8_t const> endpoint_id,
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
                                         std::span<uint8_t const> endpoint_id,
                                         TransportStatus status) = 0;

        /**
         * @brief Callback on client setup message
         * @detais In server mode, client will send a setup message on new connection.
         *         Server responds with server setup.
         *
         * @param conn_id          Transport connection ID
         * @param client_setup     Decoded client setup message
         */
        virtual void cb_clientSetup([[maybe_unused]] TransportConnId conn_id,
                                    [[maybe_unused]] messages::MoqClientSetup client_setup) {}

        /**
         * @brief Callback notification for new announce received (need to authorize)
         *
         * @param conn_id                   Source connection ID
         * @param track_namespace_hash      Track namespace hash
         *
         * @return True if send announce should be sent, false if not
         */
        virtual bool cb_announce([[maybe_unused]] TransportConnId conn_id,
                                 [[maybe_unused]] uint64_t track_namespace_hash) { return true; }

        /**
         * @brief Callback notification for new announce received post OK being sent
         *
         * @details After announce an OK is sent providing the announce was authorized and accepted. This
         *      callback is called after `cb_announce()` to allow the server to subscribe or followup
         *      with actions based on the announce.
         *
         * @param conn_id                   Source connection ID
         * @param track_namespace_hash      Track namespace hash
         */
        virtual void cb_announce_post([[maybe_unused]] TransportConnId conn_id,
                                      [[maybe_unused]] uint64_t track_namespace_hash) { }

        /**
         * @brief Callback notification for unannounce received
         *
         * @param conn_id                   Source connection ID
         * @param track_namespace_hash      Track namespace hash
         * @param track_name_hash           Track name is present if subscribe DONE was received
         *                                  Otherwise, it will be nullopt for received UNANNOUNCE
         */
        virtual void cb_unannounce([[maybe_unused]] TransportConnId conn_id,
                                   [[maybe_unused]] uint64_t track_namespace_hash,
                                   [[maybe_unused]] std::optional<uint64_t> track_name_hash) { }
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
         * @brief Callback notification on unsubscribe received
         *
         * @param conn_id             Source connection ID
         * @param subscribe_id        Subscribe ID received
         */
        virtual void cb_unsubscribe([[maybe_unused]] TransportConnId conn_id,
                                    [[maybe_unused]] uint64_t subscribe_id) {}

    };

} // namespace quicr
