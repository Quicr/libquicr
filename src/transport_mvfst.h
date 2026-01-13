// SPDX-FileCopyrightText: Copyright (c) 2024 Cisco Systems
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

#ifdef USE_MVFST

// quicr headers
#include "quicr/detail/priority_queue.h"
#include "quicr/detail/quic_transport_metrics.h"
#include "quicr/detail/safe_queue.h"
#include "quicr/detail/stream_buffer.h"
#include "quicr/detail/time_queue.h"
#include <quicr/detail/quic_transport.h>

#include <spdlog/spdlog.h>

// mvfst headers
#include <quic/api/QuicSocket.h>
#include <quic/client/QuicClientTransport.h>
#include <quic/server/QuicServer.h>
#include <quic/server/QuicServerTransport.h>
#include <quic/common/events/FollyQuicEventBase.h>
#include <quic/common/udpsocket/FollyQuicAsyncUDPSocket.h>
#include <quic/fizz/client/handshake/FizzClientQuicHandshakeContext.h>

// fizz headers for TLS contexts
#include <fizz/client/FizzClientContext.h>
#include <fizz/server/FizzServerContext.h>

// proxygen WebTransport support (optional)
#ifdef HAVE_PROXYGEN_WEBTRANSPORT
#include <proxygen/lib/http/webtransport/QuicWebTransport.h>
#include <proxygen/lib/http/webtransport/WebTransport.h>
#endif

