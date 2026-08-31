// SPDX-FileCopyrightText: Copyright (c) 2024 Cisco Systems
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

#include "picoquic_connection.h"
#include "quicr/containers/priority_queue.h"
#include "quicr/containers/safe_queue.h"
#include "quicr/containers/safe_time_queue.h"
#include "quicr/containers/stream_buffer.h"
#include "quicr/log.h"
#include "quicr/metrics.h"
#include "quicr/transport.h"

#include <h3zero.h>
#include <h3zero_common.h>
#include <pico_webtransport.h>
#include <picoquic.h>
#include <picoquic_config.h>
#include <picoquic_packet_loop.h>
#include <timeq/time_queue.h>
#include <tls_api.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <netinet/in.h>
#include <queue>
#include <span>
#include <string>
#include <sys/socket.h>
#include <sys/types.h>
#include <thread>
#include <vector>

namespace quicr {

    constexpr int kPqLoopMaxDelayUs = 5000;           /// The max microseconds that pq_loop will be ran again
    constexpr int kPqCcLowCwin = 4000;                /// Bytes less than this value are considered a low/congested CWIN
    constexpr int kCongestionCheckInterval = 100'000; /// Congestion check interval in microseconds

    /**
     * Minimum bytes needed to write before considering to send. This doesn't
     */
    constexpr int kMinStreamBytesForSend = 2;

    class PicoQuicTransport : public Transport
    {
      public:
        const char* webtransport_alpn = "h3";

        /**
         */
        enum class StreamErrorCodes : uint32_t
        {
            kInternalError = 20,
            kUnknownExpiry = 50
        };

        /**
         * @brief A request from another thread to mark a stream active on the picoquic thread
         *
         * @details Weak so that queuing a mark cannot keep a torn-down stream or connection alive,
         *      and so the queue can outlive them. `ProcessMarkActive` drops entries whose stream is
         *      gone or already closed.
         */
        struct StreamMarkActiveInfo
        {
            std::weak_ptr<PicoQuicConnection> connection;
            std::weak_ptr<PicoQuicStream> stream;
        };

        /// Picoquic instance.
        struct Shard
        {
            PicoQuicTransport* transport{ nullptr };
            std::size_t index{ 0 };

            /// Thread id of this shard's picoquic network thread, recorded by PqLoopCb.
            std::atomic<std::thread::id> thread_id{};

            picoquic_quic_t* quic_ctx{ nullptr };
            picoquic_network_thread_ctx_t* quic_network_thread_ctx{ nullptr };
            picoquic_packet_loop_param_t quic_network_thread_params{};
            int quic_loop_return_value{ 0 };

            SafeQueue<std::function<int()>> picoquic_runner_queue;
            SafeQueue<StreamMarkActiveInfo> stream_mark_active_queue;
            SafeQueue<std::weak_ptr<PicoQuicConnection>> datagram_mark_active_queue;

            uint64_t pq_loop_prev_time{ 0 };
            uint64_t pq_loop_metrics_prev_time{ 0 };
        };

        /*
         * Exceptions
         */
        struct Exception : public std::runtime_error
        {
            using std::runtime_error::runtime_error;
        };

        struct InvalidConfigException : public Exception
        {
            using Exception::Exception;
        };

        struct PicoQuicException : public Exception
        {
            using Exception::Exception;
        };

      public:
        /**
         * @brief Constructor for PicoQuicTransport
         * @param server Server connection information
         * @param tcfg Transport configuration
         * @param is_server_mode True for server mode, false for client mode
         * @param tick_service Shared pointer to tick service
         * @param logger Shared pointer to logger
         */
        PicoQuicTransport(const TransportRemote& server,
                          const TransportConfig& tcfg,
                          bool is_server_mode,
                          std::shared_ptr<timeq::tick_service> tick_service,
                          std::shared_ptr<Logger> logger,
                          Connection::API connection_api);

        virtual ~PicoQuicTransport();

        TransportStatus Status() const override;

        std::shared_ptr<Connection> Start() override;

        void Shutdown() override;

        void Close(const std::shared_ptr<Connection>& connection,
                   AppReasonForClose app_reason = AppReasonForClose::kRemoteRequestClose) override;

        void CloseInternal(const std::shared_ptr<Connection>& connection,
                           AppReasonForClose app_reason = AppReasonForClose::kRemoteRequestClose);

