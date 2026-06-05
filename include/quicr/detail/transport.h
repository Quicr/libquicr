// SPDX-FileCopyrightText: Copyright (c) 2024 Cisco Systems
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

#include "attributes.h"
#include "message.h"
#include "messages.h"
#include "quic_transport.h"
#include "quicr/common.h"
#include "quicr/config.h"
#include "quicr/fetch_track_handler.h"
#include "quicr/metrics.h"
#include "quicr/publish_fetch_handler.h"
#include "quicr/publish_namespace_handler.h"
#include "quicr/publish_track_handler.h"
#include "quicr/subscribe_namespace_handler.h"
#include "quicr/subscribe_track_handler.h"

#include <timeq/tick_service.h>

#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>

#include <atomic>
#include <chrono>
#include <map>
#include <span>
#include <string>
#include <string_view>

namespace quicr {

    /**
     * @brief MoQ transport endpoint supporting connection-explicit operations
     *
     * @details Unified MoQ transport endpoint that operates in either client or server mode depending on
     *   configuration. `Client` provides the single-connection convenience API for outbound clients.
     */
    class Transport
      : public ITransport::TransportDelegate
      , public std::enable_shared_from_this<Transport>
    {
      public:
        Transport() = delete;

        /**
         * @brief Status of the transport
         */
        enum class Status : uint8_t
        {
            kReady = 0,
            kNotReady,

            kInternalError,

            kInvalidParams,

            kConnecting,
            kDisconnecting,
            kNotConnected,
            kFailedToConnect,
            kPendingServerSetup,
        };

        /**
         * @brief Control message status codes
         */
        enum class ControlMessageStatus : uint8_t
        {
            kMessageIncomplete,        ///< control message is incomplete and more data is needed
            kMessageComplete,          ///< control message is complete and stream buffer get any has complete message
            kStreamBufferCannotBeZero, ///< stream buffer cannot be zero when parsing message type
            kStreamBufferMissingType,  ///< connection context is missing message type
            kUnsupportedMessageType,   ///< Unsupported MOQT message type
        };

        enum class StreamDataMessageStatus : uint8_t
        {
        };

        /**
         * @brief Connection status codes
         */
        enum class ConnectionStatus : uint8_t
        {
            kNotConnected = 0,
            kConnecting,
            kConnected,
            kIdleTimeout,
            kClosedByRemote
        };

        /**
         * @brief Connection remote information
         */
        struct ConnectionRemoteInfo
        {
            std::string ip; ///< remote IPv4/v6 address
            uint16_t port;  ///< remote port
        };

        /**
         * @brief Create a client-mode endpoint
         *
         * @param cfg MoQ Client Configuration
         */
        static std::shared_ptr<Transport> Create(const ClientConfig& cfg)
        {
            return std::shared_ptr<Transport>(new Transport(cfg));
        }

        /**
         * @brief Create a server-mode endpoint
         *
         * @param cfg MoQ Server Configuration
         */
        static std::shared_ptr<Transport> Create(const ServerConfig& cfg)
        {
            return std::shared_ptr<Transport>(new Transport(cfg));
        }

        ~Transport();

        const std::shared_ptr<timeq::tick_service>& GetTickService() const noexcept { return tick_service_; }

        // -------------------------------------------------------------------------------------------------
        // Lifecycle
        // -------------------------------------------------------------------------------------------------

        /**
         * @brief Startup transport. Server mode will listen for connection, client mode will make a connection.
         *
         * @details Creates a new transport thread to listen for new connections. All control and track
         *   callbacks will be run based on events.
         *
         * @return Status indicating state or error. If successful, status will be kReady.
         */
        virtual Status Start();

        /**
         * @brief Stop the transport and teardown.
         *
         * @details Stops the transport thread and any connections.
         */
        virtual void Stop();

        // -------------------------------------------------------------------------------------------------
        // Public API MoQ Instance API methods
        // -------------------------------------------------------------------------------------------------
        /**
         * @brief Subscribe to a track
         *
         * @param connection_handle         Connection ID to send subscribe
         * @param track_handler             Track handler to use for track related functions and callbacks
         *
         */
        void SubscribeTrack(ConnectionHandle connection_handle, std::shared_ptr<SubscribeTrackHandler> track_handler);

        /**
         * @brief Unsubscribe track
         *
         * @param connection_handle         Connection ID to send subscribe
         * @param track_handler             Track handler to use for track related functions and callbacks
         */
        void UnsubscribeTrack(ConnectionHandle connection_handle,
                              const std::shared_ptr<SubscribeTrackHandler>& track_handler);

        /**
         * @brief Update Subscription to a track
         *
         * @param connection_handle         Connection ID to send subscribe
         * @param track_handler             Track handler to use for track related functions and callbacks
         *
         */
        void UpdateTrackSubscription(ConnectionHandle connection_handle,
                                     std::shared_ptr<SubscribeTrackHandler> track_handler);

        /**
         * @brief Publish to a track
         *
         * @param connection_handle           Connection ID from transport for the QUIC connection context
         * @param track_handler               Track handler to use for track related functions
         *                                    and callbacks
         */
        void PublishTrack(ConnectionHandle connection_handle, std::shared_ptr<PublishTrackHandler> track_handler);

        /**
         * @brief Unpublish track
         *
         * @param connection_handle           Connection ID from transport for the QUIC connection context
         * @param track_handler               Track handler used when published track
         */
        void UnpublishTrack(ConnectionHandle connection_handle,
                            const std::shared_ptr<PublishTrackHandler>& track_handler);

        /**
         * @brief Publish a track namespace
         *
         * @param connection_handle           Connection ID from transport for the QUIC connection context
         * @param ns_handler                  Namespace handler to use for track related functions
         *                                    and callbacks
         * @param passive                     True indicates that PUBLISH_NAMESPACE will not be sent
         */
        void PublishNamespace(ConnectionHandle connection_handle,
                              std::shared_ptr<PublishNamespaceHandler> ns_handler,
                              bool passive = false);

        /**
         * @brief Unpublish track namespace
         *
         * @param connection_handle           Connection ID from transport for the QUIC connection context
         * @param track_handler               Track handler used when published track
         */
        void PublishNamespaceDone(ConnectionHandle connection_handle,
                                  const std::shared_ptr<PublishNamespaceHandler>& track_handler);

        /**
         * @brief Subscribe to a prefix namespace on a specific connection
         *
         * @param connection_handle           Connection ID from transport for the QUIC connection context
         * @param handler                     Namespace handler to subscribe with
         */
        void SubscribeNamespace(ConnectionHandle connection_handle, std::shared_ptr<SubscribeNamespaceHandler> handler);

        /**
         * @brief Unsubscribe from a prefix namespace on a specific connection
         *
         * @param connection_handle           Connection ID from transport for the QUIC connection context
         * @param handler                     Namespace handler to unsubscribe
         */
        void UnsubscribeNamespace(ConnectionHandle connection_handle,
                                  const std::shared_ptr<SubscribeNamespaceHandler>& handler);

        /**
         * @brief Accept or reject publish that was received
         *
         * @details Accept or reject publish received via PublishReceived(). The MoQ Transport
         *      will send the protocol message based on the PublishResponse
         *      This method will SubscribeTrack() using the handler passed and the
         *      attributes provided.
         *
         * @param connection_handle         source connection ID
         * @param request_id                Request ID
         * @param attributes                Attributes for the accepted publish
         * @param publish_response          response for the publish
         * @param handler                   Constructed SubscribeTrackHandler to subscribe track using
         *                                  Clients set this, relay/server does not need to.
         */
        void ResolvePublish(ConnectionHandle connection_handle,
                            uint64_t request_id,
                            const messages::PublishAttributes& attributes,
                            const PublishResponse& publish_response,
                            std::shared_ptr<SubscribeTrackHandler> handler);

        /**
         * @brief Fetch track
         *
         * @param connection_handle         Connection ID to send fetch
         * @param track_handler             Track handler used for fetching
         */
        void FetchTrack(ConnectionHandle connection_handle, std::shared_ptr<FetchTrackHandler> track_handler);

        /**
         * @brief Cancel Fetch track
         *
         * @param connection_handle         Connection ID to send fetch cancel.
         * @param track_handler             Fetch Track handler to cancel.
         */
        void CancelFetchTrack(ConnectionHandle connection_handle, std::shared_ptr<FetchTrackHandler> track_handler);

        /**
         * @brief Request track status
         *
         * @param connection_handle           Source connection ID.
         * @param track_full_name             Track full name
         * @param subscribe_attributes        Subscribe attributes for track status
         *
         * * @returns Request ID that is used for the track status request
         */
        uint64_t RequestTrackStatus(ConnectionHandle connection_handle,
                                    const FullTrackName& track_full_name,
                                    const messages::SubscribeAttributes& subscribe_attributes);

        /**
         * @brief Get the status of the endpoint
         *
         * @return Status of the endpoint
         */
        Status GetStatus() const noexcept { return status_; }

        /**
         * @brief Set the WebTransport flag for a connection
         * @param conn_id Connection ID
         * @param is_webtransport True if this is a WebTransport connection
         */
        void SetWebTransportMode(ConnectionHandle conn_id, bool is_webtransport);

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
        void ResolveSubscribe(ConnectionHandle connection_handle,
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
        void ResolveSubscribeNamespace(ConnectionHandle connection_handle,
                                       DataContextId data_ctx_id,
                                       uint64_t request_id,
                                       const TrackNamespace& prefix,
                                       const SubscribeNamespaceResponse& response);

        /**
         * @brief Accept or reject subscribe tracks that was received
         *
         * @details Server mode only. Called after `SubscribeTracksReceived()`.
         *
         * @param connection_handle Source connection ID
         * @param data_ctx_id       Data context ID for the bidir connection to use
         * @param request_id        Request ID
         * @param prefix            Track namespace prefix
         * @param response          Response for remainder of subscribe tracks flow
         */
        void ResolveSubscribeTracks(ConnectionHandle connection_handle,
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
        void ResolveFetch(ConnectionHandle connection_handle,
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

        /**
         * @brief Accept or reject track status that was received
         *
         * @details Accept or reject track status received via TrackStatusReceived(). The MoQ Transport
         *      will send the protocol message based on the RequestResponse. Per MOQT draft-14,
         *      track status request, ok, and error are the same as subscribe
         *
         * @param connection_handle        source connection ID
         * @param request_id               Request ID that was provided by TrackStatusReceived
         * @param subscribe_response       Response to the track status request, either Ok or Error.
         *                                 Largest loation should be set if kOk and there is content
         */
        void ResolveTrackStatus(ConnectionHandle connection_handle,
                                uint64_t request_id,
                                const RequestResponse& subscribe_response);

        ///@}
        // --END RESOLVE METHODS -----------------------------------------------------------------------------

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

        // --BEGIN CALLBACKS ---------------------------------------------------------------------------------
        /** @name Base Callbacks
         *  Callbacks that may be invoked in either client or server mode.
         */
        ///@{
        /**
         * @brief Callback notification for status/state change
         * @details Callback notification indicates state change of connection, such as disconnected
         *
         * @param status           Changed Status value
         */
        virtual void StatusChanged([[maybe_unused]] Status status) {}

        /**
         * @brief Callback notification for new publish received
         *
         * @details The app must call `ResolvePublish()` with a reason code of OK to accept, or another reason code
         *      to reject. In client mode the default implementation rejects with `kNotSupported`.
         *
         * @param connection_handle  Connection that received this publish
         * @param request_id         Incoming publish request ID
         * @param publish_attributes Attributes of the publish
         * @param sub_ns_handler     Matching subscribe namespace handler, if any
         */
        virtual void PublishReceived(ConnectionHandle connection_handle,
                                     uint64_t request_id,
                                     const messages::PublishAttributes& publish_attributes,
                                     std::weak_ptr<SubscribeNamespaceHandler> sub_ns_handler);

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
                                             const quicr::messages::StandaloneFetchAttributes& attributes);

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
                                          const quicr::messages::JoiningFetchAttributes& attributes);

        /**
         * @brief Callback notification on receiving a FetchCancel message.
         *
         * @param connection_handle Source connection ID.
         * @param request_id        Request ID received.
         */
        virtual void FetchCancelReceived(ConnectionHandle connection_handle, uint64_t request_id);

        /**
         * @brief Callback notification for track status message received
         *
         * @note The caller **MUST** respond to this via ResolveTrackStatus(). If the caller does not
         * override this method, the default will call ResolveTrackStatus() with the status of OK
         *
         * @param connection_handle     Source connection ID
         * @param request_id            Request ID received
         * @param track_full_name       Track full name
         */
        virtual void TrackStatusReceived(ConnectionHandle connection_handle,
                                         uint64_t request_id,
                                         const FullTrackName& track_full_name);
        /**
         * @brief Callback notification for Request Ok received
         *
         * @note The REQUEST_OK message is sent to a response to REQUEST_UPDATE, TRACK_STATUS, SUBSCRIBE_NAMESPACE and
         *       PUBLISH_NAMESPACE requests. The unique request ID in the REQUEST_OK is used to associate it with the
         *       correct type of request.
         *
         * @param connection_handle     Source connection ID
         * @param request_id            Request ID received
         * @param largest_location      Largest location (only set for responses from TRACK_STATUS and REQUEST_UPDATE)
         */
        virtual void RequestOkReceived(ConnectionHandle connection_handle,
                                       uint64_t request_id,
                                       std::optional<messages::Location> largest_location = std::nullopt);

        /**
         * @brief Callback notification for Request Error received
         *
         * @note The REQUEST_ERROR message is sent to a response to any request (SUBSCRIBE, FETCH, PUBLISH,
         *       SUBSCRIBE_NAMESPACE, PUBLISH_NAMESPACE, TRACK_STATUS). The unique request ID in the REQUEST_ERROR is
         *       used to associate it with the correct type of request.
         *
         * @param connection_handle     Source connection ID
         * @param request_id            Request ID received
         * @param response              Response message
         */
        virtual void RequestErrorReceived(ConnectionHandle connection_handle,
                                          uint64_t request_id,
                                          const RequestResponse& response);

        /**
         * @brief Callback notification on request update received
         *
         * @param connection_handle     Source connection ID
         * @param request_id            Request ID for the update
         * @param existing_request_id   Existing request ID being updated
         * @param params                Updated parameters
         */
        virtual void RequestUpdateReceived(ConnectionHandle connection_handle,
                                           uint64_t request_id,
                                           uint64_t existing_request_id,
                                           const messages::Parameters& params);
        ///@}

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
        virtual void MetricsSampled(const ConnectionMetrics& metrics);

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
        virtual void NewConnectionAccepted(ConnectionHandle connection_handle, const ConnectionRemoteInfo& remote);

        /**
         * @brief Callback notification for connection status/state change
         *
         * @details Server mode only. Callback notification indicates state change of connection, such as
         *      disconnected.
         *
         * @param connection_handle Transport connection ID
         * @param status            ConnectionStatus of connection id
         */
        virtual void ConnectionStatusChanged(ConnectionHandle connection_handle, ConnectionStatus status);

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
        virtual void MetricsSampled(ConnectionHandle connection_handle, const ConnectionMetrics& metrics);

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
         * @brief Callback notification for new subscribe tracks received
         *
         * @details Server mode only.
         *
         * @note The implementor **MUST** call `ResolveSubscribeTracks()`.
         *
         * @param connection_handle  Source connection ID
         * @param data_ctx_id        Data context ID that the message was received on
         * @param prefix_namespace   Track namespace prefix
         * @param attributes         Attributes received
         */
        virtual void SubscribeTracksReceived(ConnectionHandle connection_handle,
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
        // --END CALLBACKS -----------------------------------------------------------------------------------

      protected:
        struct StartTransportResult
        {
            Status status;
            std::optional<ConnectionHandle> connection_handle;
        };

        /**
         * @brief Client mode constructor
         *
         * @param cfg MoQ Client Configuration
         */
        Transport(const ClientConfig& cfg)
          : Transport(cfg, std::make_shared<timeq::threaded_tick_service>(cfg.tick_service_sleep_delay_us))
        {
        }

        /**
         * @brief Server mode constructor
         *
         * @param cfg MoQ Server Configuration
         */
        Transport(const ServerConfig& cfg)
          : Transport(cfg, std::make_shared<timeq::threaded_tick_service>(cfg.tick_service_sleep_delay_us))
        {
        }

        /**
         * @brief Client mode constructor with explicit tick service
         *
         * @param cfg            MoQ Instance Client Configuration
         * @param tick_service   Shared pointer to the tick service to use
         */
        Transport(const ClientConfig& cfg, std::shared_ptr<timeq::tick_service> tick_service);

        /**
         * @brief Server mode constructor with explicit tick service
         *
         * @param cfg            MoQ Server Configuration
         * @param tick_service   Shared pointer to the tick service to use
         */
        Transport(const ServerConfig& cfg, std::shared_ptr<timeq::tick_service> tick_service);

        StartTransportResult StartTransport();

      private:
        Status StopTransport();

        // -------------------------------------------------------------------------------------------------
        // Transport Delegate/callback functions
        // -------------------------------------------------------------------------------------------------

        void OnNewDataContext([[maybe_unused]] const ConnectionHandle& connection_handle,
                              [[maybe_unused]] const DataContextId& data_ctx_id) override;

        void OnConnectionStatus(const ConnectionHandle& connection_handle, const TransportStatus status) override;
        void OnNewConnection(const ConnectionHandle& connection_handle, const TransportRemote& remote) override;
        void OnRecvStream(const ConnectionHandle& connection_handle,
                          uint64_t stream_id,
                          std::optional<DataContextId> data_ctx_id,
                          const bool is_bidir = false) override;
        void OnRecvDgram(const ConnectionHandle& connection_handle, std::optional<DataContextId> data_ctx_id) override;

        void OnConnectionMetricsSampled(MetricsTimeStamp sample_time,
                                        TransportConnId conn_id,
                                        const QuicConnectionMetrics& quic_connection_metrics) override;

        void OnDataMetricsStampled(MetricsTimeStamp sample_time,
                                   TransportConnId conn_id,
                                   DataContextId data_ctx_id,
                                   const QuicDataContextMetrics& quic_data_context_metrics) override;

        void OnStreamClosed(const ConnectionHandle& connection_handle,
                            std::uint64_t stream_id,
                            std::shared_ptr<StreamRxContext> rx_ctx,
                            std::optional<uint64_t> request_id,
                            StreamClosedFlag flag) override;

        // -------------------------------------------------------------------------------------------------
        // End of transport handler/callback functions
        // -------------------------------------------------------------------------------------------------

        static constexpr std::size_t kControlMessageBufferSize = 4096;

        struct TrackHandler
        {
            std::shared_ptr<BaseTrackHandler> handler;

            TrackHandler() = default;
            ~TrackHandler() = default;

            TrackHandler(const std::shared_ptr<BaseTrackHandler>& other_handler)
              : handler(other_handler)
            {
            }

            TrackHandler& operator=(const TrackHandler& other) = default;

            TrackHandler& operator=(const std::shared_ptr<BaseTrackHandler>& other_handler)
            {
                handler = other_handler;
                return *this;
            }

            template<typename T>
            std::shared_ptr<T> Get()
            {
                return std::dynamic_pointer_cast<T>(handler);
            }

            template<typename T>
            std::shared_ptr<T> Get() const
            {
                return std::dynamic_pointer_cast<T>(handler);
            }
        };

        struct ConnectionContext
        {
            ConnectionContext(const ConnectionContext& other)
              : next_request_id(other.next_request_id.load(std::memory_order_seq_cst))
            {
            }

            ConnectionHandle connection_handle{ 0 };
            std::optional<uint64_t> ctrl_data_ctx_id;
            std::optional<uint64_t> ctrl_stream_id;

            bool setup_complete{ false }; ///< True if both client and server setup messages have completed
            bool closed{ false };
            uint64_t client_version{ 0 };

            struct CtrlMsgBuffer
            {
                /// Indicates the current message type being read
                std::optional<messages::ControlMessageType> msg_type;

                std::vector<uint8_t> data; ///< Data buffer to parse control message
            };
            std::map<uint64_t, CtrlMsgBuffer> ctrl_msg_buffer; ///< Control message buffer

            /** Next Connection request Id. This value is shifted left when setting Request Id.
             * The least significant bit is used to indicate client (0) vs server (1).
             */
            std::atomic<uint64_t> next_request_id{ 0 }; ///< Connection specific ID for control messages messages

            /// Subscribe Context by received subscribe IDs
            /// Used to map published tracks to subscribes in client mode and to handle joining fetch lookups
            struct SubscribeContext
            {
                FullTrackName track_full_name;
                TrackHash track_hash{ 0, 0 };
                std::optional<messages::Location> largest_location{ std::nullopt };
                DataContextId data_ctx_id{ 0 };
            };

            std::map<messages::RequestID, SubscribeContext> recv_req_id;

            /// Handlers by request ID
            std::map<messages::RequestID, TrackHandler> request_handlers;

            /**
             * Data is received with a track alias that is set by the publisher. The map key
             * track alias is the received publisher track alias specific to the connection. Data received
             * is matched to this track alias to find the subscriber handler that matches. The
             * subscribe handler has both received track alias and generated track alias.
             */
            std::map<messages::TrackAlias, std::shared_ptr<SubscribeTrackHandler>> sub_by_recv_track_alias;

            /**
             * Publish tracks by namespace and name. map[track namespace][track name] = track handler
             * Used mainly in client mode only
             */
            std::map<TrackNamespaceHash, std::map<TrackNameHash, std::shared_ptr<PublishTrackHandler>>>
              pub_tracks_by_name;

            /// Publish tracks to subscriber by source id of publisher - required for multi-publisher
            std::map<messages::TrackAlias, std::map<uint64_t, std::shared_ptr<PublishTrackHandler>>>
              pub_tracks_by_track_alias;

            /// Published tracks by quic transport data context ID.
            std::map<DataContextId, std::shared_ptr<PublishTrackHandler>> pub_tracks_by_data_ctx_id;

            /// Fetch Publishers by request ID.
            std::map<messages::RequestID, std::shared_ptr<PublishTrackHandler>> pub_fetch_tracks_by_request_id;

            ConnectionMetrics metrics{};   ///< Connection metrics
            bool is_webtransport{ false }; ///< True if this connection uses WebTransport over HTTP/3

            ConnectionContext() {}

            /*
             * Get the next request Id to use
             */
            uint64_t GetNextRequestId()
            {
                uint64_t rid = next_request_id;
                next_request_id += 2;

                return rid;
            }
        };

        // -------------------------------------------------------------------------------------------------
        // Private methods
        // -------------------------------------------------------------------------------------------------

        void Init();

        std::shared_ptr<Transport> GetSharedPtr();

        ConnectionContext& GetConnectionContext(ConnectionHandle conn);

        bool ProcessCtrlMessage(ConnectionContext& conn_ctx,
                                uint64_t data_ctx_id,
                                messages::ControlMessageType msg_type,
                                BytesSpan msg_bytes);

        void SetStatus(Status status)
        {
            status_ = status;
            StatusChanged(status);
        }

        void SendCtrlMsg(const ConnectionContext& conn_ctx,
                         DataContextId data_ctx_id,
                         std::shared_ptr<const std::vector<uint8_t>> data);

        template<typename... Fields>
        void SendCtrlMsg(const ConnectionContext& conn_ctx,
                         DataContextId data_ctx_id,
                         messages::ControlMessageType type,
                         Fields&&... args)
        {
            messages::Message msg = messages::Message{}.PrependType(type).ReserveLength();

            (msg.Append(args), ...);

            SendCtrlMsg(conn_ctx, data_ctx_id, msg.ToBytes());
        }

        void SendClientSetup();
        void SendServerSetup(ConnectionContext& conn_ctx);

        /*===================================================================*/
        // Requests
        /*===================================================================*/

        void SendRequestOk(ConnectionContext& conn_ctx,
                           DataContextId data_ctx_id,
                           messages::RequestID request_id,
                           std::optional<messages::Location> largest_location = std::nullopt);

        void SendRequestUpdate(const ConnectionContext& conn_ctx,
                               DataContextId data_ctx_id,
                               messages::RequestID request_id,
                               messages::RequestID existing_request_id,
                               TrackHash th,
                               std::optional<messages::GroupId> end_group_id,
                               std::uint8_t priority,
                               bool forward);

        void SendRequestError(ConnectionContext& conn_ctx,
                              DataContextId data_ctx_id,
                              messages::RequestID request_id,
                              messages::ErrorCode error,
                              std::chrono::milliseconds retry_interval,
                              const std::string& reason);

        /*===================================================================*/
        // Publish Namespace
        /*===================================================================*/

        void SendPublishNamespace(ConnectionContext& conn_ctx,
                                  DataContextId data_ctx_id,
                                  messages::RequestID request_id,
                                  const TrackNamespace& track_namespace);

        void SendPublishNamespaceDone(ConnectionContext& conn_ctx,
                                      DataContextId data_ctx_id,
                                      messages::RequestID request_id);

        /*===================================================================*/
        // Subscribe Namespace
        /*===================================================================*/

        void SendSubscribeNamespace(ConnectionHandle conn_handle, std::shared_ptr<SubscribeNamespaceHandler> handler);

        void SendUnsubscribeNamespace(ConnectionHandle conn_handle,
                                      const std::shared_ptr<SubscribeNamespaceHandler>& handler);

        /*===================================================================*/
        // Subscribe
        /*===================================================================*/

        void SendSubscribe(ConnectionContext& conn_ctx,
                           DataContextId data_ctx_id,
                           messages::RequestID request_id,
                           const FullTrackName& tfn,
                           TrackHash th,
                           std::uint8_t priority,
                           std::optional<messages::GroupOrder> group_order,
                           const messages::Filter& filter,
                           std::optional<std::chrono::milliseconds> delivery_timeout);

        void SendSubscribeOk(ConnectionContext& conn_ctx,
                             DataContextId data_ctx_id,
                             messages::RequestID request_id,
                             uint64_t track_alias,
                             uint64_t expires,
                             const std::optional<messages::Location>& largest_location,
                             messages::GroupOrder publisher_default_group_order);
        void SendUnsubscribe(ConnectionContext& conn_ctx,
                             DataContextId data_ctx_id,
                             messages::RequestID request_id);

        /*===================================================================*/
        // Publish
        /*===================================================================*/

        void SendPublish(ConnectionContext& conn_ctx,
                         DataContextId data_ctx_id,
                         messages::RequestID request_id,
                         const FullTrackName& tfn,
                         uint64_t track_alias,
                         messages::GroupOrder group_order,
                         std::optional<messages::Location> largest_location,
                         bool forward,
                         bool support_new_group);

        void SendPublishDone(ConnectionContext& conn_ctx,
                             DataContextId data_ctx_id,
                             messages::RequestID request_id,
                             messages::PublishDoneStatusCode status,
                             const std::string& reason);

        void SendPublishOk(ConnectionContext& conn_ctx,
                           DataContextId data_ctx_id,
                           messages::RequestID request_id,
                           bool forward,
                           std::uint8_t priority,
                           std::optional<messages::GroupOrder> group_order,
                           const messages::Filter& filter);

        std::optional<DataContextId> FindSubscribeNamespaceDataContext(const ConnectionContext& conn_ctx,
                                                                       const TrackNamespace& track_namespace) const;

        DataContextId ResponseDataContext(const ConnectionContext& conn_ctx, messages::RequestID request_id) const;

        /*===================================================================*/
        // Track Status
        /*===================================================================*/

        void SendTrackStatus(ConnectionContext& conn_ctx, messages::RequestID request_id, const FullTrackName& tfn);

        /*===================================================================*/
        // Fetch
        /*===================================================================*/

        void SendFetch(ConnectionContext& conn_ctx,
                       messages::RequestID request_id,
                       const FullTrackName& tfn,
                       std::uint8_t priority,
                       std::optional<messages::GroupOrder> group_order,
                       const messages::Location& start_location,
                       const messages::FetchEndLocation& end_location);

        void SendJoiningFetch(ConnectionContext& conn_ctx,
                              messages::RequestID request_id,
                              std::uint8_t priority,
                              std::optional<messages::GroupOrder> group_order,
                              messages::RequestID joining_request_id,
                              messages::GroupId joining_start,
                              bool absolute);

        void SendFetchCancel(ConnectionContext& conn_ctx, messages::RequestID request_id);

        void SendFetchOk(ConnectionContext& conn_ctx,
                         messages::RequestID request_id,
                         messages::GroupOrder publisher_default_group_order,
                         bool end_of_track,
                         messages::Location end_location);

        /*===================================================================*/
        // Other member functions
        /*===================================================================*/

        void CloseConnection(ConnectionHandle connection_handle,
                             messages::TerminationReason reason,
                             const std::string& reason_str);

        void RemoveSubscribeTrack(ConnectionContext& conn_ctx,
                                  SubscribeTrackHandler& handler,
                                  bool remove_handler = true,
                                  bool send_unsubscribe = true);

        void CloseRequestHandler(ConnectionContext& conn_ctx,
                                 ConnectionHandle connection_handle,
                                 messages::RequestID request_id,
                                 std::uint64_t stream_id,
                                 StreamClosedFlag flag);

        void ClosePublishTrackLocal(ConnectionContext& conn_ctx,
                                    ConnectionHandle connection_handle,
                                    PublishTrackHandler& handler,
                                    std::uint64_t stream_id,
                                    bool is_reset);

        std::shared_ptr<PublishTrackHandler> GetPubTrackHandler(ConnectionContext& conn_ctx, TrackHash& th);

        void RemoveAllTracksForConnectionClose(ConnectionContext& conn_ctx);

        uint64_t GetNextRequestId();

        bool OnRecvSubgroup(messages::StreamHeaderProperties properties,
                            std::vector<uint8_t>::const_iterator cursor_it,
                            StreamRxContext& rx_ctx,
                            std::uint64_t stream_id,
                            ConnectionContext& conn_ctx,
                            std::shared_ptr<const std::vector<uint8_t>> data) const;
        bool OnRecvFetch(std::vector<uint8_t>::const_iterator cursor_it,
                         StreamRxContext& rx_ctx,
                         std::uint64_t stream_id,
                         ConnectionContext& conn_ctx,
                         std::shared_ptr<const std::vector<uint8_t>> data) const;

        std::uint64_t CreateStream(ConnectionHandle conn, std::uint64_t data_ctx_id, uint8_t priority);

        TransportError Enqueue(const TransportConnId& conn_id,
                               const DataContextId& data_ctx_id,
                               std::uint64_t stream_id,
                               std::shared_ptr<const std::vector<uint8_t>> bytes,
                               const uint8_t priority,
                               const uint32_t ttl_ms,
                               const uint32_t delay_ms,
                               const ITransport::EnqueueFlags flags);

      private:
        // -------------------------------------------------------------------------------------------------
        // Private member variables
        // -------------------------------------------------------------------------------------------------
        std::mutex state_mutex_;
        const bool client_mode_;
        std::shared_ptr<spdlog::logger> logger_;
        bool stop_{ false };
        const ServerConfig server_config_;
        const ClientConfig client_config_;

        std::map<ConnectionHandle, ConnectionContext> connections_;

        Status status_{ Status::kNotReady };

        std::shared_ptr<timeq::tick_service> tick_service_;
        std::shared_ptr<ITransport> quic_transport_; // **MUST** be last for proper order of destruction

        friend class PublishTrackHandler;
        friend class PublishFetchHandler;
        friend class SubscribeTrackHandler;
    };

} // namespace quicr
