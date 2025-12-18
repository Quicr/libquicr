// SPDX-FileCopyrightText: Copyright (c) 2024 Cisco Systems
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

#include "quicr/detail/priority_queue.h"
#include "quicr/detail/quic_transport_metrics.h"
#include "quicr/detail/safe_queue.h"
#include "quicr/detail/stream_buffer.h"
#include "quicr/detail/time_queue.h"

#include <picoquic.h>
#include <picoquic_config.h>
#include <picoquic_packet_loop.h>
#include <quicr/detail/quic_transport.h>
#include <spdlog/spdlog.h>
#include <tls_api.h>

// WebTransport headers
#include <h3zero.h>
#include <h3zero_common.h>
#include <pico_webtransport.h>

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
    constexpr int kPqRestWaitMinPriority = 4;         /// Minimum priority value to consider for RESET and WAIT
    constexpr int kPqCcLowCwin = 4000;                /// Bytes less than this value are considered a low/congested CWIN
    constexpr int kCongestionCheckInterval = 100'000; /// Congestion check interval in microseconds

    /**
     * Minimum bytes needed to write before considering to send. This doesn't
     */
    constexpr int kMinStreamBytesForSend = 2;

    enum class TransportMode
    {
        kQuic,        // Raw QUIC transport with moq-00 ALPN
        kWebTransport // WebTransport over HTTP/3 with h3 ALPN
    };

    class PicoQuicTransport : public ITransport
    {
      public:
        const char* quicr_alpn = "moq-00";
        const char* webtransport_alpn = "h3";

        using BytesT = std::vector<uint8_t>;
        using DataContextId = uint64_t;

        /**
         */
        enum class StreamErrorCodes : uint32_t
        {
            kInternalError = 20,
            kUnknownExpiry = 50
        };

        /**
         * Data context information
         *  Data context is intended to be a container for metrics and other state that is
         *  related to a flow of data that may use datagram or one or more stream QUIC frames
         */
        struct DataContext
        {
          public:
            DataContext() = default;
            DataContext(DataContext&&) = default;

            DataContext& operator=(const DataContext&) = delete;
            DataContext& operator=(DataContext&&) = delete;

            ~DataContext()
            {
                // clean up
                stream_tx_object = nullptr;
            }

            /**
             * Reset the TX object buffer
             */
            void ResetTxObject()
            {
                // reset/clean up
                stream_tx_object = nullptr;
                stream_tx_object_offset = 0;
            }

          public:
            bool is_bidir : 1 { false };           /// Indicates if the stream is bidir (true) or unidir (false)
            bool mark_stream_active : 1 { false }; /// Instructs the stream to be marked active
            bool tx_start_stream : 1 { false };    /// Indicates tx queue starts a new stream

            bool uses_reset_wait : 1 { false };  /// Indicates if data context can/uses reset wait strategy
            bool tx_reset_wait_discard{ false }; /// Instructs TX objects to be discarded on POP instead

            bool delete_on_empty{ false }; /// Instructs TX objects to be discarded on POP instead

            DataContextId data_ctx_id{ 0 }; /// The ID of this context
            TransportConnId conn_id{ 0 };   /// The connection ID this context is under

            std::optional<uint64_t> current_stream_id; /// Current active stream if the value is >= 4

            uint8_t priority{ 0 };

            uint64_t in_data_cb_skip_count{ 0 }; /// Number of times callback was skipped due to size

            std::unique_ptr<PriorityQueue<ConnData>> tx_data; /// Pending objects to be written to the network

            /// Current object that is being sent as a byte stream
            std::shared_ptr<const std::vector<uint8_t>> stream_tx_object;
            size_t stream_tx_object_offset{ 0 }; /// Pointer offset to next byte to send

            // The last ticks when TX callback was run
            uint64_t last_tx_tick{ 0 };

            QuicDataContextMetrics metrics;

            /// WebTransport stream context (only used in WebTransport mode)
            h3zero_stream_ctx_t* wt_stream_ctx{ nullptr };
        };

        /**
         * Connection context information
         */
        struct ConnectionContext
        {
            TransportConnId conn_id{ 0 };     /// This connection ID
            picoquic_cnx_t* pq_cnx = nullptr; /// Picoquic connection/path context
            uint64_t last_stream_id{ 0 };     /// last stream Id
            std::optional<uint64_t> control_stream_id{ std::nullopt };

            bool mark_dgram_ready{ false };                       /// Instructs datagram to be marked ready/active
            TransportMode transport_mode{ TransportMode::kQuic }; /// Transport mode for this connection

            DataContextId next_data_ctx_id{ 1 }; /// Next data context ID; zero is reserved for default context

            std::shared_ptr<PriorityQueue<ConnData>>
              dgram_tx_data; /// Datagram pending objects to be written to the network

            std::shared_ptr<SafeQueue<std::shared_ptr<const std::vector<uint8_t>>>>
              dgram_rx_data; /// Buffered datagrams received from the network

            /**
             * Active stream buffers for received unidirectional streams
             */
            struct RxStreamBuffer
            {
                std::shared_ptr<StreamRxContext> rx_ctx; /// Stream RX context that holds data and caller info
                bool closed{ false };                    /// Indicates if stream is active or in closed state
                bool checked_once{ false };              /// True if closed and checked once to close

                RxStreamBuffer()
                  : rx_ctx(std::make_shared<StreamRxContext>())
                {
                    rx_ctx->caller_any.reset();
                    rx_ctx->data_queue.Clear();
                }
            };

            std::map<uint64_t, RxStreamBuffer> rx_stream_buffer; /// Map of stream receive buffers, key is stream_id

            /**
             * Active data contexts (streams bidir/unidir and datagram)
             */
            std::map<quicr::DataContextId, DataContext> active_data_contexts;

            /**
             * WebTransport stream ID to data context ID mapping
             * Used in WebTransport mode to look up data context for a stream
             */
            std::map<uint64_t, DataContextId> wt_stream_to_data_ctx;

            /// WebTransport HTTP/3 context for this connection (only used in WebTransport mode)
            /// - Server mode: created by h3zero_callback per connection
            /// - Client mode: created by picowt_prepare_client_cnx per connection
            h3zero_callback_ctx_t* wt_h3_ctx{ nullptr };

            /// WebTransport control stream context for this connection (only used in WebTransport mode)
            h3zero_stream_ctx_t* wt_control_stream_ctx{ nullptr };

            /// True if this connection owns the wt_h3_ctx and should free it on cleanup (client mode)
            /// False for server mode where h3zero manages the h3_ctx lifecycle
            bool wt_h3_ctx_owned{ false };

            /// Client mode: connection-specific authority (server:port)
            std::string wt_authority;

            /// WebTransport capsule accumulator for control stream message parsing
            /// Used to parse CLOSE_WEBTRANSPORT_SESSION and other capsules
            picowt_capsule_t wt_capsule{};

            char peer_addr_text[45]{ 0 };
            uint16_t peer_port{ 0 };
            sockaddr_storage peer_addr;

            // States
            bool is_congested{ false };
            uint16_t not_congested_gauge{ 0 }; /// Interval gauge count of consecutive not congested checks

            // Metrics
            QuicConnectionMetrics metrics;

            ConnectionContext()
              : dgram_rx_data(std::make_shared<SafeQueue<std::shared_ptr<const std::vector<uint8_t>>>>())
            {
            }

            ConnectionContext(picoquic_cnx_t* cnx)
              : ConnectionContext()
            {
                pq_cnx = cnx;
            }
        };

        /*
         * pq event loop member vars
         */
        uint64_t pq_loop_prev_time = 0;
        uint64_t pq_loop_metrics_prev_time = 0;

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

        static void PicoQuicLogging(const char* message, void* argp)
        {
            auto instance = reinterpret_cast<PicoQuicTransport*>(argp);
            if (!instance->stop_ && instance->logger) {
                instance->logger->info(message);
            }
        }

      public:
        /**
         * @brief Constructor for PicoQuicTransport
         * @param server Server connection information
         * @param tcfg Transport configuration
         * @param delegate Transport delegate for callbacks
         * @param is_server_mode True for server mode, false for client mode
         * @param tick_service Shared pointer to tick service
         * @param logger Shared pointer to logger
         * @param transport_mode For clients: sets ALPN (kQuic→moq-00, kWebTransport→h3).
         *                       For servers: NOT USED - server auto-negotiates per connection
         *                       based on client ALPN (supports both moq-00 and h3 simultaneously)
         */
        PicoQuicTransport(const TransportRemote& server,
                          const TransportConfig& tcfg,
                          TransportDelegate& delegate,
                          bool is_server_mode,
                          std::shared_ptr<TickService> tick_service,
                          std::shared_ptr<spdlog::logger> logger,
                          TransportMode transport_mode = TransportMode::kQuic);

        virtual ~PicoQuicTransport();

        TransportStatus Status() const override;
        TransportConnId Start() override;
        void Close(const TransportConnId& conn_id, uint64_t app_reason_code = 100) override;

        virtual bool GetPeerAddrInfo(const TransportConnId& conn_id, sockaddr_storage* addr) override;

        DataContextId CreateDataContext(TransportConnId conn_id,
                                        bool use_reliable_transport,
                                        uint8_t priority,
                                        bool bidir) override;

        void DeleteDataContext(const TransportConnId& conn_id,
                               DataContextId data_ctx_id,
                               bool delete_on_empty = false) override;

        TransportError Enqueue(const TransportConnId& conn_id,
                               const DataContextId& data_ctx_id,
                               std::uint64_t group_id,
                               std::shared_ptr<const std::vector<uint8_t>> bytes,
                               uint8_t priority,
                               uint32_t ttl_ms,
                               uint32_t delay_ms,
                               EnqueueFlags flags) override;

        std::shared_ptr<const std::vector<uint8_t>> Dequeue(TransportConnId conn_id,
                                                            std::optional<DataContextId> data_ctx_id) override;

        std::shared_ptr<StreamRxContext> GetStreamRxContext(TransportConnId conn_id, uint64_t stream_id) override;

        int CloseWebTransportSession(TransportConnId conn_id,
                                     uint32_t error_code,
                                     const char* error_msg = nullptr) override;

        int DrainWebTransportSession(TransportConnId conn_id) override;

        void SetRemoteDataCtxId(TransportConnId conn_id,
                                DataContextId data_ctx_id,
                                DataContextId remote_data_ctx_id) override;

        void SetStreamIdDataCtxId(TransportConnId conn_id, DataContextId data_ctx_id, uint64_t stream_id) override;
        void SetDataCtxPriority(TransportConnId conn_id, DataContextId data_ctx_id, uint8_t priority) override;

        /*
         * Internal public methods
         */
        ConnectionContext* GetConnContext(const TransportConnId& conn_id);
        void SetStatus(TransportStatus status);

        /**
         * @brief Accept an incoming WebTransport connection
         * @details Initializes WebTransport context, updates internal data structures,
         *          and reports OnNewConnection() callback. Similar to wt_baton_accept.
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

        /**
         * @brief Create bidirectional data context for received new stream
         *
         * @details Create a bidir data context for received bidir stream. This is only called
         *  for received bidirectional streams.
         *
         * @param conn_id           Connection context ID for the stream
         * @param stream_id         Stream ID of the new received stream
         *
         * @returns DataContext pointer to the created context, nullptr if invalid connection id
         */
        DataContext* CreateDataContextBiDirRecv(TransportConnId conn_id, uint64_t stream_id);

        ConnectionContext& CreateConnContext(picoquic_cnx_t* pq_cnx);

        void SendNextDatagram(ConnectionContext* conn_ctx, uint8_t* bytes_ctx, size_t max_len);
        void SendStreamBytes(DataContext* data_ctx, uint8_t* bytes_ctx, size_t max_len);

        void OnConnectionStatus(TransportConnId conn_id, TransportStatus status);

        void OnNewConnection(TransportConnId conn_id);
        void OnRecvDatagram(ConnectionContext* conn_ctx, uint8_t* bytes, size_t length);
        void OnRecvStreamBytes(ConnectionContext* conn_ctx,
                               DataContext* data_ctx,
                               uint64_t stream_id,
                               int is_fin,
                               std::span<const uint8_t> bytes);

        void OnStreamClosed(TransportConnId conn_id,
                            uint64_t stream_id,
                            std::shared_ptr<StreamRxContext> rx_ctx,
                            StreamClosedFlag flag);

        void CheckConnsForCongestion();
        void EmitMetrics();
        void RemoveClosedStreams();

        bool StreamActionCheck(DataContext* data_ctx, StreamAction stream_action);

        /**
         * @brief Close stream by stream id
         * @param conn_id           Connection id of stream
         * @param stream_id         Stream ID to close
         * @param use_reset         True to close by RESET, false to close by FIN
         */
        void CloseStreamById(TransportConnId conn_id, uint64_t stream_id, bool use_reset) override;

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
        int PqRunner();

        /*
         * Internal Public Variables
         */
        std::shared_ptr<spdlog::logger> logger;
        bool is_server_mode;
        bool is_unidirectional{ false };
        bool debug{ false };
        TransportMode transport_mode{ TransportMode::kQuic };

      private:
        void DeleteDataContextInternal(TransportConnId conn_id, DataContextId data_ctx_id, bool delete_on_empty);

        TransportConnId StartClient();
        void Shutdown();

        void Server();
        bool ClientLoop();
        void CbNotifier();
        void RunPqFunction(std::function<int()>&& function);
        void CheckCallbackDelta(DataContext* data_ctx, bool tx = true);

        /**
         * @brief Mark a stream active
         * @details This method MUST only be called within the picoquic thread. Enqueue and other
         *      thread methods can call this via the pq_runner.
         */
        void MarkStreamActive(TransportConnId conn_id, DataContextId data_ctx_id);

        /**
         * @brief Mark datagram ready
         * @details This method MUST only be called within the picoquic thread. Enqueue and other
         *      thread methods can call this via the pq_runner.
         */
        void MarkDgramReady(TransportConnId conn_id);

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
         * @brief Send drain session message for WebTransport
         * @details Sends a drain_webtransport_session capsule to tell peer to finish and close
         * @param cnx The picoquic connection
         * @return 0 on success, -1 on failure
         */
        int SendWebTransportDrainSession(picoquic_cnx_t* cnx);

        /**
         * @brief Create a new stream
         *
         * @param conn_ctx      Connection context to create stream under
         * @param data_ctx      Data context in connection context to create streams
         */
        void CreateStream(ConnectionContext& conn_ctx, DataContext* data_ctx);

        /**
         * @brief App initiated Close stream
         * @details App initiated close stream. When the app deletes a context or wants to switch streams to a new
         * stream this function is used to close out the current stream. A FIN will be sent.
         *
         * @param conn_ctx      Connection context for the stream
         * @param data_ctx      Data context for the stream
         * @param send_reset    Indicates if the stream should be closed by RESET, otherwise FIN
         */
        void CloseStream(ConnectionContext& conn_ctx, DataContext* data_ctx, bool send_reset);

        /*
         * Variables
         */
        picoquic_quic_config_t config_;
        picoquic_quic_t* quic_ctx_;
        picoquic_network_thread_ctx_t* quic_network_thread_ctx_;
        picoquic_packet_loop_param_t quic_network_thread_params_{};
        int quic_loop_return_value_{ 0 };
        picoquic_tp_t local_tp_options_;
        SafeQueue<std::function<void()>> cbNotifyQueue_;

        /// Threads queue functions that picoquic will call via the pq_loop callback
        SafeQueue<std::function<int()>> picoquic_runner_queue_;

        std::atomic<bool> stop_;
        std::mutex state_mutex_; /// Used for stream/context/state updates
        std::atomic<TransportStatus> transportStatus_;
        std::thread cbNotifyThread_;

        TransportRemote serverInfo_;
        TransportDelegate& delegate_;
        TransportConfig tconfig_;

        std::map<TransportConnId, ConnectionContext> conn_context_;
        std::shared_ptr<TickService> tick_service_;

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
