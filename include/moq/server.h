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
         * @brief Response to received MOQT ClientSetup message
         */
        struct ClientSetupResponse {};


        /**
         * @brief Response to received MOQT Announce message
         */
        struct AnnounceResponse {
            /**
             * @details **kOK** indicates that the announce is accepted and OK should be sent. Any other
             *       value indicates that the announce is not accepted and the reason code and other
             *       fields will be set.
             */
            enum class ReasonCode: uint8_t {
                kOk = 0,
                kInternalError
            };
            ReasonCode reason_code;

            std::optional<Bytes> reason_phrase;
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
        void Stop();

        /**
         * @brief Callback notification on new connection
         * @details Callback notification that a new connection has been accepted
         *
         * @param connection_handle          Transport connection ID
         * @param remote                     Transport remote connection information
         */
         void NewConnectionAccepted(ConnectionHandle connection_handle, const ConnectionRemoteInfo& remote);

        /**
         * @brief Callback notification for connection status/state change
         * @details Callback notification indicates state change of connection, such as disconnected
         *
         * @param connection_handle          Transport connection ID
         * @param status                     ConnectionStatus of connection id
         */
         void ConnectionStatusChanged(ConnectionHandle connection_handle, ConnectionStatus status);

        /**
         * @brief Callback on client setup message
         * @details In server mode, client will send a setup message on new connection.
         *         Server responds with server setup.
         *
         * @param connection_handle                    Transport connection ID
         * @param client_setup_attributes              Decoded client setup message
         *
         * @return ClientSetupResponse indicating the status of processing the setup message.
         */
        virtual ClientSetupResponse ClientSetupReceived(ConnectionHandle connection_handle,
                                                        const ClientSetupAttributes& client_setup_attributes) = 0;

        /**
         * @brief Callback notification for new announce received that needs to be authorized
         *
         * @note The caller **MUST** respond to this via ResolveAnnounce(). If the caller does not
         * override this method, the default will call ResolveAnnounce() with the status of OK
         *
         * @param connection_handle             Source connection ID
         * @param track_namespace               Track namespace
         * @param publish_announce_attributes   Publish announce attributes received
         */
        virtual void AnnounceReceived(ConnectionHandle connection_handle,
                                      const TrackNamespace& track_namespace,
                                      const PublishAnnounceAttributes& publish_announce_attributes);

        /**
         * @brief Accept or reject an announce that was received
         *
         * @details Accept or reject an announce received via AnnounceReceived(). The MoQ Transport
         *      will send the protocol message based on the AnnounceResponse
         *
         * @param connection_handle        source connection ID
         * @param track_namespace          track namespace
         * @param announce_response        response to for the announcement
         */
        void ResolveAnnounce(ConnectionHandle connection_handle,
                            const TrackNamespace& track_namespace,
                            AnnounceResponse announce_response);

        /**
         * @brief Callback notification for unannounce received
         *
         * @param connection_handle         Source connection ID
         * @param track_namespace           Track namespace
         *
         */
        virtual void UnannounceReceived(ConnectionHandle connection_handle,
                                        const TrackNamespace& track_namespace) = 0;

        /**
         * @brief Callback notification for new subscribe received
         *
         * @note The caller **MUST** respond to this via ResolveSubscribe(). If the caller does not
         * override this method, the default will call ResolveSubscribe() with the status of OK
         *
         * @param connection_handle     Source connection ID
         * @param subscribe_id          Subscribe ID received
         * @param proposed_track_alias  The proposed track alias the subscriber would like to use
         * @param track_full_name       Track full name
         * @param subscribe_attributes  Subscribe attributes received
         */
        virtual void SubscribeReceived(ConnectionHandle connection_handle,
                                       uint64_t subscribe_id,
                                       uint64_t proposed_track_alias,
                                       const FullTrackName& track_full_name,
                                       const SubscribeAttributes& subscribe_attributes);

        /**
         * @brief Accept or reject an subscribe that was received
         *
         * @details Accept or reject an subscribe received via SubscribeReceived(). The MoQ Transport
         *      will send the protocol message based on the SubscribeResponse
         *
         * @param connection_handle        source connection ID
         * @param subscribe_id             subscribe ID
         * @param subscribe_response       response to for the subscribe
         */
        virtual void ResolveSubscribe(ConnectionHandle connection_handle,
                                      uint64_t subscribe_id,
                                      SubscribeResponse subscribe_response);

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

      private:
        bool stop_{ false };
    };

} // namespace moq
