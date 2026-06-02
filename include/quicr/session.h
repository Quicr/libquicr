// SPDX-FileCopyrightText: Copyright (c) 2024 Cisco Systems
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

#include "detail/transport.h"
#include "quicr/config.h"
#include "quicr/detail/attributes.h"
#include "quicr/detail/messages.h"
#include "quicr/detail/transport.h"
#include "quicr/object.h"
#include "quicr/publish_fetch_handler.h"
#include "quicr/track_name.h"

namespace quicr {
    /**
     * @brief MoQ Session
     *
     * @details Unified MoQ transport endpoint that operates in either client or server mode depending on
     *   configuration. Use `Session::Create(ClientConfig)` to connect outbound to a relay or peer, or
     *   `Session::Create(ServerConfig)` to listen for inbound connections.
     *
     *   Client mode exposes outbound operations such as `Connect()`, `SubscribeTrack()`, and
     *   `PublishNamespace()`. Server mode exposes inbound callbacks such as `SubscribeReceived()` and
     *   `PublishNamespaceReceived(ConnectionHandle, ...)`, plus relay helpers such as `BindPublisherTrack()`.
     */
    class Session : public Transport
    {
      protected:
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
         * @brief Construct a client-mode session
         *
         * @param cfg MoQ Client Configuration
         */
        Session(const ClientConfig& cfg)
          : Transport(cfg, std::make_shared<ThreadedTickService>(cfg.tick_service_sleep_delay_us))
        {
        }

        /**
         * @brief Construct a server-mode session
         *
         * @param cfg MoQ Server Configuration
         */
        Session(const ServerConfig& cfg)
          : Transport(cfg, std::make_shared<ThreadedTickService>(cfg.tick_service_sleep_delay_us))
        {
        }

      public:
        ~Session() = default;

        /**
         * @brief Create a client-mode session
         *
         * @param cfg MoQ Client Configuration
         */
        static std::shared_ptr<Session> Create(const ClientConfig& cfg)
        {
            return std::shared_ptr<Session>(new Session(cfg));
        }

        /**
         * @brief Create a server-mode session
         *
         * @param cfg MoQ Server Configuration
         */
        static std::shared_ptr<Session> Create(const ServerConfig& cfg)
        {
            return std::shared_ptr<Session>(new Session(cfg));
        }

        /**
         * @brief Starts a client connection via a transport thread
         *
         * @details Makes a client connection session and runs in a newly created thread. All control and track
         *   callbacks will be run based on events. Only valid in client mode; use `Start()` in server mode.
         *
         * @return Status indicating state or error. If successful, status will be kConnecting.
         */
        Status Connect();

        /**
         * @brief Disconnect the client connection gracefully
         *
         * @details Unsubscribes and unpublishes all remaining active ones, sends MoQ control messages
         *   for those and then closes the QUIC connection gracefully. Stops the transport thread. The class
         *   destructor calls this method as well. Status will be updated to reflect not connected.
         *
         * @return Status of kDisconnecting
         */
        Status Disconnect();

        /**
         * @brief Starts server transport thread to listen for new connections
         *
         * @details Creates a new transport thread to listen for new connections. All control and track
         *   callbacks will be run based on events. Only valid in server mode; use `Connect()` in client mode.
         *
         * @return Status indicating state or error. If successful, status will be kReady.
         */
        Status Start();

        /**
         * @brief Stop the server transport
         *
         * @details Stops the transport thread and tears down the listening socket. Only valid in server mode.
         */
        void Stop();

        // --BEGIN RESOLVE METHODS ---------------------------------------------------------------------------
        /** @name Resolve Methods
         *      Methods to accept or reject inbound requests. Most are used in server mode; `ResolveSubscribe()`
         *      is also used when acting as a publisher in client mode.
         */
        ///@{

        /**
         * @brief Accept or reject a subscribe that was received
         *
         * @details Accept or reject a subscribe received via `SubscribeReceived()` (server mode) or when acting
         *      as a publisher in client mode. The MoQ transport will send the protocol message based on the
         *      `RequestResponse`.
         *
         * @param connection_handle  Source connection ID
         * @param request_id         Request ID
         * @param track_alias        Track alias the subscriber should use
         * @param subscribe_response Response for the subscribe
         */
        virtual void ResolveSubscribe(ConnectionHandle connection_handle,
                                      uint64_t request_id,
                                      uint64_t track_alias,
                                      const RequestResponse& subscribe_response);

