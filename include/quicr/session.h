// SPDX-FileCopyrightText: Copyright (c) 2024 Cisco Systems
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

#include "quicr/attributes.h"
#include "quicr/common.h"
#include "quicr/config.h"
#include "quicr/connection.h"
#include "quicr/containers/stream_buffer.h"
#include "quicr/handlers/fetch_track_handler.h"
#include "quicr/handlers/publish_fetch_handler.h"
#include "quicr/handlers/publish_namespace_handler.h"
#include "quicr/handlers/publish_track_handler.h"
#include "quicr/handlers/subscribe_namespace_handler.h"
#include "quicr/handlers/subscribe_track_handler.h"
#include "quicr/messages/message.h"
#include "quicr/messages/messages.h"
#include "quicr/metrics.h"
#include "quicr/transport.h"

#include <timeq/tick_service.h>

#include <atomic>
#include <chrono>
#include <map>
#include <span>
#include <string>
#include <string_view>

namespace spdlog {
    class logger;
}

namespace quicr {

    /**
     * @brief MoQ Session endpoint supporting connection-explicit operations
     *
     * @details Unified MoQ transport endpoint that operates in either client or server mode depending on
     *   configuration. `Client` provides the single-connection convenience API for outbound clients.
     */
    class Session
      : public Connection::Delegate
      , public std::enable_shared_from_this<Session>
    {
        static constexpr std::size_t kControlMessageBufferSize = 4096;

      public:
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

        /// Subscribe Context by received subscribe IDs
        /// Used to map published tracks to subscribes in client mode and to handle joining fetch lookups
        struct SubscribeContext
        {
            FullTrackName track_full_name;
            TrackHash track_hash{ 0, 0 };
            std::optional<messages::Location> largest_location{ std::nullopt };
            std::uint64_t data_ctx_id{ 0 };
        };

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

            std::optional<Bytes> error_reason;
        };

        struct RequestUpdateResponse
        {
            struct Error
            {
                const messages::ErrorCode error_code;
                const std::chrono::milliseconds retry_interval;
                const std::string reason;
            };
            const std::optional<Error> error;
            const messages::Parameters params;
        };

      public:
        /**
         * @brief Create a client-mode endpoint
         *
         * @param cfg MoQ Client Configuration
         */
        static std::shared_ptr<Session> Create(const ClientConfig& cfg,
                                               std::shared_ptr<Transport> transport,
                                               std::shared_ptr<Connection> connection,
                                               std::shared_ptr<timeq::tick_service> tick_service)
        {
            return std::shared_ptr<Session>(
              new Session(cfg, std::move(transport), std::move(connection), std::move(tick_service)));
        }

        /**
         * @brief Create a server-mode endpoint
         *
         * @param cfg MoQ Server Configuration
         */
        static std::shared_ptr<Session> Create(const ServerConfig& cfg,
                                               std::shared_ptr<Transport> transport,
                                               std::shared_ptr<Connection> connection,
                                               std::shared_ptr<timeq::tick_service> tick_service)
        {
            return std::shared_ptr<Session>(
              new Session(cfg, std::move(transport), std::move(connection), std::move(tick_service)));
        }

        virtual ~Session();

        const std::shared_ptr<Connection>& GetConnection() const noexcept { return current_connection_; }

        const std::shared_ptr<Transport>& GetTransport() const noexcept { return quic_transport_; }

        const std::shared_ptr<timeq::tick_service>& GetTickService() const noexcept { return tick_service_; }

        /*===================================================================*/
        // Public API MoQ Instance API methods
        /*===================================================================*/

        /**
         * @brief Subscribe to a track
         *
         * @param track_handler     Track handler to use for track related functions and callbacks
         *
         */
        void SubscribeTrack(std::shared_ptr<SubscribeTrackHandler> track_handler);

        /**
         * @brief Unsubscribe track
         *
         * @param track_handler     Track handler to use for track related functions and callbacks
         */
        void UnsubscribeTrack(const std::shared_ptr<SubscribeTrackHandler>& track_handler);

        /**
         * @brief Update Subscription to a track
         *
         * @param track_handler     Track handler to use for track related functions and callbacks
         *
         */
        void UpdateTrackSubscription(std::shared_ptr<SubscribeTrackHandler> track_handler);

