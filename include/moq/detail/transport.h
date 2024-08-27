/*
 *  Copyright (C) 2024
 *  Cisco Systems, Inc.
 *  All Rights Reserved
 */

#pragma once

#include <transport/transport.h>

#include <moq/common.h>
#include <moq/config.h>
#include <moq/detail/messages.h>
#include <moq/metrics.h>
#include <moq/publish_track_handler.h>
#include <moq/subscribe_track_handler.h>
#include <moq/server_publish_track_handler.h>
#include <transport/span.h>

#include <map>
#include <string>
#include <string_view>

namespace moq {

    /**
     * @brief MOQ Implementation supporting both client and server modes
     * @details MoQ implementation is the handler for either a client or server. It can run
     *   in only one mode, client or server.
     */
    class Transport : public ITransport::TransportDelegate
    {
      public:
        Transport() = delete;

        enum class Status : uint8_t
        {
            kReady = 0,
            kNotReady,

            kInternalError,

            kInvalidParams,

            kClientConnecting,
            kDisconnecting,
            kClientNotConnected,
            kClientFailedToConnect
        };

        /**
         * @brief Client mode Constructor to create the MOQ instance
         *
         * @param cfg       MoQ Instance Client Configuration
         */
        Transport(const ClientConfig& cfg);

        /**
         * @brief Server mode Constructor to create the MOQ instance
         *
         * @param cfg        MoQ Server Configuration
         */
        Transport(const ServerConfig& cfg);

        ~Transport() = default;

        // -------------------------------------------------------------------------------------------------
        // Public API MoQ Instance API methods
        // -------------------------------------------------------------------------------------------------
        /**
         * @brief Subscribe to a track
         *
         * @param conn_id           Connection ID to send subscribe
         * @param track_delegate    Track delegate to use for track related functions and callbacks
         *
         */
        void SubscribeTrack(TransportConnId conn_id, std::shared_ptr<SubscribeTrackHandler> track_delegate);

        /**
         * @brief Unsubscribe track
         *
         * @param conn_id           Connection ID to send subscribe
         * @param track_delegate    Track delegate to use for track related functions and callbacks
         */
        void UnsubscribeTrack(TransportConnId conn_id, std::shared_ptr<SubscribeTrackHandler> track_delegate);

        /**
         * @brief Publish to a track
         *
         * @param conn_id           Connection ID from transport for the QUIC connection context
         * @param track_delegate    Track delegate to use for track related functions
         *                          and callbacks
         */
        void PublishTrack(TransportConnId conn_id, std::shared_ptr<PublishTrackHandler> track_delegate);

        /**
         * @brief Unpublish track
         *
         * @param conn_id           Connection ID from transport for the QUIC connection context
         * @param track_delegate    Track delegate used when published track
         */
        void UnpublishTrack(TransportConnId conn_id, std::shared_ptr<PublishTrackHandler> track_delegate);

        /**
         * @brief Get the instance status
         *
         * @return Status indicating the state/status of the instance
         */
        Status GetStatus();

        // --------------------------------------------------------------------------
        // Metrics
        // --------------------------------------------------------------------------

        /**
         * @brief Connection metrics for server accepted connections
         *
         * @details Connection metrics are updated real-time and transport quic metrics on
         *      Config::metrics_sample_ms period
         */
        std::map<TransportConnId, ConnectionMetrics> connection_metrics_;

      private:
        // -------------------------------------------------------------------------------------------------
        // Transport Delegate/callback functions
        // -------------------------------------------------------------------------------------------------

        void on_new_data_context([[maybe_unused]] const TransportConnId& conn_id,
                                 [[maybe_unused]] const DataContextId& data_ctx_id) override
        {
        }

        void on_connection_status(const TransportConnId& conn_id, const TransportStatus status) override;
        void on_new_connection(const TransportConnId& conn_id, const TransportRemote& remote) override;
        void on_recv_stream(const TransportConnId& conn_id,
                            uint64_t stream_id,
                            std::optional<DataContextId> data_ctx_id,
                            const bool is_bidir = false) override;
        void on_recv_dgram(const TransportConnId& conn_id, std::optional<DataContextId> data_ctx_id) override;

