/*
 *  Copyright (C) 2024
 *  Cisco Systems, Inc.
 *  All Rights Reserved
 */

#pragma once

#include <moqt/config.h>
#include <moqt/core/transport.h>

namespace moq::transport {
    using namespace qtransport;

    /**
     * @brief MoQT Client
     *
     * @details MoQT Client is the handler of the MoQT QUIC transport IP connection.
     */
    class Client : public Transport
    {
      public:
        /**
         * @brief MoQ Client Constructor to create the client mode instance
         *
         * @param cfg           MoQT Client Configuration
         */
        Client(const ClientConfig& cfg)
          : Transport(cfg)
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
         *    kClientConnecting.
         */
        Status Connect();

        /**
         * @brief Disconnect the client connection gracefully (blocking)
         *
         * @details Unsubscribes and unpublishes all remaining active ones, sends MoQT control messages
         *   for those and then closes the QUIC connection gracefully. Stops the transport thread. The class
         *   destructor calls this method as well. Status will be updated to reflect not connected.
         */
        void Disconnect();

        /**
         * @brief Callback notification for connection status/state change
         * @details Callback notification indicates state change of connection, such as disconnected
         *
         * @param status           Transport status of connection id
         */
        virtual void ConnectionChanged(TransportStatus status) = 0;

        /**
         * @brief Callback on server setup message
         * @details Server will send sever setup in response to client setup message sent. This callback is called
         *  when a server setup has been received.
         *
         * @param server_setup     Decoded sever setup message
         */
        virtual void ServerSetup(const messages::MoqServerSetup& server_setup) = 0;

        /**
         * @brief Notification callback to provide sampled metrics
         *
         * @details Callback will be triggered on Config::metrics_sample_ms to provide the sampled data based
         *      on the sample period.  After this callback, the period/sample based metrics will reset and start over
         *      for the new period.
         *
         * @param metrics           Copy of the connection metrics for the sample period
         */
        virtual void MetricsSampled(const ConnectionMetrics&& metrics)  = 0;

    };

} // namespace moq::transport
