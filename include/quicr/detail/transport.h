// SPDX-FileCopyrightText: Copyright (c) 2024 Cisco Systems
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

#include "ctrl_messages.h"
#include "messages.h"
#include "tick_service.h"

#include "quic_transport.h"

#include "span.h"
#include <chrono>
#include <quicr/common.h>
#include <quicr/config.h>
#include <quicr/fetch_track_handler.h>
#include <quicr/metrics.h>
#include <quicr/publish_track_handler.h>
#include <quicr/subscribe_track_handler.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>

#include <map>
#include <string>
#include <string_view>

namespace quicr {

    /**
     * @brief MOQ Implementation supporting both client and server modes
     * @details MoQ implementation is the handler for either a client or server. It can run
     *   in only one mode, client or server.
     */
    class Transport : public ITransport::TransportDelegate
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
            kPendingSeverSetup,
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
            ConnectionHandle connection_handle{ 0 };
            std::optional<uint64_t> ctrl_data_ctx_id;
            bool setup_complete{ false }; ///< True if both client and server setup messages have completed
            uint64_t client_version{ 0 };
            std::optional<quicr::ctrl_messages::ControlMessageType>
              ctrl_msg_type_received; ///< Indicates the current message type being read

            std::vector<uint8_t> ctrl_msg_buffer; ///< Control message buffer

            uint64_t current_subscribe_id{ 0 }; ///< Connection specific ID for subscribe messages

            /// Subscribe Context by received subscribe IDs
            /// Used to map published tracks to subscribes in client mode and to handle joining fetch lookups
            struct SubscribeContext
            {
                FullTrackName track_full_name;
                std::optional<quicr::ctrl_messages::GroupId> largest_group_id{ std::nullopt };
                std::optional<quicr::ctrl_messages::ObjectId> largest_object_id{ std::nullopt };
            };
            std::map<messages::SubscribeId, SubscribeContext> recv_sub_id;

            /// Tracks by subscribe ID (Subscribe and Fetch)
            std::map<messages::SubscribeId, std::shared_ptr<SubscribeTrackHandler>> tracks_by_sub_id;

            /// Subscribes by Track Alais is used for data object forwarding
            std::map<messages::TrackAlias, std::shared_ptr<SubscribeTrackHandler>> sub_by_track_alias;

            /// Publish tracks by namespace and name. map[track namespace][track name] = track handler
            std::map<TrackNamespaceHash, std::map<TrackNameHash, std::shared_ptr<PublishTrackHandler>>>
              pub_tracks_by_name;

            std::map<messages::TrackAlias, std::shared_ptr<PublishTrackHandler>> pub_tracks_by_track_alias;

            /// Published tracks by quic transport data context ID.
            std::map<DataContextId, std::shared_ptr<PublishTrackHandler>> pub_tracks_by_data_ctx_id;

            /// Fetch Publishers by subscribe ID.
            std::map<messages::SubscribeId, std::shared_ptr<PublishTrackHandler>> pub_fetch_tracks_by_sub_id;

            ConnectionMetrics metrics{}; ///< Connection metrics

