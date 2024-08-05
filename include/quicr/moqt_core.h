/*
 *  Copyright (C) 2024
 *  Cisco Systems, Inc.
 *  All Rights Reserved
 */

#pragma once

#include <quicr/metrics_exporter.h>
#include <quicr/quicr_common.h>
#include <transport/transport.h>

#include <quicr/moqt_base_track_handler.h>
#include <quicr/moqt_client_callbacks.h>
#include <quicr/moqt_config.h>
#include <quicr/moqt_publish_track_handler.h>
#include <quicr/moqt_server_callbacks.h>
#include <quicr/moqt_subscribe_track_handler.h>

#include <map>
#include <string>
#include <string_view>

namespace quicr {
    using namespace qtransport;

    /**
     * @brief MOQ Implementation supporting both client and server modes
     * @details MoQ implementation is the handler for either a client or server. It can run
     *   in only one mode, client or server.
     */
    class MOQTCore : public ITransport::TransportDelegate
    {
      public:
        MOQTCore() = delete;

        enum class Status : uint8_t
        {
            READY = 0,
            NOT_READY,

            ERROR_NOT_IN_CLIENT_MODE,
            ERROR_NOT_IN_SERVER_MODE,

            INVALID_PARAMS,
            CLIENT_NOT_CONNECTED,
            CLIENT_CONNECTING,
            CLIENT_FAILED_TO_CONNECT,
        };

      protected:
        struct TrackFullName
        {
            std::span<uint8_t const> name_space;
            std::span<uint8_t const> name;
        };

        struct TrackHash
        {
            uint64_t track_namespace_hash; // 64bit hash of namespace
            uint64_t track_name_hash;      // 64bit hash of name

            uint64_t track_fullname_hash; // 62bit of namespace+name

            TrackHash(const uint64_t name_space, const uint64_t name)
            {
                track_namespace_hash = name_space;
                track_name_hash = name;
            }

            TrackHash(const TrackFullName& tfn) noexcept
            {
                track_namespace_hash = std::hash<std::string_view>{}(
                  { reinterpret_cast<const char*>(tfn.name_space.data()), tfn.name_space.size() });
                track_name_hash =
                  std::hash<std::string_view>{}({ reinterpret_cast<const char*>(tfn.name.data()), tfn.name.size() });

                track_fullname_hash = (track_namespace_hash ^ (track_name_hash << 1)) << 1 >>
                                      2; // combine and convert to 62 bits for uintVar
            }
        };

        /**
         * @brief Client mode Constructor to create the MOQ instance
         *
         * @param cfg       MoQ Instance Client Configuration
         * @param delegate  MoQ instance delegate of callbacks
         * @param logger    MoQ Log pointer to parent logger
         */
        MOQTCore(const MOQTClientConfig& cfg,
                 std::shared_ptr<MOQTClientCallbacks> delegate,
                 const cantina::LoggerPointer& logger);

        /**
         * @brief Server mode Constructor to create the MOQ instance
         *
         * @param cfg        MoQ Server Configuration
         * @param delegate   MoQ delegate of callbacks
         * @param logger     MoQ Log pointer to parent logger
         */
        MOQTCore(const MOQTServerConfig& cfg,
                 std::shared_ptr<MOQTServerCallbacks> delegate,
                 const cantina::LoggerPointer& logger);

        ~MOQTCore() = default;

        // -------------------------------------------------------------------------------------------------
        // Public API MoQ Intance API methods
        // -------------------------------------------------------------------------------------------------
      public:
        /**
         * @brief Subscribe to a track
         *
         * @param conn_id           Connection ID to send subscribe
         * @param track_delegate    Track delegate to use for track related functions and callbacks
         *
         * @returns `track_alias` if no error and nullopt on error
         */
        std::optional<uint64_t> subscribeTrack(TransportConnId conn_id,
                                               std::shared_ptr<MOQTSubscribeTrackHandler> track_delegate);

        /**
         * @brief Unsubscribe track
         *
         * @param conn_id           Connection ID to send subscribe
         * @param track_delegate    Track delegate to use for track related functions and callbacks
         */
        void unsubscribeTrack(TransportConnId conn_id, std::shared_ptr<MOQTSubscribeTrackHandler> track_delegate);

        /**
         * @brief Publish to a track
         *
         * @param conn_id           Connection ID from transport for the QUIC connection context
         * @param track_delegate    Track delegate to use for track related functions
         *                          and callbacks
         *
         * @returns `track_alias` if no error and nullopt on error
         */
        std::optional<uint64_t> publishTrack(TransportConnId conn_id,
                                             std::shared_ptr<MOQTPublishTrackHandler> track_delegate);

