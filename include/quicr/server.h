// SPDX-FileCopyrightText: Copyright (c) 2024 Cisco Systems
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

#include "quicr/detail/messages.h"
#include <quicr/config.h>
#include <quicr/detail/attributes.h>
#include <quicr/detail/transport.h>
#include <quicr/object.h>
#include <quicr/publish_fetch_handler.h>
#include <quicr/track_name.h>

namespace quicr {
    using namespace quicr;

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
        struct ClientSetupResponse
        {};

        /**
         * @brief Response to received MOQT Announce message
         */
        struct PublishNamespaceResponse
        {
            /**
             * @details **kOK** indicates that the announce is accepted and OK should be sent. Any other
             *       value indicates that the announce is not accepted and the reason code and other
             *       fields will be set.
             */
            enum class ReasonCode : uint8_t
            {
                kOk = 0,
                kInternalError
            };
            ReasonCode reason_code;

            std::optional<messages::ReasonPhrase> error_reason;
        };

        /**
         * @brief MoQ Server constructor to create the MOQ server mode instance
         *
         * @param cfg           MoQ Server Configuration
         */
        Server(const ServerConfig& cfg)
          : Transport(cfg, std::make_shared<ThreadedTickService>(cfg.tick_service_sleep_delay_us))
        {
        }

        Server(const ServerConfig& cfg, std::shared_ptr<ThreadedTickService> tick_service)
          : Transport(cfg, tick_service)
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
         * @brief Bind a server publish track handler based on a subscribe
         *
         * @details The server will create a server publish track handler based on a received
         *      subscribe. It will use this handler to send objects to subscriber.
         *
         * @param connection_handle         Connection ID of the client/subscriber
         * @param src_id                    Connection or peering Id for publisher origin
         * @param request_id                Request ID from the received subscribe
         * @param track_handler             Server publish track handler
         * @param ephemeral                 Bool value to indicate if the state tracking is needed
         */
        void BindPublisherTrack(ConnectionHandle connection_handle,
                                ConnectionHandle src_id,
                                uint64_t request_id,
                                const std::shared_ptr<PublishTrackHandler>& track_handler,
                                bool ephemeral = false);

        /**
         * @brief Unbind a server publish track handler
         *
         * @details Removes a server publish track handler state.
         *
         * @param connection_handle         Connection ID of the client/subscriber
         * @param src_id                    Connect or peering Id of the receiving publisher
         * @param track_handler             Server publish track handler
         * @param send_publish_done         Indicates to send publish done or not
         */
        void UnbindPublisherTrack(ConnectionHandle connection_handle,
                                  ConnectionHandle src_id,
                                  const std::shared_ptr<PublishTrackHandler>& track_handler,
                                  bool send_publish_done = false);

        /**
         * @brief Bind a server fetch publisher track handler.
         * @param conn_id Connection Id of the client/fetcher.
         * @param track_handler The fetch publisher.
         */
        void BindFetchTrack(TransportConnId conn_id, std::shared_ptr<PublishFetchHandler> track_handler);

        /**
         * @brief Unbind a server fetch publisher track handler.
         * @param conn_id Connection ID of the client/fetcher.
         * @param track_handler The fetch publisher.
         */
        void UnbindFetchTrack(ConnectionHandle conn_id, const std::shared_ptr<PublishFetchHandler>& track_handler);

        /**
         * @brief Accept or reject an subscribe that was received
         *
         * @details Accept or reject an subscribe received via SubscribeReceived(). The MoQ Transport
         *      will send the protocol message based on the SubscribeResponse
         *
         * @param connection_handle        source connection ID
         * @param request_id               Request ID
         * @param track_alias              Track alias the subscriber should use.
         * @param subscribe_response       response to for the subscribe
         */
        virtual void ResolveSubscribe(ConnectionHandle connection_handle,
                                      uint64_t request_id,
                                      uint64_t track_alias,
                                      const SubscribeResponse& subscribe_response);

        /**
         * @brief Accept or reject subscribe namespace that was received
         *
         * @param connection_handle source connection ID
         * @param data_ctx_id       Data context ID for the bidir connection to use
         * @param request_id        Request ID
         * @param prefix            Track namespace prefix
         * @param response          Response for remainder of subscribe namespace flow
         */
        virtual void ResolveSubscribeNamespace(ConnectionHandle connection_handle,
                                               DataContextId data_ctx_id,
                                               uint64_t request_id,
                                               const messages::TrackNamespacePrefix& prefix,
                                               const SubscribeNamespaceResponse& response);

