// SPDX-FileCopyrightText: Copyright (c) 2024 Cisco Systems
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

#include "attributes.h"
#include "messages.h"
#include "quic_transport.h"
#include "quicr/common.h"
#include "quicr/config.h"
#include "quicr/fetch_track_handler.h"
#include "quicr/metrics.h"
#include "quicr/publish_namespace_handler.h"
#include "quicr/publish_track_handler.h"
#include "quicr/subscribe_namespace_handler.h"
#include "quicr/subscribe_track_handler.h"
#include "tick_service.h"

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
     * @brief MOQ Implementation supporting both client and server modes
     * @details MoQ implementation is the handler for either a client or server. It can run
     *   in only one mode, client or server.
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
         * @brief Client mode Constructor to create the MOQ instance
         *
         * @param cfg            MoQ Instance Client Configuration
         * @param tick_service   Shared pointer to the tick service to use
         */
        Transport(const ClientConfig& cfg, std::shared_ptr<TickService> tick_service);

        /**
         * @brief Server mode Constructor to create the MOQ instance
         *
         * @param cfg            MoQ Server Configuration
         * @param tick_service   Shared pointer to the tick service to use
         */
        Transport(const ServerConfig& cfg, std::shared_ptr<TickService> tick_service);

        ~Transport();

        const std::shared_ptr<TickService>& GetTickService() const noexcept { return tick_service_; }

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
         * @brief Publish to a track
         *
         * @param connection_handle           Connection ID from transport for the QUIC connection context
         * @param track_handler               Track handler to use for track related functions
         *                                    and callbacks
         */
        void PublishNamespace(ConnectionHandle connection_handle,
                              std::shared_ptr<PublishNamespaceHandler> track_handler);

        /**
         * @brief Unpublish track
         *
         * @param connection_handle           Connection ID from transport for the QUIC connection context
         * @param track_handler               Track handler used when published track
         */
        void PublishNamespaceDone(ConnectionHandle connection_handle,
                                  const std::shared_ptr<PublishNamespaceHandler>& track_handler);

        /**
         * @brief Callback notification for new publish received
         *
         * @note The caller **MUST** respond to this via ResolvePublish(). If the caller does not
         * override this method, the default will call ResolvePublish() with the status of OK
         *
         * @param connection_handle     Source connection ID
         * @param request_id            Request ID received
         * @param publish_attributes    Publish attributes received
         */
        virtual void PublishReceived(ConnectionHandle connection_handle,
                                     uint64_t request_id,
                                     const messages::PublishAttributes& publish_attributes) = 0;

        /**
         * @brief Accept or reject publish that was received
         *
         * @details Accept or reject publish received via PublishReceived(). The MoQ Transport
         *      will send the protocol message based on the PublishResponse
         *
         * @param connection_handle        source connection ID
         * @param request_id               Request ID
         * @param attributes               Attributes for the accepted publish
         * @param publish_response         response for the publish
         */
        void ResolvePublish(ConnectionHandle connection_handle,
                            uint64_t request_id,
                            const messages::PublishAttributes& attributes,
                            const PublishResponse& publish_response);

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
        virtual void FetchCancelReceived(ConnectionHandle connection_handle, uint64_t request_id) = 0;

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
        virtual void ResolveTrackStatus(ConnectionHandle connection_handle,
                                        uint64_t request_id,
                                        const RequestResponse& subscribe_response);

        /**
         * @brief Get the status of the Client
         *
         * @return Status of the Client
         */
        Status GetStatus() const noexcept { return status_; }

        /** @name Base Calbacks
         *  Both client and server implement the same transport base callbacks
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
         * @brief Callback notification for Request Error received
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

        ///@}

        /// @cond
      protected:
        Status Start();

        Status Stop();

        /// @endcond

      private:
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
                            StreamClosedFlag flag) override;

        // -------------------------------------------------------------------------------------------------
        // End of transport handler/callback functions
        // -------------------------------------------------------------------------------------------------

        static constexpr std::size_t kControlMessageBufferSize = 4096;
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
            };

            std::map<messages::RequestID, SubscribeContext> recv_req_id;

            /// Tracks by request ID (Subscribe and Fetch)
            std::map<messages::RequestID, std::shared_ptr<SubscribeTrackHandler>> sub_tracks_by_request_id;

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

            /** MoQT does not send all announce messages with namespace. Instead, they are sent
             *  with request-id. The namespace is needed. This map is used to map request ID to namespace
             */
            std::map<messages::RequestID, TrackNamespaceHash> pub_tracks_ns_by_request_id;

            /**
             * State to track by request ID PUBLISH_NAMESPACE sent to requestors of SUBSCRIBE_NAMESPACE
             *    This is used in ResolvePublishNamespaceDone to find the request Id for the publish done message
             *    to be sent.
             */
            std::map<TrackFullNameHash, messages::RequestID> pub_namespaces_by_request_id;

            /**
             * Pending outbound publish tracks by request ID, for publish_ok.
             */
            std::map<messages::RequestID, FullTrackName> pub_by_request_id;

            /// Publish tracks by request Id. Used in client mode
            std::map<messages::RequestID, std::shared_ptr<PublishTrackHandler>> pub_tracks_by_request_id;

            /// Published tracks by quic transport data context ID.
            std::map<DataContextId, std::shared_ptr<PublishTrackHandler>> pub_tracks_by_data_ctx_id;

            /// Fetch Publishers by request ID.
            std::map<messages::RequestID, std::shared_ptr<PublishTrackHandler>> pub_fetch_tracks_by_request_id;

            /// Publish Namespace handlers by namespace.
            std::map<TrackNamespace, std::shared_ptr<PublishNamespaceHandler>> pub_namespace_handlers;

            /// Subscribe Namespace prefix by request Id
            std::map<messages::RequestID, TrackNamespace> pub_namespace_prefix_by_request_id;

            /// Subscribe Namespace handlers by namespace.
            std::map<TrackNamespace, std::shared_ptr<SubscribeNamespaceHandler>> sub_namespace_handlers;

            /// Subscribe Namespace prefix by request Id
            std::map<messages::RequestID, TrackNamespace> sub_namespace_prefix_by_request_id;

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

        void SendCtrlMsg(const ConnectionContext& conn_ctx, DataContextId data_ctx_id, BytesSpan data);
        void SendClientSetup();
        void SendServerSetup(ConnectionContext& conn_ctx);

        /*===================================================================*/
        // Requests
        /*===================================================================*/

        void SendRequestOk(ConnectionContext& conn_ctx,
                           messages::RequestID request_id,
                           std::optional<messages::Location> largest_location = std::nullopt);

        void SendRequestUpdate(const ConnectionContext& conn_ctx,
                               messages::RequestID request_id,
                               messages::RequestID existing_request_id,
                               TrackHash th,
                               std::optional<messages::GroupId> end_group_id,
                               std::uint8_t priority,
                               bool forward);

        void SendRequestError(ConnectionContext& conn_ctx,
                              messages::RequestID request_id,
                              messages::ErrorCode error,
                              std::chrono::milliseconds retry_interval,
                              const std::string& reason);

        /*===================================================================*/
        // Publish Namespace
        /*===================================================================*/

        void SendPublishNamespace(ConnectionContext& conn_ctx,
                                  messages::RequestID request_id,
                                  const TrackNamespace& track_namespace);

        void SendPublishNamespaceDone(ConnectionContext& conn_ctx, messages::RequestID request_id);

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
                           messages::RequestID request_id,
                           const FullTrackName& tfn,
                           TrackHash th,
                           std::uint8_t priority,
                           messages::GroupOrder group_order,
                           messages::FilterType filter_type,
                           std::optional<std::chrono::milliseconds> delivery_timeout);

        void SendSubscribeOk(ConnectionContext& conn_ctx,
                             messages::RequestID request_id,
                             uint64_t track_alias,
                             uint64_t expires,
                             const std::optional<messages::Location>& largest_location);
        void SendUnsubscribe(ConnectionContext& conn_ctx, messages::RequestID request_id);

        /*===================================================================*/
        // Publish
        /*===================================================================*/

        void SendPublish(ConnectionContext& conn_ctx,
                         messages::RequestID request_id,
                         const FullTrackName& tfn,
                         uint64_t track_alias,
                         messages::GroupOrder group_order,
                         std::optional<messages::Location> largest_location,
                         bool forward,
                         bool support_new_group);

        void SendPublishDone(ConnectionContext& conn_ctx,
                             messages::RequestID request_id,
                             messages::PublishDoneStatusCode status,
                             const std::string& reason);

        void SendPublishOk(ConnectionContext& conn_ctx,
                           messages::RequestID request_id,
                           bool forward,
                           std::uint8_t priority,
                           messages::GroupOrder group_order,
                           messages::FilterType filter_type);

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
                       messages::GroupOrder group_order,
                       const messages::Location& start_location,
                       const messages::FetchEndLocation& end_location);

        void SendJoiningFetch(ConnectionContext& conn_ctx,
                              messages::RequestID request_id,
                              std::uint8_t priority,
                              messages::GroupOrder group_order,
                              messages::RequestID joining_request_id,
                              messages::GroupId joining_start,
                              bool absolute);

        void SendFetchCancel(ConnectionContext& conn_ctx, messages::RequestID request_id);

        void SendFetchOk(ConnectionContext& conn_ctx,
                         messages::RequestID request_id,
                         messages::GroupOrder group_order,
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
                                  bool remove_handler = true);

        std::shared_ptr<PublishTrackHandler> GetPubTrackHandler(ConnectionContext& conn_ctx, TrackHash& th);

        void RemoveAllTracksForConnectionClose(ConnectionContext& conn_ctx);

        uint64_t GetNextRequestId();

        bool OnRecvSubgroup(messages::StreamHeaderType type,
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

        // -------------------------------------------------------------------------------------------------
        // Private member functions that will be implemented by Server class
        // -------------------------------------------------------------------------------------------------
        virtual void NewConnectionAccepted(ConnectionHandle, const ConnectionRemoteInfo&) {}

        virtual void ConnectionStatusChanged(ConnectionHandle, ConnectionStatus) {}

        virtual void SetConnectionHandle(ConnectionHandle) {}

        virtual void MetricsSampled(ConnectionHandle, const ConnectionMetrics&) {}

        // -------------------------------------------------------------------------------------------------
        // Private member functions that will be implemented by Client class
        // -------------------------------------------------------------------------------------------------
        virtual void MetricsSampled(const ConnectionMetrics&) {}

      public:
        /**
         * @brief Set the WebTransport flag for a connection
         * @param conn_id Connection ID
         * @param is_webtransport True if this is a WebTransport connection
         */
        void SetWebTransportMode(ConnectionHandle conn_id, bool is_webtransport);

      protected:
        std::shared_ptr<Transport> GetSharedPtr();

        ConnectionContext& GetConnectionContext(ConnectionHandle conn);

        // -------------------------------------------------------------------------------------------------

      private:
        // -------------------------------------------------------------------------------------------------
        // Private member functions that will be implemented by both Server and Client
        // ------------------------------------------------------------------------------------------------

        virtual bool ProcessCtrlMessage(ConnectionContext& conn_ctx,
                                        uint64_t data_ctx_id,
                                        messages::ControlMessageType msg_type,
                                        BytesSpan msg_bytes) = 0;

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

        std::shared_ptr<TickService> tick_service_;
        std::shared_ptr<ITransport> quic_transport_; // **MUST** be last for proper order of destruction

        friend class Client;
        friend class Server;
        friend class PublishTrackHandler;
        friend class PublishFetchHandler;
        friend class SubscribeTrackHandler;
    };

} // namespace quicr