        // -------------------------------------------------------------------------------------------------
        // End of transport delegate/callback functions
        // -------------------------------------------------------------------------------------------------

        struct ConnectionContext
        {
            TransportConnId conn_id;
            std::optional<uint64_t> ctrl_data_ctx_id;
            bool setup_complete{ false }; ///< True if both client and server setup messages have completed
            uint64_t client_version{ 0 };
            std::optional<messages::MoqtMessageType>
              ctrl_msg_type_received; ///< Indicates the current message type being read

            uint64_t current_subscribe_id{ 0 }; ///< Connection specific ID for subscribe messages

            /// Track namespace/name by received subscribe IDs
            /// Used to map published tracks to subscribes in client mode
            std::map<uint64_t, std::pair<uint64_t, uint64_t>> recv_sub_id;

            /// Tracks by subscribe ID
            std::map<uint64_t, std::shared_ptr<BaseTrackHandler>> tracks_by_sub_id;

            /// Publish tracks by namespace and name. map[track namespace][track name] = track delegate
            std::map<uint64_t, std::map<uint64_t, std::shared_ptr<BaseTrackHandler>>> pub_tracks_by_name;
        };

        // -------------------------------------------------------------------------------------------------
        // Private methods
        // -------------------------------------------------------------------------------------------------
        void Init();

        void SendCtrlMsg(const ConnectionContext& conn_ctx, std::vector<uint8_t>&& data);
        void SendClientSetup();
        void SendServerSetup(ConnectionContext& conn_ctx);
        void SendAnnounce(ConnectionContext& conn_ctx, Span<uint8_t const> track_namespace);
        void SendAnnounceOk(ConnectionContext& conn_ctx, Span<uint8_t const> track_namespace);
        void SendUnannounce(ConnectionContext& conn_ctx, Span<uint8_t const> track_namespace);
        void SendSubscribe(ConnectionContext& conn_ctx, uint64_t subscribe_id, FullTrackName& tfn, TrackHash th);
        void SendSubscribeOk(ConnectionContext& conn_ctx, uint64_t subscribe_id, uint64_t expires, bool content_exists);
        void SendUnsubscribe(ConnectionContext& conn_ctx, uint64_t subscribe_id);
        void SendSubscribeDone(ConnectionContext& conn_ctx, uint64_t subscribe_id, const std::string& reason);
        void SendSubscribeError(ConnectionContext& conn_ctx,
                                uint64_t subscribe_id,
                                uint64_t track_alias,
                                messages::MoqtSubscribeError error,
                                const std::string& reason);
        void CloseConnection(TransportConnId conn_id,
                             messages::MoqtTerminationReason reason,
                             const std::string& reason_str);
        bool ProcessRecvCtrlMessage(ConnectionContext& conn_ctx, std::shared_ptr<StreamBuffer<uint8_t>>& stream_buffer);
        bool ProcessRecvStreamDataMessage(ConnectionContext& conn_ctx,
                                          std::shared_ptr<StreamBuffer<uint8_t>>& stream_buffer);

        void RemoveSubscribeTrack(ConnectionContext& conn_ctx,
                                  SubscribeTrackHandler& delegate,
                                  bool remove_delegate = true);

        std::optional<std::weak_ptr<PublishTrackHandler>> GetPubTrackHandler(ConnectionContext& conn_ctx,
                                                                             TrackHash& th);

        // -------------------------------------------------------------------------------------------------
        // Private member variables
        // -------------------------------------------------------------------------------------------------

        std::mutex state_mutex_;
        const bool client_mode_;
        bool stop_{ false };
        const ServerConfig server_config_;
        const ClientConfig client_config_;

        std::map<TransportConnId, ConnectionContext> connections_;

        Status status_{ Status::kNotReady };

        std::shared_ptr<ITransport> quic_transport_; // **MUST** be last for proper order of destruction
    };

} // namespace moq
