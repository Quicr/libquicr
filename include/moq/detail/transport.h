// SPDX-FileCopyrightText: Copyright (c) 2024 Cisco Systems
// SPDX-License-Identifier: BSD-2-Clause


#pragma once

#include <moq/detail/messages.h>

#include "quic_transport.h"

#include <moq/common.h>
#include <moq/config.h>
#include <moq/metrics.h>
#include <moq/publish_track_handler.h>
#include "span.h"
#include <moq/subscribe_track_handler.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>

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

            kConnecting,
            kDisconnecting,
            kNotConnected,
            kFailedToConnect
        };

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
            std::string ip;         ///< remote IPv4/v6 address
            uint16_t port;          ///< remote port
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
        void UnsubscribeTrack(ConnectionHandle connection_handle, std::shared_ptr<SubscribeTrackHandler> track_handler);

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
         * @param track_handler    Track handler used when published track
         */
        void UnpublishTrack(ConnectionHandle connection_handle, std::shared_ptr<PublishTrackHandler> track_handler);

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
                                std::shared_ptr<PublishTrackHandler> track_handler);

        /**
         * @brief Get the status of the Client
         *
         * @return Status of the Client
         */
        Status GetStatus() const noexcept { return status_; }

        /**
         * @brief Callback notification for status/state change
         * @details Callback notification indicates state change of connection, such as disconnected
         *
         * @param status           Status change
         */
        virtual void StatusChanged(Status) {}

        // --------------------------------------------------------------------------
        // Metrics
        // --------------------------------------------------------------------------

        /**
         * @brief Connection metrics for server accepted connections
         *
         * @details Connection metrics are updated real-time and transport quic metrics on
         *      Config::metrics_sample_ms period
         */
        std::map<ConnectionHandle, ConnectionMetrics> connection_metrics_;

      protected:
        Status Start();
        Status Stop();

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

        // -------------------------------------------------------------------------------------------------
        // End of transport handler/callback functions
        // -------------------------------------------------------------------------------------------------

        struct ConnectionContext
        {
            ConnectionHandle connection_handle;
            std::optional<uint64_t> ctrl_data_ctx_id;
            bool setup_complete{ false }; ///< True if both client and server setup messages have completed
            uint64_t client_version{ 0 };
            std::optional<messages::MoqMessageType>
              ctrl_msg_type_received; ///< Indicates the current message type being read

            uint64_t current_subscribe_id{ 0 }; ///< Connection specific ID for subscribe messages

            /// Track namespace/name by received subscribe IDs
            /// Used to map published tracks to subscribes in client mode
            std::map<uint64_t, std::pair<uint64_t, uint64_t>> recv_sub_id;

            /// Tracks by subscribe ID
            std::map<uint64_t, std::shared_ptr<SubscribeTrackHandler>> tracks_by_sub_id;

            /// Publish tracks by namespace and name. map[track namespace][track name] = track handler
            std::map<uint64_t, std::map<uint64_t, std::shared_ptr<PublishTrackHandler>>> pub_tracks_by_name;
        };

        // -------------------------------------------------------------------------------------------------
        // Private methods
        // -------------------------------------------------------------------------------------------------

        void Init();

        PublishTrackHandler::PublishObjectStatus SendObject(std::weak_ptr<PublishTrackHandler> track_handler,
                                                            uint8_t priority,
                                                            uint32_t ttl,
                                                            bool stream_header_needed,
                                                            uint64_t group_id,
                                                            uint64_t object_id,
                                                            BytesSpan data);

        void SendCtrlMsg(const ConnectionContext& conn_ctx, std::vector<uint8_t>&& data);
        void SendClientSetup();
        void SendServerSetup(ConnectionContext& conn_ctx);
        void SendAnnounce(ConnectionContext& conn_ctx, Span<uint8_t const> track_namespace);
        void SendAnnounceOk(ConnectionContext& conn_ctx, Span<uint8_t const> track_namespace);
        void SendUnannounce(ConnectionContext& conn_ctx, Span<uint8_t const> track_namespace);
        void SendSubscribe(ConnectionContext& conn_ctx, uint64_t subscribe_id, const FullTrackName& tfn, TrackHash th);
        void SendSubscribeOk(ConnectionContext& conn_ctx, uint64_t subscribe_id, uint64_t expires, bool content_exists);
        void SendUnsubscribe(ConnectionContext& conn_ctx, uint64_t subscribe_id);
        void SendSubscribeDone(ConnectionContext& conn_ctx, uint64_t subscribe_id, const std::string& reason);
        void SendSubscribeError(ConnectionContext& conn_ctx,
                                uint64_t subscribe_id,
                                uint64_t track_alias,
                                messages::SubscribeError error,
                                const std::string& reason);
        void CloseConnection(ConnectionHandle connection_handle,
                             messages::MoqTerminationReason reason,
                             const std::string& reason_str);

        void RemoveSubscribeTrack(ConnectionContext& conn_ctx,
                                  SubscribeTrackHandler& handler,
                                  bool remove_handler = true);

        std::optional<std::weak_ptr<PublishTrackHandler>> GetPubTrackHandler(ConnectionContext& conn_ctx,
                                                                             TrackHash& th);

        // -------------------------------------------------------------------------------------------------
        // Private member functions that will be implemented by Server class
        // -------------------------------------------------------------------------------------------------
        virtual void NewConnectionAccepted(ConnectionHandle,
                                           const ConnectionRemoteInfo&) {};

        virtual void ConnectionStatusChanged(ConnectionHandle, ConnectionStatus) {};

        virtual void SetConnectionHandle(ConnectionHandle) {};

        // -------------------------------------------------------------------------------------------------

      private:
        // -------------------------------------------------------------------------------------------------
        // Private member functions that will be implemented by both Server and Client
        // ------------------------------------------------------------------------------------------------
        virtual bool ProcessCtrlMessage(ConnectionContext& conn_ctx,
                                        std::shared_ptr<StreamBuffer<uint8_t>>& stream_buffer) = 0;

        bool ProcessStreamDataMessage(ConnectionContext& conn_ctx,
                                      std::shared_ptr<StreamBuffer<uint8_t>>& stream_buffer);

        template<class MessageType>
        std::pair<MessageType&, bool> ParseControlMessage(std::shared_ptr<StreamBuffer<uint8_t>>& stream_buffer);

        template<class MessageType>
        std::pair<MessageType&, bool> ParseDataMessage(std::shared_ptr<StreamBuffer<uint8_t>>& stream_buffer, messages::MoqMessageType msg_type);

        template<class HeaderType, class MessageType>
        std::pair<HeaderType&, bool> ParseStreamData(
          std::shared_ptr<StreamBuffer<uint8_t>>& stream_buffer,
          messages::MoqMessageType msg_type,
          const ConnectionContext& conn_ctx);

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

        std::shared_ptr<ITransport> quic_transport_; // **MUST** be last for proper order of destruction

        friend class Client;
        friend class Server;
    };

} // namespace moq
