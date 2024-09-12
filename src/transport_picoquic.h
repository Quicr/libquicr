// SPDX-FileCopyrightText: Copyright (c) 2024 Cisco Systems
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <map>
#include <mutex>
#include <queue>
#include <string>
#include <thread>
#include <vector>

#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>

#include <picoquic.h>
#include <picoquic_config.h>
#include <picoquic_packet_loop.h>
#include <quicr/detail/quic_transport.h>
#include <spdlog/spdlog.h>

#include "quicr/detail/priority_queue.h"
#include "quicr/detail/quic_transport_metrics.h"
#include "quicr/detail/safe_queue.h"
#include "quicr/detail/span.h"
#include "quicr/detail/stream_buffer.h"
#include "quicr/detail/time_queue.h"

namespace quicr {

    constexpr int kPqLoopMaxDelayUs = 500;    /// The max microseconds that pq_loop will be ran again
    constexpr int kPqRestWaitMinPriority = 4; /// Minimum priority value to consider for RESET and WAIT
    constexpr int kPqCcLowCwin = 4000;        /// Bytes less than this value are considered a low/congested CWIN

    class PicoQuicTransport : public ITransport
    {
      public:
        const char* quicr_alpn = "moq-00";

        using BytesT = std::vector<uint8_t>;
        using DataContextId = uint64_t;

        /**
         * Data context information
         *      Data context is intended to be a container for metrics and other state that is related to a flow of
         *      data that may use datagram or one or more stream QUIC frames
         */
        struct DataContext
        {
            bool is_bidir{ false };           /// Indicates if the stream is bidir (true) or unidir (false)
            bool mark_stream_active{ false }; /// Instructs the stream to be marked active

            bool uses_reset_wait{ false };       /// Indicates if data context can/uses reset wait strategy
            bool tx_reset_wait_discard{ false }; /// Instructs TX objects to be discarded on POP instead

            DataContextId data_ctx_id{ 0 }; /// The ID of this context
            TransportConnId conn_id{ 0 };   /// The connection ID this context is under

            enum class StreamAction : uint8_t
            { /// Stream action that should be done by send/receive processing
                kNoAction = 0,
                kReplaceStreamUseReset,
                kReplaceStreamUseFin,
            } stream_action{ StreamAction::kNoAction };

            std::optional<uint64_t> current_stream_id; /// Current active stream if the value is >= 4

            uint8_t priority{ 0 };

            uint64_t in_data_cb_skip_count{ 0 }; /// Number of times callback was skipped due to size

            std::unique_ptr<PriorityQueue<ConnData>> tx_data; /// Pending objects to be written to the network

            uint8_t* stream_tx_object{ nullptr }; /// Current object that is being sent as a byte stream
            size_t stream_tx_object_size{ 0 };    /// Size of the tx object
            size_t stream_tx_object_offset{ 0 };  /// Pointer offset to next byte to send

            // The last ticks when TX callback was run
            uint64_t last_tx_tick{ 0 };

            QuicDataContextMetrics metrics;

            DataContext() = default;
            DataContext(DataContext&&) = default;

            DataContext& operator=(const DataContext&) = delete;
            DataContext& operator=(DataContext&&) = delete;

            ~DataContext()
            {
                // Free the TX object
                if (stream_tx_object != nullptr) {
                    delete[] stream_tx_object;
                    stream_tx_object = nullptr;
                }
            }

            /**
             * Reset the TX object buffer
             */
            void ResetTxObject()
            {
                if (stream_tx_object != nullptr) {
                    delete[] stream_tx_object;
                }

                stream_tx_object = nullptr;
                stream_tx_object_offset = 0;
                stream_tx_object_size = 0;
            }
        };

        /**
         * Connection context information
         */
        struct ConnectionContext
        {
            TransportConnId conn_id{ 0 };     /// This connection ID
            picoquic_cnx_t* pq_cnx = nullptr; /// Picoquic connection/path context
            uint64_t last_stream_id{ 0 };     /// last stream Id

            bool mark_dgram_ready{ false }; /// Instructs datagram to be marked ready/active

            DataContextId next_data_ctx_id{ 1 }; /// Next data context ID; zero is reserved for default context

            std::unique_ptr<PriorityQueue<ConnData>>
              dgram_tx_data;                 /// Datagram pending objects to be written to the network
            SafeQueue<BytesT> dgram_rx_data; /// Buffered datagrams received from the network