        /**
         * @brief Unpublish track
         *
         * @param conn_id           Connection ID from transport for the QUIC connection context
         * @param track_delegate    Track delegate used when published track
         */
        void unpublishTrack(TransportConnId conn_id, std::shared_ptr<MOQTPublishTrackHandler> track_delegate);

        /**
         * @brief Get the instance status
         *
         * @return Status indicating the state/status of the instance
         */
        Status status();

        /**
         * Stop Instance
         */
        void stop() { _stop = true; }

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

        struct ConnectionContext
        {
            TransportConnId conn_id;
            std::optional<uint64_t> ctrl_data_ctx_id;
            bool setup_complete{ false }; ///< True if both client and server setup messages have completed
            uint64_t client_version{ 0 };
            std::optional<messages::MoQMessageType>
              ctrl_msg_type_received; ///< Indicates the current message type being read

            uint64_t _sub_id{ 0 }; ///< Connection specific ID for subscribe messages

            // Track namespace/name by received subscribe IDs - Used to map published tracks to subscribes in client
            // mode
            std::map<uint64_t, std::pair<uint64_t, uint64_t>> recv_sub_id;

            // Tracks by subscribe ID
            std::map<uint64_t, std::shared_ptr<MOQTBaseTrackHandler>> tracks_by_sub_id;

            // Publish tracks by namespace and name. map[track namespace][track name] = track delegate
            std::map<uint64_t, std::map<uint64_t, std::shared_ptr<MOQTBaseTrackHandler>>> pub_tracks_by_name;
        };

        // -------------------------------------------------------------------------------------------------
        // Private methods
        // -------------------------------------------------------------------------------------------------
        void init();

        void send_ctrl_msg(const ConnectionContext& conn_ctx, std::vector<uint8_t>&& data);
        void send_client_setup();
        void send_server_setup(ConnectionContext& conn_ctx);
        void send_announce(ConnectionContext& conn_ctx, std::span<uint8_t const> track_namespace);
        void send_announce_ok(ConnectionContext& conn_ctx, std::span<uint8_t const> track_namespace);
        void send_unannounce(ConnectionContext& conn_ctx, std::span<uint8_t const> track_namespace);
        void send_subscribe(ConnectionContext& conn_ctx, uint64_t subscribe_id, TrackFullName& tfn, TrackHash th);
        void send_subscribe_ok(ConnectionContext& conn_ctx,
                               uint64_t subscribe_id,
                               uint64_t expires,
                               bool content_exists);
        void send_unsubscribe(ConnectionContext& conn_ctx, uint64_t subscribe_id);
        void send_subscribe_done(ConnectionContext& conn_ctx, uint64_t subscribe_id, const std::string& reason);
        void send_subscribe_error(ConnectionContext& conn_ctx,
                                  uint64_t subscribe_id,
                                  uint64_t track_alias,
                                  messages::MoQSubscribeError error,
                                  const std::string& reason);
        void close_connection(TransportConnId conn_id,
                              messages::MoQTerminationReason reason,
                              const std::string& reason_str);
        bool process_recv_ctrl_message(ConnectionContext& conn_ctx,
                                       std::shared_ptr<StreamBuffer<uint8_t>>& stream_buffer);
        bool process_recv_stream_data_message(ConnectionContext& conn_ctx,
                                              std::shared_ptr<StreamBuffer<uint8_t>>& stream_buffer);

        void remove_subscribeTrack(ConnectionContext& conn_ctx,
                                   MOQTSubscribeTrackHandler& delegate,
                                   bool remove_delegate = true);

        std::optional<std::weak_ptr<MOQTPublishTrackHandler>> getPubTrackDelegate(ConnectionContext& conn_ctx,
                                                                                  TrackHash& th);

        // -------------------------------------------------------------------------------------------------
        // Private member variables
        // -------------------------------------------------------------------------------------------------

        std::mutex _state_mutex;
        const bool _client_mode;
        bool _stop{ false };
        const MOQTServerConfig _server_config;
        const MOQTClientConfig _client_config;

        std::map<TransportConnId, ConnectionContext> _connections;

        Status _status{ Status::NOT_READY };

        // Log handler to use
        cantina::LoggerPointer _logger;

#ifndef LIBQUICR_WITHOUT_INFLUXDB
        MetricsExporter _mexport;
#endif

        std::shared_ptr<MOQTClientCallbacks> _client_delegate;
        std::shared_ptr<MOQTServerCallbacks> _server_delegate;
        std::shared_ptr<ITransport> _transport; // **MUST** be last for proper order of destruction
    };

} // namespace quicr