        /**
         * @brief Publish to a track
         *
         * @param track_handler     Track handler to use for track related functions and callbacks
         */
        void PublishTrack(std::shared_ptr<PublishTrackHandler> track_handler);

        /**
         * @brief Unpublish track
         *
         * @param track_handler     Track handler used when published track
         */
        void UnpublishTrack(const std::shared_ptr<PublishTrackHandler>& track_handler);

        /**
         * @brief Publish a track namespace
         *
         * @param ns_handler        Namespace handler to use for track related functions and callbacks
         * @param passive           True indicates that PUBLISH_NAMESPACE will not be sent
         */
        void PublishNamespace(std::shared_ptr<PublishNamespaceHandler> ns_handler, bool passive = false);

        /**
         * @brief Unpublish track namespace
         *
         * @param track_handler     Track handler used when published track
         */
        void PublishNamespaceDone(const std::shared_ptr<PublishNamespaceHandler>& track_handler);

        /**
         * @brief Subscribe to a prefix namespace on a specific connection
         *
         * @param handler           Namespace handler to subscribe with
         */
        void SubscribeNamespace(std::shared_ptr<SubscribeNamespaceHandler> handler);

        /**
         * @brief Unsubscribe from a prefix namespace on a specific connection
         *
         * @param handler           Namespace handler to unsubscribe
         */
        void UnsubscribeNamespace(const std::shared_ptr<SubscribeNamespaceHandler>& handler);

        /**
         * @brief Accept or reject publish that was received
         *
         * @details Accept or reject publish received via PublishReceived(). The MoQ Transport
         *      will send the protocol message based on the PublishResponse
         *      This method will SubscribeTrack() using the handler passed and the
         *      attributes provided.
         *
         * @param request_id                Request ID
         * @param attributes                Attributes for the accepted publish
         * @param publish_response          response for the publish
         * @param handler                   Constructed SubscribeTrackHandler to subscribe track using
         *                                  Clients set this, relay/server does not need to.
         */
        void ResolvePublish(uint64_t request_id,
                            const PublishAttributes& attributes,
                            const PublishResponse& publish_response,
                            std::shared_ptr<SubscribeTrackHandler> handler);

        /**
         * @brief Fetch track
         *
         * @param track_handler         Track handler used for fetching
         */
        void FetchTrack(std::shared_ptr<FetchTrackHandler> track_handler);

        /**
         * @brief Cancel Fetch track
         *
         * @param track_handler         Fetch Track handler to cancel.
         */
        void CancelFetchTrack(std::shared_ptr<FetchTrackHandler> track_handler);

        /**
         * @brief Request track status
         *
         * @param track_full_name           Track full name
         * @param subscribe_attributes      Subscribe attributes for track status
         *
         * * @returns Request ID that is used for the track status request
         */
        uint64_t RequestTrackStatus(const FullTrackName& track_full_name,
                                    const SubscribeAttributes& subscribe_attributes);

        /**
         * @brief Get the status of the endpoint
         *
         * @return Status of the endpoint
         */
        Status GetStatus() const noexcept { return status_; }

        /**
         * @brief Close the underlying transport connection and detach this session as delegate.
         */
        void Disconnect();

        /**
         * @brief Set the WebTransport flag for a connection
         * @param is_webtransport True if this is a WebTransport connection
         */
        void SetWebTransportMode(bool is_webtransport);

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
         * @param request_id         Request ID
         * @param track_alias        Track alias the subscriber should use
         * @param subscribe_response Response for the subscribe
         */
        void ResolveSubscribe(uint64_t request_id, uint64_t track_alias, const RequestResponse& subscribe_response);

        /**
         * @brief Accept or reject subscribe namespace that was received
         *
         * @details Server mode only. Called after `SubscribeNamespaceReceived()`.
         *
         * @param data_ctx_id       Data context ID for the bidir connection to use
         * @param request_id        Request ID
         * @param prefix            Track namespace prefix
         * @param response          Response for remainder of subscribe namespace flow
         */
        void ResolveSubscribeNamespace(std::uint64_t data_ctx_id,
                                       uint64_t request_id,
                                       const TrackNamespace& prefix,
                                       const SubscribeNamespaceResponse& response);