        /**
         * @brief Accept or reject a Fetch that was received
         *
         * @details Accept or reject a fetch received via FetchReceived().
         *
         * @param connection_handle        Source connection ID
         * @param request_id               Request ID
         * @param type                     The type of Fetch being resolved.
         * @param priority
         * @param group_order
         * @param response                 Response to the Fetch.
         */
        virtual void ResolveFetch(ConnectionHandle connection_handle,
                                  uint64_t request_id,
                                  messages::SubscriberPriority priority,
                                  messages::GroupOrder group_order,
                                  const FetchResponse& response);

        // --BEGIN CALLBACKS ----------------------------------------------------------------------------------
        /** @name Server Calbacks
         *      slient transport specific callbacks
         */
        ///@{

        /**
         * @brief Callback notification on new connection
         * @details Callback notification that a new connection has been accepted
         *
         * @param connection_handle          Transport connection ID
         * @param remote                     Transport remote connection information
         */
        void NewConnectionAccepted(ConnectionHandle connection_handle, const ConnectionRemoteInfo& remote) override;

        /**
         * @brief Callback notification for connection status/state change
         * @details Callback notification indicates state change of connection, such as disconnected
         *
         * @param connection_handle          Transport connection ID
         * @param status                     ConnectionStatus of connection id
         */
        void ConnectionStatusChanged(ConnectionHandle connection_handle, ConnectionStatus status) override;

        /**
         * @brief Notification callback to provide sampled metrics
         *
         * @details Callback will be triggered on Config::metrics_sample_ms to provide the sampled data based
         *      on the sample period.  After this callback, the period/sample based metrics will reset and start over
         *      for the new period.
         *
         * @param connection_handle           Source connection ID
         * @param metrics                     Copy of the connection metrics for the sample period
         */
        void MetricsSampled(ConnectionHandle connection_handle, const ConnectionMetrics& metrics) override;

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
        virtual void PublishNamespaceReceived(ConnectionHandle connection_handle,
                                              const TrackNamespace& track_namespace,
                                              const PublishNamespaceAttributes& publish_announce_attributes);

        /**
         * @brief Accept or reject an announce that was received
         *
         * @details Accept or reject an announce received via AnnounceReceived(). The MoQ Transport
         *      will send the protocol message based on the AnnounceResponse. Subscribers
         *      defined will be sent a copy of the announcement
         *
         * @param connection_handle        source connection ID
         * @param request_id               Request Id received for the announce request
         * @param track_namespace          track namespace
         * @param subscribers              Vector/list of subscriber connection handles that should be sent the announce
         * @param announce_response        response to for the announcement
         */
        void ResolvePublishNamespace(ConnectionHandle connection_handle,
                                     uint64_t request_id,
                                     const TrackNamespace& track_namespace,
                                     const std::vector<ConnectionHandle>& subscribers,
                                     const PublishNamespaceResponse& announce_response);

        /**
         * @brief Callback notification for publish namespace done received
         *
         * @details The callback will indicate that publish namespace done has been received. The
         *    app should return a vector of connection handler ids that should receive a
         *    copy of the pubilsh namespace done message. The returned list is based on subscribe namespace prefix
         *    matching.
         *
         * @param connection_handle         Source connection ID
         * @param track_namespace           Track namespace
         *
         * @returns vector of subscribe namespace connection handler ids matching prefix to the namespace being
         * marked as done.
         */
        virtual std::vector<ConnectionHandle> PublishNamespaceDoneReceived(ConnectionHandle connection_handle,
                                                                           const TrackNamespace& track_namespace) = 0;

        /**
         * @brief Finalize the publish done received
         *
         * @details Server calls this to have libquicr send Publish Namespace Done to SUBSCRIBE_NAMESPACE
         *  requestors.
         * @param connection_handle        Connection ID of the received publish done message
         * @param track_namespace          Track namespace of publish namespace
         * @param subscribers              Vector/list of subscriber connection handles/ids that should be sent a done
         */
        void ResolvePublishNamespaceDone(ConnectionHandle connection_handle,
                                         const TrackNamespace& track_namespace,
                                         const std::vector<ConnectionHandle>& subscribers);

