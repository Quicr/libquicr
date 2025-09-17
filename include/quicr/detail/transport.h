// SPDX-FileCopyrightText: Copyright (c) 2024 Cisco Systems
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

#include "attributes.h"
#include "messages.h"
#include "tick_service.h"

#include "quic_transport.h"

#include <chrono>
#include <quicr/common.h>
#include <quicr/config.h>
#include <quicr/fetch_track_handler.h>
#include <quicr/metrics.h>
#include <quicr/publish_track_handler.h>
#include <quicr/subscribe_track_handler.h>
#include <span>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>

#include <atomic>
#include <map>
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
         * @param new_group_request         True to add new group request parameter
         *
         */
        void UpdateTrackSubscription(ConnectionHandle connection_handle,
                                     std::shared_ptr<SubscribeTrackHandler> track_handler,
                                     bool new_group_request = false);

        /**
         * @brief Publish to a track
         *
         * @param connection_handle           Connection ID from transport for the QUIC connection context
         * @param track_handler               Track handler to use for track related functions
         *                                    and callbacks
         */
        void PublishTrack(ConnectionHandle connection_handle, std::shared_ptr<PublishTrackHandler> track_handler);

        /**
         * @brief Publish to a track and force subscribe
         *
         * @param connection_handle           Connection ID from transport for the QUIC connection context
         * @param track_handler               Track handler to use for track related functions
         *                                    and callbacks
         */
        void PublishTrackSub(ConnectionHandle connection_handle, std::shared_ptr<PublishTrackHandler> track_handler);

        /**
         * @brief Unpublish track
         *
         * @param connection_handle           Connection ID from transport for the QUIC connection context
         * @param track_handler               Track handler used when published track
         */
        void UnpublishTrack(ConnectionHandle connection_handle,
                            const std::shared_ptr<PublishTrackHandler>& track_handler);

        /**
         * @brief Event to run on receiving Fetch request.
         *
         * @param connection_handle Source connection ID.
         * @param request_id        Request ID received.
         * @param track_full_name   Track full name
         * @param attributes        Fetch attributes received.
         *
         * @returns True to indicate fetch will send data, False if no data is within the requested range
         */
        virtual bool FetchReceived(ConnectionHandle connection_handle,
                                   uint64_t request_id,
                                   const FullTrackName& track_full_name,
                                   const quicr::messages::FetchAttributes& attributes);

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
         *      will send the protocol message based on the SubscribeResponse. Per MOQT draft-14,
         *      track status request, ok, and error are the same as subscribe
         *
         * @param connection_handle        source connection ID
         * @param request_id               Request ID that was provided by TrackStatusReceived
         * @param track_alias              Track alias for the track
         * @param subscribe_response       Response to the track status request, either Ok or Error.
         *                                 Largest loation should be set if kOk and there is content
         */
        virtual void ResolveTrackStatus(ConnectionHandle connection_handle,
                                        uint64_t request_id,
                                        uint64_t track_alias,
                                        const SubscribeResponse& subscribe_response);

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
         * @param subscribe_attributes  Subscribe attributes received
         */
        virtual void TrackStatusReceived(ConnectionHandle connection_handle,
                                         uint64_t request_id,
                                         const FullTrackName& track_full_name,
                                         const messages::SubscribeAttributes& subscribe_attributes);

        /**
         * @brief Callback notification for track status OK received
         *
         * @note The caller is able to state track the OK based on the request Id returned
         *      from RequestTrackStatus method call
         *
         * @param connection_handle     Source connection ID
         * @param request_id            Request ID received
         * @param response              Track status (track_status = Subscribe) response
         */
        virtual void TrackStatusResponseReceived(ConnectionHandle connection_handle,
                                                 uint64_t request_id,
                                                 const SubscribeResponse& response);

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
            bool setup_complete{ false }; ///< True if both client and server setup messages have completed
            bool closed{ false };
            uint64_t client_version{ 0 };
            std::optional<messages::ControlMessageType>
              ctrl_msg_type_received; ///< Indicates the current message type being read

            std::vector<uint8_t> ctrl_msg_buffer; ///< Control message buffer

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

            /// Publish tracks by request Id. Used in client mode
            std::map<messages::RequestID, std::shared_ptr<PublishTrackHandler>> pub_tracks_by_request_id;

            /// Published tracks by quic transport data context ID.
            std::map<DataContextId, std::shared_ptr<PublishTrackHandler>> pub_tracks_by_data_ctx_id;

            /// Fetch Publishers by subscribe ID.
            std::map<messages::RequestID, std::shared_ptr<PublishTrackHandler>> pub_fetch_tracks_by_sub_id;

            /// Subscribe Announces namespace prefix by request Id
            std::map<messages::RequestID, TrackNamespace> sub_announces_by_request_id;

            ConnectionMetrics metrics{}; ///< Connection metrics

            ConnectionContext() { ctrl_msg_buffer.reserve(kControlMessageBufferSize); }

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

        void SendCtrlMsg(const ConnectionContext& conn_ctx, BytesSpan data);
        void SendClientSetup();
        void SendServerSetup(ConnectionContext& conn_ctx);
        void SendAnnounce(ConnectionContext& conn_ctx,
                          messages::RequestID request_id,
                          const TrackNamespace& track_namespace);
        void SendAnnounceOk(ConnectionContext& conn_ctx, messages::RequestID request_id);
        void SendUnannounce(ConnectionContext& conn_ctx, const TrackNamespace& track_namespace);
        void SendSubscribe(ConnectionContext& conn_ctx,
                           messages::RequestID request_id,
                           const FullTrackName& tfn,
                           TrackHash th,
                           messages::SubscriberPriority priority,
                           messages::GroupOrder group_order,
                           messages::FilterType filter_type,
                           std::chrono::milliseconds delivery_timeout);
        void SendSubscribeUpdate(const ConnectionContext& conn_ctx,
                                 messages::RequestID request_id,
                                 messages::RequestID subscribe_request_id,
                                 TrackHash th,
                                 messages::Location start_location,
                                 messages::GroupId end_group_id,
                                 messages::SubscriberPriority priority,
                                 bool forward,
                                 bool new_group_request = false);

        void SendSubscribeOk(ConnectionContext& conn_ctx,
                             messages::RequestID request_id,
                             uint64_t track_alias,
                             uint64_t expires,
                             const std::optional<messages::Location>& largest_location);
        void SendUnsubscribe(ConnectionContext& conn_ctx, messages::RequestID request_id);
        void SendSubscribeDone(ConnectionContext& conn_ctx, messages::RequestID request_id, const std::string& reason);
        void SendSubscribeError(ConnectionContext& conn_ctx,
                                messages::RequestID request_id,
                                messages::SubscribeErrorCode error,
                                const std::string& reason);

        void SendTrackStatus(ConnectionContext& conn_ctx, messages::RequestID request_id, const FullTrackName& tfn);
        void SendTrackStatusOk(ConnectionContext& conn_ctx,
                               messages::RequestID request_id,
                               uint64_t track_alias,
                               uint64_t expires,
                               const std::optional<messages::Location>& largest_location);
        void SendTrackStatusError(ConnectionContext& conn_ctx,
                                  messages::RequestID request_id,
                                  messages::SubscribeErrorErrorCode error,
                                  const std::string& reason);

        void SendPublish(ConnectionContext& conn_ctx,
                         messages::RequestID request_id,
                         const FullTrackName& tfn,
                         uint64_t track_alias,
                         messages::GroupOrder group_order,
                         std::optional<messages::Location> largest_location,
                         bool forward);

        void SendPublishOk(ConnectionContext& conn_ctx,
                           messages::RequestID request_id,
                           bool forward,
                           messages::SubscriberPriority priority,
                           messages::GroupOrder group_order,
                           messages::FilterType filter_type);

        void SendPublishError(ConnectionContext& conn_ctx,
                              messages::RequestID request_id,
                              messages::SubscribeErrorCode error,
                              const std::string& reason);

        void SendSubscribeAnnounces(ConnectionHandle conn_handle, const TrackNamespace& prefix_namespace);
        void SendUnsubscribeAnnounces(ConnectionHandle conn_handle, const TrackNamespace& prefix_namespace);
        void SendSubscribeAnnouncesOk(ConnectionContext& conn_ctx, messages::RequestID request_id);
        void SendSubscribeAnnouncesError(ConnectionContext& conn_ctx,
                                         messages::RequestID request_id,
                                         messages::SubscribeNamespaceErrorCode err_code,
                                         const messages::ReasonPhrase& reason);

        void SendFetch(ConnectionContext& conn_ctx,
                       messages::RequestID request_id,
                       const FullTrackName& tfn,
                       messages::SubscriberPriority priority,
                       messages::GroupOrder group_order,
                       messages::GroupId start_group,
                       messages::GroupId start_object,
                       messages::GroupId end_group,
                       messages::GroupId end_object);
        void SendJoiningFetch(ConnectionContext& conn_ctx,
                              messages::RequestID request_id,
                              messages::SubscriberPriority priority,
                              messages::GroupOrder group_order,
                              messages::RequestID joining_request_id,
                              messages::GroupId joining_start,
                              bool absolute,
                              const messages::Parameters parameters);
        void SendFetchCancel(ConnectionContext& conn_ctx, messages::RequestID request_id);
        void SendFetchOk(ConnectionContext& conn_ctx,
                         messages::RequestID request_id,
                         messages::GroupOrder group_order,
                         bool end_of_track,
                         messages::Location end_location);
        void SendFetchError(ConnectionContext& conn_ctx,
                            messages::RequestID request_id,
                            messages::FetchErrorCode error,
                            const std::string& reason);
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

      protected:
        std::shared_ptr<Transport> GetSharedPtr();

        ConnectionContext& GetConnectionContext(ConnectionHandle conn);

        // -------------------------------------------------------------------------------------------------

      private:
        // -------------------------------------------------------------------------------------------------
        // Private member functions that will be implemented by both Server and Client
        // ------------------------------------------------------------------------------------------------

        virtual bool ProcessCtrlMessage(ConnectionContext& conn_ctx, BytesSpan msg_bytes) = 0;

        TransportError Enqueue(const TransportConnId& conn_id,
                               const DataContextId& data_ctx_id,
                               std::uint64_t group_id,
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