        /**
         * @brief Accept or reject subscribe namespace that was received
         *
         * @details Server mode only. Called after `SubscribeNamespaceReceived()`.
         *
         * @param connection_handle Source connection ID
         * @param data_ctx_id       Data context ID for the bidir connection to use
         * @param request_id        Request ID
         * @param prefix            Track namespace prefix
         * @param response          Response for remainder of subscribe namespace flow
         */
        virtual void ResolveSubscribeNamespace(ConnectionHandle connection_handle,
                                               DataContextId data_ctx_id,
                                               uint64_t request_id,
                                               const TrackNamespace& prefix,
                                               const SubscribeNamespaceResponse& response);

        /**
         * @brief Accept or reject a fetch that was received
         *
         * @details Accept or reject a fetch received via `StandaloneFetchReceived()` or
         *      `JoiningFetchReceived()`.
         *
         * @param connection_handle Source connection ID
         * @param request_id        Request ID
         * @param priority          Subscriber priority for the fetch response
         * @param group_order       Optional group order for the fetch response
         * @param response          Response to the fetch
         */
        virtual void ResolveFetch(ConnectionHandle connection_handle,
                                  uint64_t request_id,
                                  std::uint8_t priority,
                                  std::optional<messages::GroupOrder> group_order,
                                  const FetchResponse& response);

        /**
         * @brief Accept or reject an announce that was received
         *
         * @details Server mode only. Accept or reject an announce received via `PublishNamespaceReceived()`.
         *      The MoQ transport will send the protocol message based on the `PublishNamespaceResponse`.
         *      Subscribers defined will be sent a copy of the announcement.
         *
         * @param connection_handle  Source connection ID
         * @param request_id         Request ID received for the announce request
         * @param track_namespace    Track namespace
         * @param subscribers        Subscriber connection handles that should be sent the announce
         * @param announce_response  Response for the announcement
         */
        void ResolvePublishNamespace(ConnectionHandle connection_handle,
                                     uint64_t request_id,
                                     const TrackNamespace& track_namespace,
                                     const std::vector<ConnectionHandle>& subscribers,
                                     const PublishNamespaceResponse& announce_response);

        /**
         * @brief Finalize the publish namespace done received
         *
         * @details Server mode only. Sends Publish Namespace Done to SUBSCRIBE_NAMESPACE requestors.
         *
         * @param connection_handle Connection ID of the received publish namespace done message
         * @param request_id        Request ID of the namespace that is done
         * @param subscribers       Subscriber connection handles that should be sent a done message
         */
        void ResolvePublishNamespaceDone(ConnectionHandle connection_handle,
                                         messages::RequestID request_id,
                                         const std::vector<ConnectionHandle>& subscribers);

        /**
         * @brief Accept or reject a request update
         *
         * @param connection_handle     Source connection ID
         * @param request_id            Request ID for the update
         * @param existing_request_id   Existing request ID being updated
         * @param params                Updated parameters
         */
        void ResolveRequestUpdate(ConnectionHandle connection_handle,
                                  uint64_t request_id,
                                  uint64_t existing_request_id,
                                  const messages::Parameters& params);

        ///@}
        // --END RESOLVE METHODS -----------------------------------------------------------------------------

        // --BEGIN CALLBACKS ---------------------------------------------------------------------------------
        /** @name Client Callbacks
         *      Callbacks invoked in client mode unless noted otherwise.
         */
        ///@{

        /**
         * @brief Callback on server setup message
         *
         * @details Server will send server setup in response to client setup message sent. This callback is
         *      called when a server setup has been received. Client mode only.
         *
         * @param server_setup_attributes Server setup attributes received
         */
        virtual void ServerSetupReceived(const ServerSetupAttributes& server_setup_attributes);

        /**
         * @brief Notification on publish namespace status change
         *
         * @details Callback notification for a change in publish namespace status. Client mode only.
         *
         * @param request_id Request ID of the namespace being changed
         * @param status     Publish namespace status
         */
        virtual void PublishNamespaceStatusChanged(messages::RequestID request_id, const PublishNamespaceStatus status);

