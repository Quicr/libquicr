/*
 *  Copyright (C) 2024
 *  Cisco Systems, Inc.
 *  All Rights Reserved
 */

#pragma once

#include <moq/config.h>
#include <moq/detail/messages.h>
#include <moq/track_name.h>
#include <moq/object.h>
#include <moq/detail/transport.h>

namespace moq {
    using namespace qtransport;

    /**
     * @brief MoQ Server
     *
     * @details MoQ Server is the handler of the MoQ QUIC listening socket
     */
    class Server : public Transport
    {
      public:
        /**
         * @brief Connection status codes
         */
        enum class ConnectionStatus : uint8_t
        {
            kNotConnected = 0,
            kConnected,
            kIdleTimeout,
            kClosedByRemote
        };

        /**
         * @brief
         */
        struct ClientSetupResponse {

        };


        struct ErrorReason {
            enum class ReasonCode: uint8_t {
                kInternalError = 0,
                kInvalidRange,
                kRetryTrackAlias,
            };
            ReasonCode reason_code;
            std::optional<Bytes> reason_phrase;
        };

        struct SubscribeResponse {
            bool status; ///< True indicates successful response, false implies error as indicated by reason_code
            std::optional<ErrorReason> error_reason;  ///< Set only when status is false.
            std::optional<uint64_t> track_alias; ///< Set only when ResponseCode is kkRetryTrackAlias
        };

        struct AnnounceResponse {
            bool status; ///< True indicates successful response, false implies error as indicated by reason_code
            std::optional<ErrorReason> error_reason;  ///< Set only when status is false.
        };

        /**
         * @brief Connection remote information
         */
        struct ConnectionRemoteInfo
        {
            std::string ip;         ///< remote IPv4/v6 address
            uint16_t port;          ///< remote port
        };

        /**
         * @brief MoQ Server constructor to create the MOQ server mode instance
         *
         * @param cfg           MoQ Server Configuration
         */
        Server(const ServerConfig& cfg)
          : Transport(cfg)
        {
        }

        ~Server() = default;

        /**
         * @brief Starts server transport thread to listen for new connections
         *
         * @details Creates a new transport thread to listen for new connections. All control and track
         *   callbacks will be run based on events.
         *
         * @return Status indicating state or error. If successful, status will be
         *    READY.
         */
        Status Start();

        /**
         * Stop the server transport
         */
        void Stop() { stop_ = true; }

        /**
         * @brief Callback notification on new connection
         * @details Callback notification that a new connection has been accepted
         *
         * @param connection_handle          Transport connection ID
         * @param remote           Transport remote connection information
         */
        virtual void NewConnectionCreated(ConnectionHandle connection_handle,
                                            const ConnectionRemoteInfo& remote) = 0;

        /**
         * @brief Callback notification for connection status/state change
         * @details Callback notification indicates state change of connection, such as disconnected
         *
         * @param connection_handle          Transport connection ID
         * @param status           ConnectionStatus of connection id
         */
        virtual void ConnectionStatusChanged(ConnectionHandle connection_handle, ConnectionStatus status) = 0;

        /**
         * @brief Callback on client setup message
         * @details In server mode, client will send a setup message on new connection.
         *         Server responds with server setup.
         *
         * @param connection_handle                       Transport connection ID
         * @param client_setup_attributes       Decoded client setup message
         *
         * @return ClientSetupResponse indicating the status of processing the setup message.
         */
        virtual ClientSetupResponse ClientSetupReceived(ConnectionHandle connection_handle,
                                                        const ClientSetupAttributes& client_setup_attributes) = 0;

        /**
         * @brief Callback notification for new announce received that needs to be authorized
         *
         * @param connection_handle                       Source connection ID
         * @param track_namespace               Track namespace
         * @param publish_announce_attributes   Publish announce attributes received
         *
         * @return If AnnounceResponse::status is true, ANNOUNNCE_OK MOQT message is sent
         *         else ANNNOUNCE_ERROR MOQT messsage is sent with appropriate error reason
         *         provided in AnnounceResponse::error_reason.
         */
        virtual AnnounceResponse AnnounceReceived(ConnectionHandle connection_handle,
                                                  const TrackNamespace& track_namespace,
                                                  const PublishAnnounceAttributes& publish_announce_attributes) = 0;

        /**
         * @brief Callback notification for unannounce received
         *
         * @param connection_handle                   Source connection ID
         * @param track_namespace           Track namespace
         *
         */
        virtual void UnannounceReceived(ConnectionHandle connection_handle,
                                        const TrackNamespace& track_namespace) = 0;

        /**
         * @brief Callback notification for new subscribe received
         *
         * @param connection_handle               Source connection ID
         * @param subscribe_id          Subscribe ID received
         * @param proposed_track_alias  The proposed track alias the subscriber would like to use
         * @param track_full_name       Track full name
         * @param subscribe_attributes  Subscribe attributes received
         *
         *
         * @return If SubcribeResponse::status is true, SUBSCRIBE_OK MOQT message is sent
*                   else SUBSCRIBE_ERROR MOQT messsage is sent with appropriate error reason
*                   provided in SubscribeResponse::error_reason.
         */
        virtual SubscribeResponse SubscribeReceived(ConnectionHandle connection_handle,
                                                    uint64_t subscribe_id,
                                                    uint64_t proposed_track_alias,
                                                    const FullTrackName& track_full_name,
                                                    const SubscribeAttributes& subscribe_attributes) = 0;

        /**
         * @brief Callback notification on unsubscribe received
         *
         * @param connection_handle             Source connection ID
         * @param subscribe_id        Subscribe ID received
         */
        virtual void UnsubscribeReceived(ConnectionHandle connection_handle,
                                         uint64_t subscribe_id) = 0;

        /**
         * @brief Notification callback to provide sampled metrics
         *
         * @details Callback will be triggered on Config::metrics_sample_ms to provide the sampled data based
         *      on the sample period.  After this callback, the period/sample based metrics will reset and start over
         *      for the new period.
         *
         * @param connection_handle           Source connection ID
         * @param metrics           Copy of the connection metrics for the sample period
         */
        virtual void MetricsSampled(ConnectionHandle connection_handle, const ConnectionMetrics&& metrics)  = 0;

      protected:
        /**
         * @brief Bind a server publish track handler based on a subscribe
         *
         * @details The server will create a server publish track handler based on a received
         *      subscribe. It will use this handler to send objects to subscriber.
         *
         * @param connection_handle                   Connection ID of the client/subscriber
         * @param subscribe_id              Subscribe ID from the received subscribe
         * @param track_handler             Server publish track handler
         */
        void BindPublisherTrack(ConnectionHandle connection_handle,
                                uint64_t subscribe_id,
                                std::shared_ptr<ServerPublishTrackHandler> track_handler);


      private:
        bool stop_{ false };
    };

} // namespace moq