        /**
         * @brief Accept or reject subscribe tracks that was received
         *
         * @details Server mode only. Called after `SubscribeTracksReceived()`.
         *
         * @param data_ctx_id       Data context ID for the bidir connection to use
         * @param request_id        Request ID
         * @param prefix            Track namespace prefix
         * @param response          Response for remainder of subscribe tracks flow
         */
        void ResolveSubscribeTracks(std::uint64_t data_ctx_id,
                                    uint64_t request_id,
                                    const TrackNamespace& prefix,
                                    const SubscribeNamespaceResponse& response);

        /**
         * @brief Accept or reject a fetch that was received
         *
         * @details Accept or reject a fetch received via `StandaloneFetchReceived()` or
         *      `JoiningFetchReceived()`.
         *
         * @param request_id        Request ID
         * @param priority          Subscriber priority for the fetch response
         * @param group_order       Optional group order for the fetch response
         * @param response          Response to the fetch
         */
        void ResolveFetch(uint64_t request_id,
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
         * @param request_id         Request ID received for the announce request
         * @param track_namespace    Track namespace
         * @param subscribers        Subscriber connection handles that should be sent the announce
         * @param announce_response  Response for the announcement
         */
        void ResolvePublishNamespace(uint64_t request_id,
                                     const TrackNamespace& track_namespace,
                                     const std::vector<std::uint64_t>& subscribers,
                                     const PublishNamespaceResponse& announce_response);

        /**
         * @brief Finalize the publish namespace done received
         *
         * @details Server mode only. Sends Publish Namespace Done to SUBSCRIBE_NAMESPACE requestors.
         *
         * @param request_id        Request ID of the namespace that is done
         * @param subscribers       Subscriber connection handles that should be sent a done message
         */
        void ResolvePublishNamespaceDone(std::uint64_t request_id, const std::vector<std::uint64_t>& subscribers);

        /**
         * @brief Accept or reject a request update
         *
         * @param request_id            Request being updated
         * @param response              Request update response
         */
        void ResolveRequestUpdate(uint64_t request_id, const RequestUpdateResponse& response);

        /**
         * @brief Accept or reject track status that was received
         *
         * @details Accept or reject track status received via TrackStatusReceived(). The MoQ Transport
         *      will send the protocol message based on the RequestResponse. Per MOQT draft-14,
         *      track status request, ok, and error are the same as subscribe
         *
         * @param request_id               Request ID that was provided by TrackStatusReceived
         * @param subscribe_response       Response to the track status request, either Ok or Error.
         *                                 Largest loation should be set if kOk and there is content
         */
        void ResolveTrackStatus(uint64_t request_id, const RequestResponse& subscribe_response);

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
         * @param src_id            Connection or peering ID for publisher origin
         * @param request_id        Request ID from the received subscribe
         * @param track_handler     Server publish track handler
         * @param ephemeral         Indicates if persistent state tracking is needed
         */
        void BindPublisherTrack(std::uint64_t src_id,
                                uint64_t request_id,
                                const std::shared_ptr<PublishTrackHandler>& track_handler,
                                bool ephemeral = false);

        /**
         * @brief Unbind a server publish track handler
         *
         * @details Removes a server publish track handler state.
         *
         * @param src_id            Connection or peering ID of the receiving publisher
         * @param track_handler     Server publish track handler
         * @param send_publish_done Indicates to send publish done or not
         */
        void UnbindPublisherTrack(std::uint64_t src_id,
                                  const std::shared_ptr<PublishTrackHandler>& track_handler,
                                  bool send_publish_done = false);

        /**
         * @brief Bind a server fetch publisher track handler
         *
         * @param track_handler The fetch publisher
         */
        void BindFetchTrack(std::shared_ptr<PublishFetchHandler> track_handler);

        /**
         * @brief Unbind a server fetch publisher track handler
         *
         * @param track_handler The fetch publisher
         */
        void UnbindFetchTrack(const std::shared_ptr<PublishFetchHandler>& track_handler);

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
         * @param request_id         Incoming publish request ID
         * @param publish_attributes Attributes of the publish
         * @param sub_ns_handler     Matching subscribe namespace handler, if any
         */
        virtual void PublishReceived(uint64_t request_id,
                                     const PublishAttributes& publish_attributes,
                                     std::weak_ptr<SubscribeNamespaceHandler> sub_ns_handler);

        /**
         * @brief Event to run on receiving a Standalone Fetch request.
         *
         * @param request_id        Request ID received.
         * @param track_full_name   Track full name
         * @param attributes        Fetch attributes received.
         */
        virtual void StandaloneFetchReceived(uint64_t request_id,
                                             const FullTrackName& track_full_name,
                                             const StandaloneFetchAttributes& attributes);

        /**
         * @brief Event to run on receiving a Joining Fetch request.
         *
         * @param request_id        Request ID received.
         * @param track_full_name   Track full name
         * @param attributes        Fetch attributes received.
         */
        virtual void JoiningFetchReceived(uint64_t request_id,
                                          const FullTrackName& track_full_name,
                                          const JoiningFetchAttributes& attributes);

        /**
         * @brief Callback notification on receiving a FetchCancel message.
         *
         * @param request_id        Request ID received.
         */
        virtual void FetchCancelReceived(uint64_t request_id);

        /**
         * @brief Callback notification for track status message received
         *
         * @note The caller **MUST** respond to this via ResolveTrackStatus(). If the caller does not
         * override this method, the default will call ResolveTrackStatus() with the status of OK
         *
         * @param request_id            Request ID received
         * @param track_full_name       Track full name
         */
        virtual void TrackStatusReceived(uint64_t request_id, const FullTrackName& track_full_name);

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
                                                  const SubscribeAttributes& subscribe_attributes);

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
         * @param remote              Transport remote connection information
         */
        virtual void NewConnectionAccepted(const ConnectionRemoteInfo& remote);