        /**
         * @brief Callback notification for announce received by subscribe namespace
         *
         * @details Client mode only. Called when a PUBLISH_NAMESPACE is received for a subscribed prefix.
         *
         * @param track_namespace                Track namespace
         * @param publish_namespace_attributes   Publish announce attributes received
         */
        virtual void PublishNamespaceReceived(const TrackNamespace& track_namespace,
                                              const PublishNamespaceAttributes& publish_namespace_attributes);

        /**
         * @brief Callback notification for publish namespace done received
         *
         * @details Client mode only. Called when a PUBLISH_NAMESPACE_DONE is received.
         *
         * @param request_id Request ID of the namespace that is done
         */
        virtual void PublishNamespaceDoneReceived(messages::RequestID request_id);

        /**
         * @brief Callback notification for new subscribe received that doesn't match an existing publish track
         *
         * @details Client mode only. When a new subscribe is received that doesn't match any existing publish
         *      track, this method signals the application that there is a new subscribe full track name. The
         *      application should `PublishTrack()` within this callback (or afterwards).
         *
         * @note The caller **MUST** respond via `ResolveSubscribe()`.
         *
         * @param track_full_name      Track full name
         * @param subscribe_attributes Subscribe attributes received
         */
        virtual void UnpublishedSubscribeReceived(const FullTrackName& track_full_name,
                                                  const messages::SubscribeAttributes& subscribe_attributes);

        /**
         * @brief Notification callback to provide sampled metrics
         *
         * @details Client mode only. Callback will be triggered on `Config::metrics_sample_ms` to provide the
         *      sampled data based on the sample period. After this callback, the period/sample based metrics will
         *      reset and start over for the new period.
         *
         * @param metrics Copy of the connection metrics for the sample period
         */
        void MetricsSampled(const ConnectionMetrics& metrics) override;

        ///@}

        /** @name Server Callbacks
         *      Callbacks invoked in server mode unless noted otherwise.
         */
        ///@{

        /**
         * @brief Callback notification on new connection
         *
         * @details Server mode only. Callback notification that a new connection has been accepted.
         *
         * @param connection_handle Transport connection ID
         * @param remote              Transport remote connection information
         */
        void NewConnectionAccepted(ConnectionHandle connection_handle, const ConnectionRemoteInfo& remote) override;

        /**
         * @brief Callback notification for connection status/state change
         *
         * @details Server mode only. Callback notification indicates state change of connection, such as
         *      disconnected.
         *
         * @param connection_handle Transport connection ID
         * @param status            ConnectionStatus of connection id
         */
        void ConnectionStatusChanged(ConnectionHandle connection_handle, ConnectionStatus status) override;

        /**
         * @brief Notification callback to provide sampled metrics
         *
         * @details Server mode only. Callback will be triggered on `Config::metrics_sample_ms` to provide the
         *      sampled data based on the sample period. After this callback, the period/sample based metrics will
         *      reset and start over for the new period.
         *
         * @param connection_handle Source connection ID
         * @param metrics           Copy of the connection metrics for the sample period
         */
        void MetricsSampled(ConnectionHandle connection_handle, const ConnectionMetrics& metrics) override;

        /**
         * @brief Callback on client setup message
         *
         * @details Server mode only. Client will send a setup message on new connection. Server responds with
         *      server setup.
         *
         * @param connection_handle       Transport connection ID
         * @param client_setup_attributes Decoded client setup message
         */
        virtual void ClientSetupReceived(ConnectionHandle connection_handle,
                                         const ClientSetupAttributes& client_setup_attributes);

        /**
         * @brief Callback notification for new announce received that needs to be authorized
         *
         * @details Server mode only.
         *
         * @note The caller **MUST** respond to this via `ResolvePublishNamespace()`. If the caller does not
         *      override this method, the default will call `ResolvePublishNamespace()` with the status of OK.
         *
         * @param connection_handle             Source connection ID
         * @param track_namespace               Track namespace
         * @param publish_announce_attributes   Publish announce attributes received
         */
        virtual void PublishNamespaceReceived(ConnectionHandle connection_handle,
                                              const TrackNamespace& track_namespace,
                                              const PublishNamespaceAttributes& publish_announce_attributes);

