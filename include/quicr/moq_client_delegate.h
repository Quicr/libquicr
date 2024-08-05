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
     * @brief MoQ/MOQT client callbacks
     *
     * @details MoQ client callback delegate for connection and MOQT control message handling.
     */
    class MoQClientDelegate
    {
      public:
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
         * @brief Callback on server setup message
         * @details Server will send sever setup in response to client setup message sent. This callback is called
         *  when a server setup has been received.
         *
         * @param conn_id          Transport connection ID
         * @param server_setup     Decoded sever setup message
         */
        virtual void cb_serverSetup([[maybe_unused]] TransportConnId conn_id,
                                    [[maybe_unused]] messages::MoqServerSetup server_setup)
        {
        }

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
        virtual void cb_unsubscribe([[maybe_unused]] TransportConnId conn_id, [[maybe_unused]] uint64_t subscribe_id) {}
    };

} // namespace quicr