        /**
         * @brief Callback on client setup message
         *
         * @details Server mode only. Client will send a setup message on new connection. Server responds with
         *      server setup.
         *
         * @param client_setup_attributes Decoded client setup message
         */
        virtual void ClientSetupReceived(const ClientSetupAttributes& client_setup_attributes);

        /**
         * @brief Callback notification for publish namespace done received
         *
         * @details Server mode only. The callback will indicate that publish namespace done has been received.
         *      The app should return a vector of connection handler ids that should receive a copy of the publish
         *      namespace done message. The returned list is based on subscribe namespace prefix matching.
         *
         * @param request_id        Request ID for the namespace that is done
         *
         * @returns Vector of subscribe namespace connection handler ids matching prefix to the namespace being
         *      marked as done.
         */
        virtual std::vector<std::uint64_t> PublishNamespaceDoneReceived(std::uint64_t request_id);

        /**
         * @brief Callback notification for unsubscribe namespace received
         *
         * @details Server mode only.
         *
         * @param prefix_namespace  Prefix namespace
         */
        virtual void UnsubscribeNamespaceReceived(const TrackNamespace& prefix_namespace);

        /**
         * @brief Callback notification for new subscribe namespace received
         *
         * @details Server mode only.
         *
         * @note The implementor **MUST** call `ResolveSubscribeNamespace()`.
         *
         * @param data_ctx_id        Data context ID that the message was received on
         * @param prefix_namespace   Track namespace prefix
         * @param attributes         Attributes received
         */
        virtual void SubscribeNamespaceReceived(std::uint64_t data_ctx_id,
                                                const TrackNamespace& prefix_namespace,
                                                const SubscribeNamespaceAttributes& attributes);

        /**
         * @brief Callback notification for new subscribe tracks received
         *
         * @details Server mode only.
         *
         * @note The implementor **MUST** call `ResolveSubscribeTracks()`.
         *
         * @param data_ctx_id        Data context ID that the message was received on
         * @param prefix_namespace   Track namespace prefix
         * @param attributes         Attributes received
         */
        virtual void SubscribeTracksReceived(std::uint64_t data_ctx_id,
                                             const TrackNamespace& prefix_namespace,
                                             const SubscribeNamespaceAttributes& attributes);

        /**
         * @brief Callback notification for new subscribe received
         *
         * @details Server mode only.
         *
         * @note The caller **MUST** respond to this via `ResolveSubscribe()`. If the caller does not override this
         *      method, the default will call `ResolveSubscribe()` with the status of OK.
         *
         * @param request_id           Request ID received
         * @param track_full_name      Track full name
         * @param subscribe_attributes Subscribe attributes received
         */
        virtual void SubscribeReceived(uint64_t request_id,
                                       const FullTrackName& track_full_name,
                                       const SubscribeAttributes& subscribe_attributes);