        /**
         * @brief Callback notification for Unsubscribe announces received
         *
         * @param connection_handle         Source connection ID
         * @param data_ctx_id               Data context ID that the message received on
         * @param prefix_namespace          Prefix namespace
         *
         */
        virtual void UnsubscribeNamespaceReceived(ConnectionHandle connection_handle,
                                                  DataContextId data_ctx_id,
                                                  const TrackNamespace& prefix_namespace) = 0;

        /**
         * @brief Callback notification for new subscribe namespace received
         *
         * @note The implementor **MUST** call ResolveSubscribeNamespace().
         *
         * @param connection_handle             Source connection ID
         * @param data_ctx_id                   Data context ID that the message was received on
         * @param prefix_namespace              Track namespace
         * @param attributes                    Attributes received
         */
        virtual void SubscribeNamespaceReceived(ConnectionHandle connection_handle,
                                                DataContextId data_ctx_id,
                                                const TrackNamespace& prefix_namespace,
                                                const SubscribeNamespaceAttributes& attributes) = 0;

        /**
         * @brief Callback notification for new subscribe received
         *
         * @note The caller **MUST** respond to this via ResolveSubscribe(). If the caller does not
         * override this method, the default will call ResolveSubscribe() with the status of OK
         *
         * @param connection_handle     Source connection ID
         * @param request_id            Request ID received
         * @param track_full_name       Track full name
         * @param subscribe_attributes  Subscribe attributes received
         */
        virtual void SubscribeReceived(ConnectionHandle connection_handle,
                                       uint64_t request_id,
                                       const FullTrackName& track_full_name,
                                       const messages::SubscribeAttributes& subscribe_attributes);

        /**
         * @brief Callback notification on unsubscribe received
         *
         * @param connection_handle   Source connection ID
         * @param request_id          Request ID received
         */
        virtual void UnsubscribeReceived(ConnectionHandle connection_handle, uint64_t request_id) = 0;

        /**
         * @brief Event to run on receiving a Standalone Fetch request.
         *
         * @param connection_handle Source connection ID.
         * @param request_id        Request ID received.
         * @param track_full_name   Track full name
         * @param attributes        Fetch attributes received.
         */
        virtual void StandaloneFetchReceived(ConnectionHandle connection_handle,
                                             uint64_t request_id,
                                             const FullTrackName& track_full_name,
                                             const quicr::messages::StandaloneFetchAttributes& attributes) override;

        /**
         * @brief Event to run on receiving a Joining Fetch request.
         *
         * @param connection_handle Source connection ID.
         * @param request_id        Request ID received.
         * @param track_full_name   Track full name
         * @param attributes        Fetch attributes received.
         */
        virtual void JoiningFetchReceived(ConnectionHandle connection_handle,
                                          uint64_t request_id,
                                          const FullTrackName& track_full_name,
                                          const quicr::messages::JoiningFetchAttributes& attributes) override;

        /**
         * @brief Callback notification on receiving a FetchCancel message.
         *
         * @param connection_handle Source connection ID.
         * @param request_id        Request ID received.
         */
        virtual void FetchCancelReceived(ConnectionHandle connection_handle, uint64_t request_id) override;

        /**
         * @brief Callback notification on Publish Done received
         *
         * @param connection_handle   Source connection ID
         * @param request_id          Request ID received
         */
        virtual void PublishDoneReceived(ConnectionHandle connection_handle, uint64_t request_id) = 0;

        /**
         * @brief New Group Requested received by a subscription
         *
         * @param track_full_name       Track full name
         * @param group_id              Group ID requested - Should be plus one of current group or zero
         */
        virtual void NewGroupRequested(const FullTrackName& track_full_name, messages::GroupId group_id);

        ///@}
        // --END OF CALLBACKS ----------------------------------------------------------------------------------

      private:
        bool ProcessCtrlMessage(ConnectionContext& conn_ctx,
                                uint64_t data_ctx_id,
                                messages::ControlMessageType msg_type,
                                BytesSpan msg_bytes) override;

        bool stop_{ false };
    };

} // namespace moq