// folly headers
#include <folly/io/IOBuf.h>
#include <folly/io/async/EventBase.h>
#include <folly/io/async/ScopedEventBaseThread.h>
#include <folly/futures/Future.h>
#include <folly/SocketAddress.h>

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

    enum class MvfstTransportMode
    {
        kQuic,        // Raw QUIC transport
        kWebTransport // WebTransport over HTTP/3 (via proxygen)
    };

    // Forward declarations
    class MvfstTransport;

    /**
     * @brief Callback handler for mvfst client connections
     */
    class MvfstClientCallback
      : public quic::QuicSocket::ConnectionSetupCallback
      , public quic::QuicSocket::ConnectionCallback
      , public quic::QuicSocket::ReadCallback
      , public quic::QuicSocket::DatagramCallback
    {
      public:
        explicit MvfstClientCallback(MvfstTransport* transport, TransportConnId conn_id);

        // ConnectionSetupCallback
        void onConnectionSetupError(quic::QuicError error) noexcept override;
        void onTransportReady() noexcept override;
        void onReplaySafe() noexcept override;

        // ConnectionCallback
        void onNewBidirectionalStream(quic::StreamId id) noexcept override;
        void onNewUnidirectionalStream(quic::StreamId id) noexcept override;
        void onStopSending(quic::StreamId id, quic::ApplicationErrorCode error) noexcept override;
        void onConnectionEnd() noexcept override;
        void onConnectionError(quic::QuicError error) noexcept override;
        void onConnectionEnd(quic::QuicError /* error */) noexcept override {}
        void onBidirectionalStreamsAvailable(uint64_t /*numStreamsAvailable*/) noexcept override {}
        void onUnidirectionalStreamsAvailable(uint64_t /*numStreamsAvailable*/) noexcept override {}

        // ReadCallback
        void readAvailable(quic::StreamId id) noexcept override;
        void readError(quic::StreamId id, quic::QuicError error) noexcept override;

        // DatagramCallback
        void onDatagramsAvailable() noexcept override;

        std::shared_ptr<quic::QuicSocket> getSocket() { return socket_; }
        void setSocket(std::shared_ptr<quic::QuicSocket> socket) { socket_ = std::move(socket); }

      private:
        MvfstTransport* transport_;
        TransportConnId conn_id_;
        std::shared_ptr<quic::QuicSocket> socket_;
    };

    /**
     * @brief Factory for creating server transports
     */
    class MvfstServerTransportFactory : public quic::QuicServerTransportFactory
    {
      public:
        explicit MvfstServerTransportFactory(MvfstTransport* transport);

        quic::QuicServerTransport::Ptr make(
            folly::EventBase* evb,
            std::unique_ptr<folly::AsyncUDPSocket> socket,
            const folly::SocketAddress& addr,
            quic::QuicVersion quicVersion,
            std::shared_ptr<const fizz::server::FizzServerContext> ctx) noexcept override;

      private:
        MvfstTransport* transport_;
    };

    /**
     * @brief Callback handler for mvfst server connections
     */
    class MvfstServerConnectionCallback
      : public quic::QuicSocket::ConnectionSetupCallback
      , public quic::QuicSocket::ConnectionCallback
      , public quic::QuicSocket::ReadCallback
      , public quic::QuicSocket::DatagramCallback
    {
      public:
        MvfstServerConnectionCallback(MvfstTransport* transport,
                                      TransportConnId conn_id,
                                      std::shared_ptr<quic::QuicSocket> socket);

        // ConnectionSetupCallback
        void onConnectionSetupError(quic::QuicError error) noexcept override;
        void onTransportReady() noexcept override;
        void onReplaySafe() noexcept override {}

        // ConnectionCallback
        void onNewBidirectionalStream(quic::StreamId id) noexcept override;
        void onNewUnidirectionalStream(quic::StreamId id) noexcept override;
        void onStopSending(quic::StreamId id, quic::ApplicationErrorCode error) noexcept override;
        void onConnectionEnd() noexcept override;
        void onConnectionError(quic::QuicError error) noexcept override;
        void onConnectionEnd(quic::QuicError /* error */) noexcept override {}
        void onBidirectionalStreamsAvailable(uint64_t /*numStreamsAvailable*/) noexcept override {}
        void onUnidirectionalStreamsAvailable(uint64_t /*numStreamsAvailable*/) noexcept override {}

        // ReadCallback
        void readAvailable(quic::StreamId id) noexcept override;
        void readError(quic::StreamId id, quic::QuicError error) noexcept override;

        // DatagramCallback
        void onDatagramsAvailable() noexcept override;

        std::shared_ptr<quic::QuicSocket> getSocket() { return socket_; }
        void setSocket(std::shared_ptr<quic::QuicSocket> socket) { socket_ = std::move(socket); }

      private:
        MvfstTransport* transport_;
        TransportConnId conn_id_;
        std::shared_ptr<quic::QuicSocket> socket_;
    };

    /**
     * @brief MvfstTransport - QUIC transport implementation using Facebook's mvfst library
     */
    class MvfstTransport : public ITransport
    {
      public:
        using BytesT = std::vector<uint8_t>;
        using DataContextId = uint64_t;

        friend class MvfstClientCallback;
        friend class MvfstServerTransportFactory;
        friend class MvfstServerConnectionCallback;

        /**
         * Stream error codes
         */
        enum class StreamErrorCodes : uint32_t
        {
            kInternalError = 20,
            kUnknownExpiry = 50
        };

        /**
         * Data context information
         */
        struct DataContext
        {
          public:
            DataContext() = default;
            DataContext(DataContext&&) = default;

            DataContext& operator=(const DataContext&) = delete;
            DataContext& operator=(DataContext&&) = delete;

            ~DataContext() = default;

            void ResetTxObject()
            {
                stream_tx_object = nullptr;
                stream_tx_object_offset = 0;
            }

          public:
            bool is_bidir : 1 { false };
            bool mark_stream_active : 1 { false };
            bool tx_start_stream : 1 { false };
            bool uses_reset_wait : 1 { false };
            bool tx_reset_wait_discard{ false };
            bool delete_on_empty{ false };

            DataContextId data_ctx_id{ 0 };
            TransportConnId conn_id{ 0 };

            std::optional<uint64_t> current_stream_id;

            uint8_t priority{ 0 };

            uint64_t in_data_cb_skip_count{ 0 };

            std::unique_ptr<PriorityQueue<ConnData>> tx_data;

            std::shared_ptr<const std::vector<uint8_t>> stream_tx_object;
            size_t stream_tx_object_offset{ 0 };

            uint64_t last_tx_tick{ 0 };

            QuicDataContextMetrics metrics;
        };

        /**
         * Connection context information
         */
        struct ConnectionContext
        {
            TransportConnId conn_id{ 0 };
            uint64_t last_stream_id{ 0 };
            std::optional<uint64_t> control_stream_id{ std::nullopt };

            bool mark_dgram_ready{ false };
            MvfstTransportMode transport_mode{ MvfstTransportMode::kQuic };

            DataContextId next_data_ctx_id{ 1 };

            std::shared_ptr<PriorityQueue<ConnData>> dgram_tx_data;
            std::shared_ptr<SafeQueue<std::shared_ptr<const std::vector<uint8_t>>>> dgram_rx_data;

            struct RxStreamBuffer
            {
                std::shared_ptr<StreamRxContext> rx_ctx;
                bool closed{ false };
                bool checked_once{ false };

                RxStreamBuffer()
                  : rx_ctx(std::make_shared<StreamRxContext>())
                {
                    rx_ctx->caller_any.reset();
                    rx_ctx->data_queue.Clear();
                }
            };

            std::map<uint64_t, RxStreamBuffer> rx_stream_buffer;
            std::map<quicr::DataContextId, DataContext> active_data_contexts;

            char peer_addr_text[45]{ 0 };
            uint16_t peer_port{ 0 };
            sockaddr_storage peer_addr;

            bool is_congested{ false };
            uint16_t not_congested_gauge{ 0 };

            QuicConnectionMetrics metrics;

            // mvfst-specific: socket for this connection
            std::shared_ptr<quic::QuicSocket> quic_socket;

            // Callback handler (client or server)
            std::shared_ptr<MvfstClientCallback> client_callback;
            std::shared_ptr<MvfstServerConnectionCallback> server_callback;

#ifdef HAVE_PROXYGEN_WEBTRANSPORT
            // WebTransport wrapper (when in WebTransport mode)
            std::shared_ptr<proxygen::QuicWebTransport> webtransport;
#endif

            ConnectionContext()
              : dgram_rx_data(std::make_shared<SafeQueue<std::shared_ptr<const std::vector<uint8_t>>>>())
            {
            }
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

        struct MvfstException : public Exception
        {
            using Exception::Exception;
        };

      public:
        /**
         * @brief Constructor for MvfstTransport
         */
        MvfstTransport(const TransportRemote& server,
                       const TransportConfig& tcfg,
                       TransportDelegate& delegate,
                       bool is_server_mode,
                       std::shared_ptr<TickService> tick_service,
                       std::shared_ptr<spdlog::logger> logger,
                       MvfstTransportMode transport_mode = MvfstTransportMode::kQuic);

        virtual ~MvfstTransport();

        // ITransport interface implementation
        TransportStatus Status() const override;
        TransportConnId Start() override;
        void Close(const TransportConnId& conn_id, uint64_t app_reason_code = 100) override;

        bool GetPeerAddrInfo(const TransportConnId& conn_id, sockaddr_storage* addr) override;

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

        void CloseStreamById(TransportConnId conn_id, uint64_t stream_id, bool use_reset) override;

        /*
         * Internal public methods (used by callbacks)
         */
        ConnectionContext* GetConnContext(const TransportConnId& conn_id);
        void SetStatus(TransportStatus status);

        DataContext* CreateDataContextBiDirRecv(TransportConnId conn_id, uint64_t stream_id);
        DataContext* CreateDataContextBiDirRecvInternal(ConnectionContext& conn_ctx, uint64_t stream_id);
        ConnectionContext& CreateConnContext(std::shared_ptr<quic::QuicSocket> socket = nullptr);

        void HandleConnectionReady(TransportConnId conn_id);
        void HandleConnectionError(TransportConnId conn_id, const std::string& error_msg);
        void HandleConnectionEnd(TransportConnId conn_id);
        void HandleNewStream(TransportConnId conn_id, quic::StreamId stream_id, bool is_bidir);
        void HandleStreamData(TransportConnId conn_id, quic::StreamId stream_id);
        void HandleStreamError(TransportConnId conn_id, quic::StreamId stream_id, const std::string& error);
        void HandleDatagramsAvailable(TransportConnId conn_id);

        void OnConnectionStatus(TransportConnId conn_id, TransportStatus status);
        void OnNewConnection(TransportConnId conn_id);

        folly::EventBase* getEventBase() { return &folly_evb_; }
        TransportDelegate& getDelegate() { return delegate_; }

        /*
         * Internal Public Variables
         */
        std::shared_ptr<spdlog::logger> logger;
        bool is_server_mode;
        bool is_unidirectional{ false };
        bool debug{ false };
        MvfstTransportMode transport_mode{ MvfstTransportMode::kQuic };

      private:
        void DeleteDataContextInternal(TransportConnId conn_id, DataContextId data_ctx_id, bool delete_on_empty);

        TransportConnId StartClient();
        TransportConnId StartServer();
        void Shutdown();

        void CreateStream(ConnectionContext& conn_ctx, DataContext* data_ctx);
        void CloseStream(ConnectionContext& conn_ctx, DataContext* data_ctx, bool send_reset);

        void CbNotifier();
        void TxWorker();

        std::shared_ptr<fizz::client::FizzClientContext> createFizzClientContext();
        std::shared_ptr<fizz::server::FizzServerContext> createFizzServerContext();

        /*
         * Variables
         */
        std::atomic<bool> stop_;
        std::mutex state_mutex_;
        std::atomic<TransportStatus> transportStatus_;
        std::thread cbNotifyThread_;
        std::thread txWorkerThread_;

        TransportRemote serverInfo_;
        TransportDelegate& delegate_;
        TransportConfig tconfig_;

        std::map<TransportConnId, ConnectionContext> conn_context_;
        std::shared_ptr<TickService> tick_service_;

        SafeQueue<std::function<void()>> cbNotifyQueue_;

        // folly/mvfst members
        folly::EventBase folly_evb_;
        std::unique_ptr<std::thread> evb_thread_;
        std::shared_ptr<quic::QuicClientTransport> client_transport_;
        std::shared_ptr<quic::QuicServer> quic_server_;

        TransportConnId next_conn_id_{ 1 };

        // ALPN for MOQ
        static constexpr const char* kMoqAlpn = "moq-00";
    };

} // namespace quicr

#endif // USE_MVFST