        /**
         * @brief Callback notification on unsubscribe received
         *
         * @details Server mode only.
         *
         * @param request_id        Request ID received
         */
        virtual void UnsubscribeReceived(uint64_t request_id);

        /**
         * @brief Callback notification on publish done received
         *
         * @details Server mode only.
         *
         * @param request_id        Request ID received
         */
        virtual void PublishDoneReceived(uint64_t request_id);

        /**
         * @brief New group requested received by a subscription
         *
         * @details Server mode only.
         *
         * @param track_full_name Track full name
         * @param group_id        Group ID requested — should be plus one of current group or zero
         */
        virtual void NewGroupRequested(const FullTrackName& track_full_name, std::uint64_t group_id);

        ///@}
        // --END CALLBACKS -----------------------------------------------------------------------------------

      protected:
        Session() = delete;

        /**
         * @brief Client mode constructor
         *
         * @param cfg MoQ Client Configuration
         */
        Session(const ClientConfig& cfg, std::shared_ptr<Transport> transport, std::shared_ptr<Connection> connection)
          : Session(cfg,
                    std::move(transport),
                    std::move(connection),
                    std::make_shared<timeq::threaded_tick_service>(cfg.tick_service_sleep_delay_us))
        {
        }

        /**
         * @brief Server mode constructor
         *
         * @param cfg MoQ Server Configuration
         */
        Session(const ServerConfig& cfg, std::shared_ptr<Transport> transport, std::shared_ptr<Connection> connection)
          : Session(cfg,
                    std::move(transport),
                    std::move(connection),
                    std::make_shared<timeq::threaded_tick_service>(cfg.tick_service_sleep_delay_us))
        {
        }

        /**
         * @brief Client mode constructor with explicit tick service
         *
         * @param cfg            MoQ Instance Client Configuration
         * @param tick_service   Shared pointer to the tick service to use
         */
        Session(const ClientConfig& cfg,
                std::shared_ptr<Transport> transport,
                std::shared_ptr<Connection> connection,
                std::shared_ptr<timeq::tick_service> tick_service);

        /**
         * @brief Server mode constructor with explicit tick service
         *
         * @param cfg            MoQ Server Configuration
         * @param tick_service   Shared pointer to the tick service to use
         */
        Session(const ServerConfig& cfg,
                std::shared_ptr<Transport> transport,
                std::shared_ptr<Connection> connection,
                std::shared_ptr<timeq::tick_service> tick_service);

        void OnStreamClosed(std::uint64_t stream_id,
                            std::shared_ptr<StreamRxContext> rx_ctx,
                            std::optional<uint64_t> data_ctx_id,
                            StreamClosedFlag flag) override;

      private:
        /*===================================================================*/
        // Transport Delegate/callback functions
        /*===================================================================*/

        void OnConnectionStatus(Connection::Status status) override;

        void OnNewDataContext(const std::uint64_t& data_ctx_id) override;

        void OnRecvStream(uint64_t stream_id,
                          std::optional<std::uint64_t> data_ctx_id,
                          const bool is_bidir = false) override;

        void OnRecvDgram(std::optional<std::uint64_t> data_ctx_id) override;

        void OnConnectionMetricsSampled(MetricsTimeStamp sample_time,

                                        const QuicConnectionMetrics& quic_connection_metrics) override;

        void OnDataMetricsStampled(MetricsTimeStamp sample_time,
                                   std::uint64_t data_ctx_id,
                                   const QuicDataContextMetrics& quic_data_context_metrics) override;

        /*===================================================================*/
        // Private methods
        /*===================================================================*/

        void Init();

        std::shared_ptr<Session> GetSharedPtr();

        bool ProcessRequestMessage(std::uint64_t data_ctx_id,
                                   messages::ControlMessageType msg_type,
                                   BytesSpan msg_bytes);

        bool ProcessCtrlMessage(messages::ControlMessageType msg_type, BytesSpan msg_bytes);

        void SetStatus(Status status)
        {
            status_ = status;
            StatusChanged(status);
        }

        void SendCtrlMsg(std::uint64_t data_ctx_id, std::shared_ptr<const std::vector<uint8_t>> data);