        /**
         * @brief Callback notification for publish namespace done received
         *
         * @details Server mode only. The callback will indicate that publish namespace done has been received.
         *      The app should return a vector of connection handler ids that should receive a copy of the publish
         *      namespace done message. The returned list is based on subscribe namespace prefix matching.
         *
         * @param connection_handle Source connection ID
         * @param request_id        Request ID for the namespace that is done
         *
         * @returns Vector of subscribe namespace connection handler ids matching prefix to the namespace being
         *      marked as done.
         */
        virtual std::vector<ConnectionHandle> PublishNamespaceDoneReceived(ConnectionHandle connection_handle,
                                                                           messages::RequestID request_id);

        /**
         * @brief Callback notification for unsubscribe namespace received
         *
         * @details Server mode only.
         *
         * @param connection_handle Source connection ID
         * @param prefix_namespace  Prefix namespace
         */
        virtual void UnsubscribeNamespaceReceived(ConnectionHandle connection_handle,
                                                  const TrackNamespace& prefix_namespace);

        /**
         * @brief Callback notification for new subscribe namespace received
         *
         * @details Server mode only.
         *
         * @note The implementor **MUST** call `ResolveSubscribeNamespace()`.
         *
         * @param connection_handle  Source connection ID
         * @param data_ctx_id        Data context ID that the message was received on
         * @param prefix_namespace   Track namespace prefix
         * @param attributes         Attributes received
         */
        virtual void SubscribeNamespaceReceived(ConnectionHandle connection_handle,
                                                DataContextId data_ctx_id,
                                                const TrackNamespace& prefix_namespace,
                                                const messages::SubscribeNamespaceAttributes& attributes);

        /**
         * @brief Callback notification for new subscribe received
         *
         * @details Server mode only.
         *
         * @note The caller **MUST** respond to this via `ResolveSubscribe()`. If the caller does not override this
         *      method, the default will call `ResolveSubscribe()` with the status of OK.
         *
         * @param connection_handle    Source connection ID
         * @param request_id           Request ID received
         * @param track_full_name      Track full name
         * @param subscribe_attributes Subscribe attributes received
         */
        virtual void SubscribeReceived(ConnectionHandle connection_handle,
                                       uint64_t request_id,
                                       const FullTrackName& track_full_name,
                                       const messages::SubscribeAttributes& subscribe_attributes);

        /**
         * @brief Callback notification on unsubscribe received
         *
         * @details Server mode only.
         *
         * @param connection_handle Source connection ID
         * @param request_id        Request ID received
         */
        virtual void UnsubscribeReceived(ConnectionHandle connection_handle, uint64_t request_id);

        /**
         * @brief Callback notification on publish done received
         *
         * @details Server mode only.
         *
         * @param connection_handle Source connection ID
         * @param request_id        Request ID received
         */
        virtual void PublishDoneReceived(ConnectionHandle connection_handle, uint64_t request_id);

        /**
         * @brief New group requested received by a subscription
         *
         * @details Server mode only.
         *
         * @param track_full_name Track full name
         * @param group_id        Group ID requested — should be plus one of current group or zero
         */
        virtual void NewGroupRequested(const FullTrackName& track_full_name, messages::GroupId group_id);

        ///@}

        /** @name Shared Callbacks
         *      Callbacks that may be invoked in either client or server mode.
         */
        ///@{

        /**
         * @brief Received notification of an interested track
         *
         * @details The app must call `ResolvePublish()` with a reason code of OK to accept, or another reason code
         *      to reject. In client mode the default implementation rejects with `kNotSupported`.
         *
         * @param connection_handle  Connection that received this publish
         * @param request_id         Incoming publish request ID
         * @param publish_attributes Attributes of the publish
         * @param sub_ns_handler     Matching subscribe namespace handler, if any
         */
        void PublishReceived(ConnectionHandle connection_handle,
                             uint64_t request_id,
                             const messages::PublishAttributes& publish_attributes,
                             std::weak_ptr<SubscribeNamespaceHandler> sub_ns_handler) override;

        /**
         * @brief Event to run on receiving a Standalone Fetch request
         *
         * @param connection_handle Source connection ID
         * @param request_id        Request ID received
         * @param track_full_name   Track full name
         * @param attributes        Fetch attributes received
         */
        void StandaloneFetchReceived(ConnectionHandle connection_handle,
                                     uint64_t request_id,
                                     const FullTrackName& track_full_name,
                                     const quicr::messages::StandaloneFetchAttributes& attributes) override;