        virtual bool GetPeerAddrInfo(const std::shared_ptr<Connection>& connection, sockaddr_storage* addr) override;

        TransportError EnqueueDatagram(const std::shared_ptr<Connection>& connection,
                                       std::shared_ptr<const std::vector<uint8_t>> bytes,
                                       uint8_t priority,
                                       uint32_t ttl_ms) override;

        TransportError Enqueue(const std::shared_ptr<Connection>& connection,
                               const std::shared_ptr<Stream>& stream,
                               std::shared_ptr<const std::vector<uint8_t>> bytes,
                               uint8_t priority,
                               uint32_t ttl_ms,
                               EnqueueFlags flags) override;

        std::shared_ptr<const std::vector<uint8_t>> Dequeue(const std::shared_ptr<Connection>& connection) override;

        int CloseWebTransportSession(const std::shared_ptr<Connection>& connection,
                                     uint32_t error_code,
                                     const char* error_msg = nullptr) override;

        int DrainWebTransportSession(const std::shared_ptr<Connection>& connection) override;

        /*
         * Internal public methods
         */

        std::shared_ptr<PicoQuicConnection> GetConnection(const std::uint64_t& conn_id);

        void SetStatus(TransportStatus status);
        std::uint64_t MetricsSampleIntervalUs() const { return tconfig_.metrics_sample_ms * 1'000; }

        /// Get pq config to use.
        static picoquic_packet_loop_param_t MakeThreadConfig(uint16_t listen_port,
                                                             std::size_t socket_buffer_size,
                                                             std::size_t shard_count);

        /**
         * @brief Accept an incoming WebTransport connection
         * @details Initializes WebTransport context, updates internal data structures,
         *          and reports the OnNewConnection application callback. Similar to wt_baton_accept.
         * @param cnx Picoquic connection
         * @param path WebTransport path (may be nullptr)
         * @param path_length Length of path
         * @param stream_ctx WebTransport control stream context
         * @return 0 on success, -1 on error
         */
        int AcceptWebTransportConnection(picoquic_cnx_t* cnx,
                                         uint8_t* path,
                                         size_t path_length,
                                         h3zero_stream_ctx_t* stream_ctx);

        /// @returns A TX queue configured from the transport's time-queue settings.
        std::unique_ptr<SafeTimeQueue<ConnData>> MakeStreamTxQueue() const;

        const std::shared_ptr<PicoQuicConnection>& CreateConnection(picoquic_cnx_t* pq_cnx,
                                                                    Connection::API api = Connection::API::kNativeQuic);

        void SendNextDatagram(const std::shared_ptr<PicoQuicConnection>& conn_ctx, uint8_t* bytes_ctx, size_t max_len);

        void SendStreamBytes(const std::shared_ptr<PicoQuicConnection>& conn_ctx,
                             const std::shared_ptr<PicoQuicStream>& stream,
                             uint8_t* bytes_ctx,
                             size_t max_len);

        void OnConnectionStatus(const std::shared_ptr<PicoQuicConnection>& connection, TransportStatus status);

        void HandleNewConnection(const std::shared_ptr<PicoQuicConnection>& connection);

        void OnRecvDatagram(const std::shared_ptr<PicoQuicConnection>& conn_ctx, uint8_t* bytes, size_t length);

        /**
         * @param stream_handle  Stream the data arrived on, or nullptr if the transport has not seen
         *                       this remote-initiated stream before, in which case it adopts it.
         */
        void OnRecvStreamBytes(const std::shared_ptr<PicoQuicConnection>& conn_ctx,
                               const std::shared_ptr<PicoQuicStream>& stream_handle,
                               uint64_t stream_id,
                               int is_fin,
                               std::span<const uint8_t> bytes);

        void OnStreamClosed(const std::shared_ptr<PicoQuicConnection>& connection,
                            uint64_t stream_id,
                            std::shared_ptr<StreamRxContext> rx_ctx,
                            StreamClosedFlag flag);

        void CheckConnsForCongestion(std::size_t shard_idx);
        void EmitMetrics(std::size_t shard_idx);
        void RemoveClosedStreams(std::size_t shard_idx);

        void CloseStream(const std::shared_ptr<Connection>& connection,
                         const std::shared_ptr<Stream>& stream,
                         bool use_reset) override;

        void CloseStream(const std::shared_ptr<Connection>& connection, uint64_t stream_id, bool use_reset) override;

        /**
         * @brief Deregister WebTransport context
         * @details Cleans up WebTransport session resources including all streams
         *          associated with the control stream, capsule memory, and mappings.
         *          Similar to wt_baton.c:650 wt_baton_unlink_context().
         * @param cnx The picoquic connection
         */
        void DeregisterWebTransport(picoquic_cnx_t* cnx);

        /**
         * @brief Function to run the queue functions within the picoquic thread via the pq_loop_cb
         *
         * @details Function runs the picoquic specific functions in the same thread that runs the
         *      the event loop. This allows picoquic to be thread safe.  All picoquic functions that
         *      other threads want to call should queue those so they can be ran via
         *      the loop callback picoquic_packet_loop_wake_up
         *
         * @returns PIOCOQUIC error code, or ZERO if no error
         */
        int PqRunner(Shard& shard);

        /**
         * @brief Send drain session message for WebTransport
         * @details Sends a drain_webtransport_session capsule to tell peer to finish and close
         * @param cnx The picoquic connection
         * @return 0 on success, -1 on failure
         */
        int SendWebTransportDrainSession(picoquic_cnx_t* cnx);

        /**
         * @brief Create stream a new stream
         *
         * @details Create a new stream. Will block up to 100ms max to create the stream
         *
         * @note Thread-safe
         *
         * @param connection        Connection to create the stream on
         * @param priority          Priority of the stream
         *
         * @return Handle to the created stream
         * @throws PicoQuicException if unable to create stram
         */
        std::shared_ptr<Stream> CreateDataStream(const std::shared_ptr<Connection>& connection,
                                                 uint8_t priority) override;

        std::shared_ptr<Stream> CreateControlStream(const std::shared_ptr<Connection>& connection) override;

        std::shared_ptr<Stream> CreateRequestStream(const std::shared_ptr<Connection>& connection) override;

        /**
         * @brief Erase local state for the given stream.
         *
         * @param conn_ctx Connection context for the stream
         * @param stream_id ID of the stream to erase
         *
         * @warning This method must be called within the picoquic thread.
         */
        void EraseStreamState(const std::shared_ptr<PicoQuicConnection>& conn_ctx, std::uint64_t stream_id);

        /**
         * @brief Mark queued streams and datagram connections active
         *
         * @details Drains the shard's mark-active queues on the picoquic thread. Entries hold weak
         *      handles and marking is skipped for a stream that is no longer open, so stream
         *      teardown does not have to flush the queue first. Closing a connection still does,
         *      because a queued datagram mark has no equivalent guard.
         */
        void ProcessMarkActive(Shard& shard);

      public:
        std::shared_ptr<Logger> logger;
        bool is_server_mode;
        bool is_unidirectional{ false };
        bool debug{ false };
        Connection::API connection_api{ Connection::API::kNativeQuic };

      private:
        /// Get shard ID for given connection ID.
        std::optional<std::size_t> GetConnShardIdx(const std::uint64_t& conn_id);

        std::shared_ptr<Connection> StartClient();

        /// Create a picoquic instance for the current config.
        picoquic_quic_t* CreateQuicInstance(uint64_t current_time);

        void Server();
        bool ClientLoop();
        void CbNotifier();
        void RunPqFunction(std::size_t shard_idx, std::function<int()>&& function);
        void CheckCallbackDelta(const std::shared_ptr<PicoQuicStream>& stream, bool tx = true);

        /**
         * @brief Mark a stream active
         * @details This method MUST only be called within the picoquic thread. Other threads reach it
         *      by pushing onto the shard's mark-active queue.
         */
        void MarkStreamActive(const std::shared_ptr<PicoQuicConnection>& connection,
                              const std::shared_ptr<PicoQuicStream>& stream);

        /**
         * @brief Mark datagram ready
         * @details This method MUST only be called within the picoquic thread.
         */
        void MarkDgramReady(const std::shared_ptr<PicoQuicConnection>& connection);

        /**
         * @brief Initialize WebTransport context
         * @details Sets up the HTTP/3 and WebTransport contexts for WebTransport mode
         */
        int InitializeWebTransportContext();

        /**
         * @brief Setup WebTransport connection
         * @details Establishes the WebTransport connection over HTTP/3
         */
        int SetupWebTransportConnection(picoquic_cnx_t* cnx);

        /**
         * @brief Get the appropriate ALPN based on transport mode
         */
        const char* GetAlpn() const;

        /**
         * @brief Set WebTransport path and callback for server mode
         * @details Configures the path and callback function for handling WebTransport connections.
         * ONLY connections to the configured path will be accepted; all other paths are rejected.
         * @param path The WebTransport path to accept (default: "/relay"). Only this exact path is allowed.
         * @param callback The callback function to handle WebTransport events (nullptr = use default)
         * @param app_ctx Application context to pass to the callback
         */
        void SetWebTransportPathCallback(const std::string& path,
                                         picohttp_post_data_cb_fn callback = nullptr,
                                         void* app_ctx = nullptr);

        /**
         * @brief Create a local WebTransport stream
         * @details Creates a WebTransport stream using per-connection h3_ctx.
         *          Stream data callbacks are handled through the configured path_callback.
         * @param cnx The picoquic connection
         * @param is_bidir True for bidirectional stream, false for unidirectional
         * @return Stream context or nullptr on failure
         */
        h3zero_stream_ctx_t* CreateWebTransportStream(picoquic_cnx_t* cnx, bool is_bidir);

        /**
         * @brief Send close session message for WebTransport
         * @details Sends a close_webtransport_session capsule and closes the control stream
         * @param cnx The picoquic connection
         * @param error_code WebTransport error code
         * @param error_msg Error message string
         * @return 0 on success, -1 on failure
         */
        int SendWebTransportCloseSession(picoquic_cnx_t* cnx, uint32_t error_code, const char* error_msg);

        /**
         * @brief Create a stream on the picoquic thread and wait for it.
         *
         * @param bidir     Whether to open a bidirectional stream.
         *
         * @throws PicoQuicException if the stream could not be created in time.
         */
        std::shared_ptr<PicoQuicStream> CreateStreamOnPqThread(const std::shared_ptr<Connection>& connection,
                                                               bool bidir,
                                                               uint8_t priority);

        /// @brief Queue bytes on an already-resolved stream handle.
        TransportError EnqueueStream(const std::shared_ptr<PicoQuicConnection>& connection,
                                     const std::shared_ptr<PicoQuicStream>& stream,
                                     std::shared_ptr<const std::vector<uint8_t>> bytes,
                                     uint8_t priority,
                                     uint32_t ttl_ms,
                                     EnqueueFlags flags);

        std::shared_ptr<PicoQuicStream> CreateStreamInternal(const std::shared_ptr<Connection>& connection,
                                                             bool bidir,
                                                             uint8_t priority);

        /**
         * @brief App initiated Close stream
         * @details App initiated close stream. When the app wants to switch streams to a new stream this
         * function is used to close out the current stream. A FIN will be sent.
         *
         * @warning This method must be called within the picoquic thread
         *
         * @param conn_ctx      Connection context for the stream
         * @param stream_id     ID of the stream to close.
         * @param send_reset    Indicates if the stream should be closed by RESET, otherwise FIN
         */
        void CloseStream(const std::shared_ptr<PicoQuicConnection>& conn_ctx, std::uint64_t stream_id, bool send_reset);

        /*
         * Variables
         */
        picoquic_quic_config_t config_;
        picoquic_tp_t local_tp_options_;
        SafeQueue<std::function<void()>> cbNotifyQueue_;

        /// Parallel picoquic instances
        std::vector<std::unique_ptr<Shard>> shards_;

        std::atomic<bool> stop_;
        std::mutex state_mutex_; /// Used for stream/context/state updates
        std::atomic<TransportStatus> transportStatus_;
        std::thread cbNotifyThread_;

        TransportRemote serverInfo_;
        TransportConfig tconfig_;

        std::map<std::uint64_t, std::shared_ptr<PicoQuicConnection>> connections_;
        std::shared_ptr<timeq::tick_service> tick_service_;

        // WebTransport configuration (server-wide, not per-connection)
        struct WebTransportConfig
        {
            std::string path;                                    /// WebTransport path
            picohttp_post_data_cb_fn path_callback = nullptr;    /// WebTransport path callback function
            void* path_app_ctx = nullptr;                        /// Application context for path callback
            std::vector<picohttp_server_path_item_t> path_items; /// Server path items for WebTransport
            picohttp_server_parameters_t server_params{};        /// Server parameters (must persist for ALPN callback)
        };

        std::optional<WebTransportConfig> wt_config_;
    };

} // namespace quicr