        template<typename... Fields>
        void SendCtrlMsg(std::uint64_t data_ctx_id, messages::ControlMessageType type, Fields&&... args)
        {
            messages::Message msg = messages::Message{}.PrependType(type).ReserveLength();

            (msg.Append(args), ...);

            SendCtrlMsg(data_ctx_id, msg.ToBytes());
        }

        void SendSetup();

        /*===================================================================*/
        // Requests
        /*===================================================================*/

        void SendTrackStatusOk(std::uint64_t data_ctx_id,
                               const std::optional<messages::Location>& largest_object,
                               const messages::TrackExtensions& track_properties);

        void SendSubscribeNamespaceOk(std::uint64_t data_ctx_id);
        void SendSubscribeTracksOk(std::uint64_t data_ctx_id) { SendSubscribeNamespaceOk(data_ctx_id); }
        void SendPublishNamespaceOk(std::uint64_t data_ctx_id) { SendSubscribeNamespaceOk(data_ctx_id); }
        void SendRequestUpdateOk(std::uint64_t data_ctx_id,
                                 std::optional<std::uint64_t> expires,
                                 const std::optional<messages::Location>& largest_object);

        // Prefer the above typed overloads.
        void SendRequestOk(std::uint64_t data_ctx_id,
                           const messages::Parameters& params,
                           const messages::TrackExtensions& track_properties = {});

        void SendRequestUpdate(const std::uint64_t data_ctx_id,
                               std::uint64_t request_id,
                               TrackHash th,
                               std::optional<std::uint64_t> end_group_id,
                               std::uint8_t priority,
                               bool forward);

        void SendRequestError(std::uint64_t data_ctx_id,
                              std::uint64_t request_id,
                              messages::ErrorCode error,
                              std::chrono::milliseconds retry_interval,
                              const std::string& reason);

        /*===================================================================*/
        // Publish Namespace
        /*===================================================================*/

        void SendPublishNamespace(std::uint64_t data_ctx_id,
                                  std::uint64_t request_id,
                                  const TrackNamespace& track_namespace);

        /*===================================================================*/
        // Subscribe Namespace
        /*===================================================================*/

        void SendSubscribeNamespace(std::uint64_t data_ctx_id,
                                    std::uint64_t request_id,
                                    const TrackNamespace& prefix,
                                    const messages::Filter& filter,
                                    messages::ControlMessageType type);

        void SendUnsubscribeNamespace(std::uint64_t data_ctx_id, const TrackNamespace& prefix);

        /*===================================================================*/
        // Subscribe
        /*===================================================================*/

        void SendSubscribe(std::uint64_t data_ctx_id,
                           std::uint64_t request_id,
                           const FullTrackName& tfn,
                           TrackHash th,
                           std::uint8_t priority,
                           std::optional<messages::GroupOrder> group_order,
                           const messages::Filter& filter,
                           std::optional<std::chrono::milliseconds> delivery_timeout);

        void SendSubscribeOk(std::uint64_t data_ctx_id,
                             std::uint64_t request_id,
                             uint64_t track_alias,
                             uint64_t expires,
                             const std::optional<messages::Location>& largest_location,
                             messages::GroupOrder publisher_default_group_order);

        /*===================================================================*/
        // Publish
        /*===================================================================*/

        void SendPublish(std::uint64_t data_ctx_id, std::uint64_t request_id, const PublishAttributes& publish);

        void SendPublishDone(std::uint64_t data_ctx_id,
                             std::uint64_t request_id,
                             messages::PublishDoneStatusCode status,
                             const std::string& reason);

        void SendPublishOk(std::uint64_t data_ctx_id, const PublishOkAttributes& attributes);

        std::optional<std::uint64_t> FindSubscribeNamespaceDataContext(const TrackNamespace& track_namespace) const;

        std::uint64_t ResponseDataContext(const std::uint64_t request_id) const;

        /*===================================================================*/
        // Track Status
        /*===================================================================*/

        void SendTrackStatus(std::uint64_t request_id, const FullTrackName& tfn);

        /*===================================================================*/
        // Fetch
        /*===================================================================*/

        void SendFetch(std::uint64_t data_ctx_id,
                       std::uint64_t request_id,
                       const FullTrackName& tfn,
                       std::uint8_t priority,
                       std::optional<messages::GroupOrder> group_order,
                       const messages::Location& start_location,
                       const messages::FetchEndLocation& end_location);