        /**
         * @brief Event to run on receiving a Joining Fetch request
         *
         * @param connection_handle Source connection ID
         * @param request_id        Request ID received
         * @param track_full_name   Track full name
         * @param attributes        Fetch attributes received
         */
        void JoiningFetchReceived(ConnectionHandle connection_handle,
                                  uint64_t request_id,
                                  const FullTrackName& track_full_name,
                                  const quicr::messages::JoiningFetchAttributes& attributes) override;

        /**
         * @brief Callback notification on receiving a FetchCancel message
         *
         * @param connection_handle Source connection ID
         * @param request_id        Request ID received
         */
        void FetchCancelReceived(ConnectionHandle connection_handle, uint64_t request_id) override;

        /**
         * @brief Callback notification on request update received
         *
         * @param connection_handle     Source connection ID
         * @param request_id            Request ID for the update
         * @param existing_request_id   Existing request ID being updated
         * @param params                Updated parameters
         */
        void RequestUpdateReceived(ConnectionHandle connection_handle,
                                   uint64_t request_id,
                                   uint64_t existing_request_id,
                                   const messages::Parameters& params) override;

        ///@}
        // --END CALLBACKS -----------------------------------------------------------------------------------

        // --BEGIN SERVER RELAY METHODS ----------------------------------------------------------------------
        /** @name Server Relay Methods
         *      Methods for relaying published content to subscribers. Server mode only.
         */
        ///@{

        /**
         * @brief Bind a server publish track handler based on a subscribe
         *
         * @details The server will create a server publish track handler based on a received subscribe. It will
         *      use this handler to send objects to the subscriber.
         *
         * @param connection_handle Connection ID of the client/subscriber
         * @param src_id            Connection or peering ID for publisher origin
         * @param request_id        Request ID from the received subscribe
         * @param track_handler     Server publish track handler
         * @param ephemeral         Indicates if persistent state tracking is needed
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
         * @param connection_handle Connection ID of the client/subscriber
         * @param src_id            Connection or peering ID of the receiving publisher
         * @param track_handler     Server publish track handler
         * @param send_publish_done Indicates to send publish done or not
         */
        void UnbindPublisherTrack(ConnectionHandle connection_handle,
                                  ConnectionHandle src_id,
                                  const std::shared_ptr<PublishTrackHandler>& track_handler,
                                  bool send_publish_done = false);

        /**
         * @brief Bind a server fetch publisher track handler
         *
         * @param conn_id       Connection ID of the client/fetcher
         * @param track_handler The fetch publisher
         */
        void BindFetchTrack(TransportConnId conn_id, std::shared_ptr<PublishFetchHandler> track_handler);

        /**
         * @brief Unbind a server fetch publisher track handler
         *
         * @param conn_id       Connection ID of the client/fetcher
         * @param track_handler The fetch publisher
         */
        void UnbindFetchTrack(ConnectionHandle conn_id, const std::shared_ptr<PublishFetchHandler>& track_handler);

        ///@}
        // --END SERVER RELAY METHODS ------------------------------------------------------------------------

        // --BEGIN CLIENT OPERATIONS -------------------------------------------------------------------------
        /** @name Client Operations
         *      Outbound MoQ operations. Client mode only.
         */
        ///@{

        /**
         * @brief Get announce status for namespace
         *
         * @param track_namespace Track namespace of the announcement
         *
         * @return Publish namespace status of the namespace
         */
        PublishNamespaceStatus GetPublishNamespaceStatus(const TrackNamespace& track_namespace);

        /**
         * @brief Publish a track namespace
         *
         * @details In MoQ, a publish namespace will result in an announce being sent. Announce OK will be reflected
         *      in the Status() of the `PublishNamespaceHandler` passed. This method can be called at any time, but
         *      normally it would be called before publishing any tracks to the same namespace.
         *
         *      If this method is called after a publish namespace track with a matching namespace that already
         *      exists or if called more than once, this will result in this track handler being added to the active
         *      state of the announce, but it will not result in a repeated announce being sent.
         *
         * @param handler The namespace handler to publish on
         */
        void PublishNamespace(std::shared_ptr<PublishNamespaceHandler> handler);

        /**
         * @brief Unannounce a publish namespace
         *
         * @details Unannounce a publish namespace. **ALL** tracks will be marked unpublish, as if called by
         *      `UnpublishTrack()`.
         *
         * @param handler The namespace handler to unannounce
         */
        void PublishNamespaceDone(const std::shared_ptr<PublishNamespaceHandler>& handler);