            ConnectionContext() { ctrl_msg_buffer.reserve(kControlMessageBufferSize); }
        };

        // -------------------------------------------------------------------------------------------------
        // Private methods
        // -------------------------------------------------------------------------------------------------

        void Init();

        PublishTrackHandler::PublishObjectStatus SendData(PublishTrackHandler& track_handler,
                                                          uint8_t priority,
                                                          uint32_t ttl,
                                                          bool stream_header_needed,
                                                          std::shared_ptr<const std::vector<uint8_t>> data);

        PublishTrackHandler::PublishObjectStatus SendObject(PublishTrackHandler& track_handler,
                                                            uint8_t priority,
                                                            uint32_t ttl,
                                                            bool stream_header_needed,
                                                            uint64_t group_id,
                                                            uint64_t subgroup_id,
                                                            uint64_t object_id,
                                                            std::optional<Extensions> extensions,
                                                            BytesSpan data);

        void SendCtrlMsg(const ConnectionContext& conn_ctx, BytesSpan data);
        void SendClientSetup();
        void SendServerSetup(ConnectionContext& conn_ctx);
        void SendAnnounce(ConnectionContext& conn_ctx, const TrackNamespace& track_namespace);
        void SendAnnounceOk(ConnectionContext& conn_ctx, const TrackNamespace& track_namespace);
        void SendUnannounce(ConnectionContext& conn_ctx, const TrackNamespace& track_namespace);
        void SendSubscribe(ConnectionContext& conn_ctx,
                           uint64_t subscribe_id,
                           const FullTrackName& tfn,
                           TrackHash th,
                           quicr::ctrl_messages::SubscriberPriority priority,
                           quicr::ctrl_messages::GroupOrder group_order,
                           quicr::ctrl_messages::FilterType filter_type);
        void SendSubscribeUpdate(ConnectionContext& conn_ctx,
                                 uint64_t subscribe_id,
                                 TrackHash th,
                                 quicr::ctrl_messages::GroupId start_group_id,
                                 quicr::ctrl_messages::ObjectId start_object_id,
                                 quicr::ctrl_messages::GroupId end_group_id,
                                 quicr::ctrl_messages::SubscriberPriority priority);

        void SendSubscribeOk(ConnectionContext& conn_ctx,
                             uint64_t subscribe_id,
                             uint64_t expires,
                             bool content_exists,
                             quicr::ctrl_messages::GroupId largest_group_id,
                             quicr::ctrl_messages::ObjectId largest_object_id);
        void SendUnsubscribe(ConnectionContext& conn_ctx, uint64_t subscribe_id);
        void SendSubscribeDone(ConnectionContext& conn_ctx, uint64_t subscribe_id, const std::string& reason);
        void SendSubscribeError(ConnectionContext& conn_ctx,
                                uint64_t subscribe_id,
                                uint64_t track_alias,
                                quicr::ctrl_messages::SubscribeErrorCodeEnum error,
                                const std::string& reason);

        void SendSubscribeAnnounces(ConnectionHandle conn_handle, const TrackNamespace& prefix_namespace);
        void SendUnsubscribeAnnounces(ConnectionHandle conn_handle, const TrackNamespace& prefix_namespace);
        void SendSubscribeAnnouncesOk(ConnectionContext& conn_ctx, const TrackNamespace& prefix_namespace);
        void SendSubscribeAnnouncesError(ConnectionContext& conn_ctx,
                                         const TrackNamespace& prefix_namespace,
                                         quicr::ctrl_messages::SubscribeAnnouncesErrorCodeEnum err_code,
                                         const messages::ReasonPhrase& reason);

        void SendFetch(ConnectionContext& conn_ctx,
                       uint64_t subscribe_id,
                       const FullTrackName& tfn,
                       quicr::ctrl_messages::SubscriberPriority priority,
                       quicr::ctrl_messages::GroupOrder group_order,
                       quicr::ctrl_messages::GroupId start_group,
                       quicr::ctrl_messages::GroupId start_object,
                       quicr::ctrl_messages::GroupId end_group,
                       quicr::ctrl_messages::GroupId end_object);
        void SendJoiningFetch(ConnectionContext& conn_ctx,
                              uint64_t subscribe_id,
                              quicr::ctrl_messages::SubscriberPriority priority,
                              quicr::ctrl_messages::GroupOrder group_order,
                              uint64_t joining_subscribe_id,
                              quicr::ctrl_messages::GroupId preceding_group_offset,
                              const quicr::ctrl_messages::Parameters parameters);
        void SendFetchCancel(ConnectionContext& conn_ctx, uint64_t subscribe_id);
        void SendFetchOk(ConnectionContext& conn_ctx,
                         uint64_t subscribe_id,
                         quicr::ctrl_messages::GroupOrder group_order,
                         bool end_of_track,
                         quicr::ctrl_messages::GroupId largest_group_id,
                         quicr::ctrl_messages::GroupId largest_object_id);
        void SendFetchError(ConnectionContext& conn_ctx,
                            uint64_t subscribe_id,
                            quicr::ctrl_messages::FetchErrorCodeEnum error,
                            const std::string& reason);
        void CloseConnection(ConnectionHandle connection_handle,
                             quicr::ctrl_messages::TerminationReasonEnum reason,
                             const std::string& reason_str);

        void RemoveSubscribeTrack(ConnectionContext& conn_ctx,
                                  SubscribeTrackHandler& handler,
                                  bool remove_handler = true);

        void SendNewGroupRequest(ConnectionHandle conn_id, uint64_t subscribe_id, uint64_t track_alias);

        std::shared_ptr<PublishTrackHandler> GetPubTrackHandler(ConnectionContext& conn_ctx, TrackHash& th);

        void RemoveAllTracksForConnectionClose(ConnectionContext& conn_ctx);

        bool OnRecvSubgroup(std::vector<uint8_t>::const_iterator cursor_it,
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

        // -------------------------------------------------------------------------------------------------

      private:
        // -------------------------------------------------------------------------------------------------
        // Private member functions that will be implemented by both Server and Client
        // ------------------------------------------------------------------------------------------------

        virtual bool ProcessCtrlMessage(ConnectionContext& conn_ctx, BytesSpan msg_bytes) = 0;

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
    };

} // namespace quicr