            /**
             * Active stream buffers for received unidirectional streams
             */
            struct RxStreamBuffer
            {
                std::shared_ptr<SafeStreamBuffer<uint8_t>> buf;
                bool closed{ false };       /// Indicates if stream is active or in closed state
                bool checked_once{ false }; /// True if closed and checked once to close

                RxStreamBuffer() { buf = std::make_shared<SafeStreamBuffer<uint8_t>>(); }
            };
            std::map<uint64_t, RxStreamBuffer> rx_stream_buffer; /// Map of stream receive buffers, key is stream_id

            /**
             * Active data contexts (streams bidir/unidir and datagram)
             */
            std::map<quicr::DataContextId, DataContext> active_data_contexts;

            char peer_addr_text[45]{ 0 };
            uint16_t peer_port{ 0 };
            sockaddr_storage peer_addr;

            // States
            bool is_congested{ false };
            uint16_t not_congested_gauge{ 0 }; /// Interval gauge count of consecutive not congested checks

            // Metrics
            QuicConnectionMetrics metrics;

            ConnectionContext() {}

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
        PicoQuicTransport(const TransportRemote& server,
                          const TransportConfig& tcfg,
                          TransportDelegate& delegate,
                          bool is_server_mode,
                          std::shared_ptr<spdlog::logger> logger);

        virtual ~PicoQuicTransport();

        TransportStatus Status() const override;
        TransportConnId Start() override;
        void Close(const TransportConnId& conn_id, uint64_t app_reason_code = 100) override;

        virtual bool GetPeerAddrInfo(const TransportConnId& conn_id, sockaddr_storage* addr) override;

        DataContextId CreateDataContext(TransportConnId conn_id,
                                        bool use_reliable_transport,
                                        uint8_t priority,
                                        bool bidir) override;

        void DeleteDataContext(const TransportConnId& conn_id, DataContextId data_ctx_id) override;
        void DeleteDataContextInternal(TransportConnId conn_id, DataContextId data_ctx_id);

        TransportError Enqueue(const TransportConnId& conn_id,
                               const DataContextId& data_ctx_id,
                               Span<const uint8_t> bytes,
                               std::vector<quicr::MethodTraceItem>&& trace,
                               uint8_t priority,
                               uint32_t ttl_ms,
                               uint32_t delay_ms,
                               EnqueueFlags flags) override;

        std::optional<std::vector<uint8_t>> Dequeue(TransportConnId conn_id,
                                                    std::optional<DataContextId> data_ctx_id) override;

        std::shared_ptr<SafeStreamBuffer<uint8_t>> GetStreamBuffer(TransportConnId conn_id,
                                                                   uint64_t stream_id) override;

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
                               Span<const uint8_t> bytes);

        void CheckConnsForCongestion();
        void EmitMetrics();
        void RemoveClosedStreams();

        /**
         * @brief Function run the queue functions within the picoquic thread via the pq_loop_cb
         *
         * @details Function runs the picoquic specific functions in the same thread that runs the
         *      the event loop. This allows picoquic to be thread safe.  All picoquic functions that
         *      other threads want to call should queue those in `picoquic_runner_queue`.
         */
        void PqRunner();

        /*
         * Internal Public Variables
         */
        std::shared_ptr<spdlog::logger> logger;
        bool is_server_mode;
        bool is_unidirectional{ false };
        bool debug{ false };

      private:
        TransportConnId CreateClient();
        void Shutdown();

        void Server();
        void Client(TransportConnId conn_id);
        void CbNotifier();

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
        picoquic_tp_t local_tp_options_;
        SafeQueue<std::function<void()>> cbNotifyQueue_;

        SafeQueue<std::function<void()>>
          picoquic_runner_queue_; /// Threads queue functions that picoquic will call via the pq_loop_cb call

        std::atomic<bool> stop_;
        std::mutex state_mutex_; /// Used for stream/context/state updates
        std::atomic<TransportStatus> transportStatus_;
        std::thread picoQuicThread_;
        std::thread cbNotifyThread_;

        TransportRemote serverInfo_;
        TransportDelegate& delegate_;
        TransportConfig tconfig_;

        std::map<TransportConnId, ConnectionContext> conn_context_;
        std::shared_ptr<TickService> tick_service_;
    };

} // namespace quicr