        /**
         * @brief Subscribe to prefix namespace
         *
         * @param handler The namespace handler to subscribe to
         */
        void SubscribeNamespace(std::shared_ptr<SubscribeNamespaceHandler> handler);

        /**
         * @brief Unsubscribe from prefix namespace
         *
         * @param handler The namespace handler to unsubscribe from
         */
        void UnsubscribeNamespace(const std::shared_ptr<SubscribeNamespaceHandler>& handler);

        /**
         * @brief Subscribe to a track
         *
         * @param track_handler Track handler to use for track related functions and callbacks
         */
        void SubscribeTrack(std::shared_ptr<SubscribeTrackHandler> track_handler)
        {
            if (connection_handle_) {
                Transport::SubscribeTrack(*connection_handle_, std::move(track_handler));
            }
        }

        /**
         * @brief Request track status
         *
         * @param track_full_name      Track full name
         * @param subscribe_attributes Subscribe attributes for track status
         *
         * @returns Request ID that is used for the track status request
         */
        virtual uint64_t RequestTrackStatus(const FullTrackName& track_full_name,
                                            const messages::SubscribeAttributes& subscribe_attributes = {})
        {
            if (connection_handle_) {
                return Transport::RequestTrackStatus(*connection_handle_, track_full_name, subscribe_attributes);
            }
            return 0;
        }

        /**
         * @brief Unsubscribe track
         *
         * @param track_handler Track handler to use for track related functions and callbacks
         */
        void UnsubscribeTrack(std::shared_ptr<SubscribeTrackHandler> track_handler)
        {
            if (connection_handle_) {
                Transport::UnsubscribeTrack(*connection_handle_, std::move(track_handler));
            }
        }

        /**
         * @brief Publish to a track
         *
         * @param track_handler Track handler to use for track related functions and callbacks
         */
        void PublishTrack(std::shared_ptr<PublishTrackHandler> track_handler)
        {
            if (connection_handle_) {
                Transport::PublishTrack(*connection_handle_, std::move(track_handler));
            }
        }

        /**
         * @brief Unpublish track
         *
         * @details Unpublish a track that was previously published.
         *
         * @param track_handler Track handler used when published track
         */
        void UnpublishTrack(std::shared_ptr<PublishTrackHandler> track_handler)
        {
            if (connection_handle_) {
                Transport::UnpublishTrack(*connection_handle_, std::move(track_handler));
            }
        }

        /**
         * @brief Fetch track
         *
         * @param track_handler Track handler to use for handling fetch related messages
         */
        void FetchTrack(std::shared_ptr<FetchTrackHandler> track_handler)
        {
            if (connection_handle_.has_value()) {
                Transport::FetchTrack(connection_handle_.value(), std::move(track_handler));
            }
        }

        /**
         * @brief Cancel a given fetch track handler
         *
         * @param track_handler The fetch track handler to cancel
         */
        void CancelFetchTrack(std::shared_ptr<FetchTrackHandler> track_handler)
        {
            if (connection_handle_.has_value()) {
                Transport::CancelFetchTrack(connection_handle_.value(), std::move(track_handler));
            }
        }

        /**
         * @brief Get the connection handle
         *
         * @details Client mode only. Returns the single outbound connection handle once connected.
         *
         * @return Connection handle of the client, if connected
         */
        std::optional<ConnectionHandle> GetConnectionHandle() const noexcept { return connection_handle_; }

        ///@}
        // --END CLIENT OPERATIONS ---------------------------------------------------------------------------

        /**
         * @brief Publish a track namespace on a specific connection
         *
         * @details Server mode. Passive announce on an existing connection, e.g. when relaying namespace state to
         *      subscribe namespace requestors. See `Transport::PublishNamespace()` for full details.
         */
        using Transport::PublishNamespace;

      private:
        bool ProcessCtrlMessage(ConnectionContext& conn_ctx,
                                uint64_t data_ctx_id,
                                messages::ControlMessageType msg_type,
                                BytesSpan msg_bytes) override;

        void SetConnectionHandle(ConnectionHandle connection_handle) override
        {
            connection_handle_ = connection_handle;
        }

        void SetStatus(Status status)
        {
            status_ = status;
            StatusChanged(status);
        }

      private:
        std::optional<ConnectionHandle> connection_handle_; ///< Connection ID for the client
    };
}