        void SendJoiningFetch(std::uint64_t data_ctx_id,
                              std::uint64_t request_id,
                              std::uint8_t priority,
                              std::optional<messages::GroupOrder> group_order,
                              std::uint64_t joining_request_id,
                              std::uint64_t joining_start,
                              bool absolute);

        void SendFetchOk(std::uint64_t data_ctx_id,
                         messages::GroupOrder publisher_default_group_order,
                         bool end_of_track,
                         messages::Location end_location);

        /*===================================================================*/
        // Other member functions
        /*===================================================================*/

        void RemoveSubscribeTrack(SubscribeTrackHandler& handler, bool remove_handler = true);

        void RemoveSubscribeNamespace(SubscribeNamespaceHandler& handler,
                                      bool remove_handler = true,
                                      bool send_unsubscribe = true);

        void CloseRequestHandler(std::uint64_t request_id, std::uint64_t stream_id, StreamClosedFlag flag);

        void ClosePublishTrackLocal(PublishTrackHandler& handler, std::uint64_t stream_id, bool is_reset);

        std::shared_ptr<PublishTrackHandler> GetPubTrackHandler(TrackHash& th);

        void RemoveAllTracksForConnectionClose();

        uint64_t GetNextRequestID();

        bool OnRecvSubgroup(std::uint64_t track_alias,
                            StreamRxContext& rx_ctx,
                            std::uint64_t stream_id,
                            std::shared_ptr<const std::vector<uint8_t>> data) const;
        bool OnRecvFetch(std::uint64_t request_id,
                         StreamRxContext& rx_ctx,
                         std::uint64_t stream_id,

                         std::shared_ptr<const std::vector<uint8_t>> data) const;

        std::uint64_t CreateStream(std::uint64_t data_ctx_id, uint8_t priority);

        TransportError Enqueue(const std::uint64_t& data_ctx_id,
                               std::uint64_t stream_id,
                               std::shared_ptr<const std::vector<uint8_t>> bytes,
                               const uint8_t priority,
                               const uint32_t ttl_ms,
                               const uint32_t delay_ms,
                               const Transport::EnqueueFlags flags);

      private:
        std::shared_ptr<Connection> current_connection_;

        std::map<std::uint64_t, SubscribeContext> recv_req_id;

        /// Lookup request ID by carrying data context.
        std::map<std::uint64_t, std::uint64_t> request_id_by_data_ctx;

        /// Active inbound publish namespace notifications (not handler based).
        std::vector<std::uint64_t> recv_publish_namespaces;

        /// Handlers by request ID
        std::map<std::uint64_t, std::shared_ptr<TrackHandler>> request_handlers;

        /**
         * Data is received with a track alias that is set by the publisher. The map key
         * track alias is the received publisher track alias specific to the connection. Data received
         * is matched to this track alias to find the subscriber handler that matches. The
         * subscribe handler has both received track alias and generated track alias.
         */
        std::map<std::uint64_t, std::shared_ptr<SubscribeTrackHandler>> sub_by_recv_track_alias;

        /**
         * Publish tracks by namespace and name. map[track namespace][track name] = track handler
         * Used mainly in client mode only
         */
        std::map<std::uint64_t, std::map<std::uint64_t, std::shared_ptr<PublishTrackHandler>>> pub_tracks_by_name;

        /// Publish tracks to subscriber by source id of publisher - required for multi-publisher
        std::map<std::uint64_t, std::map<uint64_t, std::shared_ptr<PublishTrackHandler>>> pub_tracks_by_track_alias;

        /// Fetch Publishers by request ID.
        std::map<std::uint64_t, std::shared_ptr<PublishTrackHandler>> pub_fetch_tracks_by_request_id;

        std::mutex state_mutex_;

        const bool client_mode_;

        std::shared_ptr<spdlog::logger> logger_;

        bool stop_{ false };

        const ServerConfig server_config_;

        const ClientConfig client_config_;

        Status status_{ Status::kNotReady };

        std::shared_ptr<timeq::tick_service> tick_service_;

        std::shared_ptr<Transport> quic_transport_; // **MUST** be last for proper order of destruction

        friend class PublishTrackHandler;
        friend class PublishFetchHandler;
        friend class SubscribeTrackHandler;
    };

} // namespace quicr
