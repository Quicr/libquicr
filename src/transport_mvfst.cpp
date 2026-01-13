// SPDX-FileCopyrightText: Copyright (c) 2024 Cisco Systems
// SPDX-License-Identifier: BSD-2-Clause

#ifdef USE_MVFST

#include "transport_mvfst.h"

#include <fizz/client/FizzClientContext.h>
#include <fizz/server/FizzServerContext.h>
#include <fizz/server/CertManager.h>
#include <fizz/protocol/CertificateVerifier.h>
#include <fizz/backend/openssl/certificate/CertUtils.h>
#include <fizz/tool/CertificateVerifiers.h>
#include <quic/fizz/client/handshake/FizzClientQuicHandshakeContext.h>

#ifdef HAVE_PROXYGEN_WEBTRANSPORT
#include <proxygen/lib/http/webtransport/QuicWebTransport.h>
#endif

#include <fstream>
#include <memory>
#include <stdexcept>
#include <utility>

namespace quicr {

// ============================================================================
// MvfstClientCallback Implementation
// ============================================================================

MvfstClientCallback::MvfstClientCallback(MvfstTransport* transport, TransportConnId conn_id)
  : transport_(transport)
  , conn_id_(conn_id)
{
}

void MvfstClientCallback::onConnectionSetupError(quic::QuicError error) noexcept
{
    if (transport_->logger) {
        transport_->logger->error("MvfstClientCallback: Connection setup error: {}",
                                  error.message);
    }
    transport_->HandleConnectionError(conn_id_, error.message);
}

void MvfstClientCallback::onTransportReady() noexcept
{
    if (transport_->logger) {
        transport_->logger->info("MvfstClientCallback: Transport ready for conn {}", conn_id_);
    }
    transport_->HandleConnectionReady(conn_id_);
}

void MvfstClientCallback::onReplaySafe() noexcept
{
    if (transport_->logger) {
        transport_->logger->debug("MvfstClientCallback: Connection is replay safe");
    }
}

void MvfstClientCallback::onNewBidirectionalStream(quic::StreamId id) noexcept
{
    if (transport_->logger) {
        transport_->logger->debug("MvfstClientCallback: New bidir stream {}", id);
    }
    // Set read callback immediately before HandleNewStream to avoid missing data
    if (socket_) {
        socket_->setReadCallback(id, this);
    }
    transport_->HandleNewStream(conn_id_, id, true);
}

void MvfstClientCallback::onNewUnidirectionalStream(quic::StreamId id) noexcept
{
    if (transport_->logger) {
        transport_->logger->debug("MvfstClientCallback: New unidir stream {}", id);
    }
    // Set read callback immediately before HandleNewStream to avoid missing data
    if (socket_) {
        socket_->setReadCallback(id, this);
    }
    transport_->HandleNewStream(conn_id_, id, false);
}

void MvfstClientCallback::onStopSending(quic::StreamId id, quic::ApplicationErrorCode error) noexcept
{
    if (transport_->logger) {
        transport_->logger->debug("MvfstClientCallback: Stop sending on stream {}, error {}",
                                  id, static_cast<uint64_t>(error));
    }
}

void MvfstClientCallback::onConnectionEnd() noexcept
{
    if (transport_->logger) {
        transport_->logger->info("MvfstClientCallback: Connection ended for conn {}", conn_id_);
    }
    transport_->HandleConnectionEnd(conn_id_);
}

void MvfstClientCallback::onConnectionError(quic::QuicError error) noexcept
{
    if (transport_->logger) {
        transport_->logger->error("MvfstClientCallback: Connection error: {}", error.message);
    }
    transport_->HandleConnectionError(conn_id_, error.message);
}

void MvfstClientCallback::readAvailable(quic::StreamId id) noexcept
{
    transport_->HandleStreamData(conn_id_, id);
}

void MvfstClientCallback::readError(quic::StreamId id, quic::QuicError error) noexcept
{
    if (transport_->logger) {
        transport_->logger->warn("MvfstClientCallback: Read error on stream {}: {}",
                                 id, error.message);
    }
    transport_->HandleStreamError(conn_id_, id, error.message);
}

void MvfstClientCallback::onDatagramsAvailable() noexcept
{
    transport_->HandleDatagramsAvailable(conn_id_);
}

// ============================================================================
// MvfstServerTransportFactory Implementation
// ============================================================================

MvfstServerTransportFactory::MvfstServerTransportFactory(MvfstTransport* transport)
  : transport_(transport)
{
}

quic::QuicServerTransport::Ptr MvfstServerTransportFactory::make(
    folly::EventBase* evb,
    std::unique_ptr<folly::AsyncUDPSocket> socket,
    const folly::SocketAddress& addr,
    quic::QuicVersion /*quicVersion*/,
    std::shared_ptr<const fizz::server::FizzServerContext> ctx) noexcept
{
    // Create a new connection context
    auto& conn_ctx = transport_->CreateConnContext();
    TransportConnId conn_id = conn_ctx.conn_id;

    // Store peer address
    conn_ctx.peer_port = addr.getPort();
    strncpy(conn_ctx.peer_addr_text, addr.getAddressStr().c_str(), sizeof(conn_ctx.peer_addr_text) - 1);

    if (transport_->logger) {
        transport_->logger->info("MvfstServerTransportFactory: New connection {} from {}:{}",
                                 conn_id, conn_ctx.peer_addr_text, conn_ctx.peer_port);
    }

    // Create callback handler first (needed for make())
    // Note: We'll set the socket on the callback after creating it
    auto callback = std::make_shared<MvfstServerConnectionCallback>(
        transport_, conn_id, nullptr);

    // Create the server transport with the socket directly
    // QuicServerTransport::make expects folly::AsyncUDPSocket, not the wrapped version
    auto transport = quic::QuicServerTransport::make(
        evb,
        std::move(socket),
        callback.get(),
        callback.get(),
        ctx);

    // Update callback with the socket
    callback->setSocket(transport);
    conn_ctx.server_callback = callback;
    conn_ctx.quic_socket = transport;

    // Set datagram callback
    transport->setDatagramCallback(callback.get());

    return transport;
}

// ============================================================================
// MvfstServerConnectionCallback Implementation
// ============================================================================

MvfstServerConnectionCallback::MvfstServerConnectionCallback(
    MvfstTransport* transport,
    TransportConnId conn_id,
    std::shared_ptr<quic::QuicSocket> socket)
  : transport_(transport)
  , conn_id_(conn_id)
  , socket_(socket)
{
}

void MvfstServerConnectionCallback::onConnectionSetupError(quic::QuicError error) noexcept
{
    if (transport_->logger) {
        transport_->logger->error("MvfstServerConnectionCallback: Setup error: {}", error.message);
    }
    transport_->HandleConnectionError(conn_id_, error.message);
}

void MvfstServerConnectionCallback::onTransportReady() noexcept
{
    if (transport_->logger) {
        transport_->logger->info("MvfstServerConnectionCallback: Transport ready for conn {}", conn_id_);
    }
    transport_->HandleConnectionReady(conn_id_);
    transport_->OnNewConnection(conn_id_);
}

void MvfstServerConnectionCallback::onNewBidirectionalStream(quic::StreamId id) noexcept
{
    if (transport_->logger) {
        transport_->logger->debug("MvfstServerConnectionCallback: New bidir stream {}", id);
    }
    // Set read callback for this stream
    socket_->setReadCallback(id, this);
    transport_->HandleNewStream(conn_id_, id, true);
}

void MvfstServerConnectionCallback::onNewUnidirectionalStream(quic::StreamId id) noexcept
{
    if (transport_->logger) {
        transport_->logger->debug("MvfstServerConnectionCallback: New unidir stream {}", id);
    }
    socket_->setReadCallback(id, this);
    transport_->HandleNewStream(conn_id_, id, false);
}

void MvfstServerConnectionCallback::onStopSending(quic::StreamId id, quic::ApplicationErrorCode error) noexcept
{
    if (transport_->logger) {
        transport_->logger->debug("MvfstServerConnectionCallback: Stop sending on stream {}", id);
    }
    (void)error;
}

void MvfstServerConnectionCallback::onConnectionEnd() noexcept
{
    if (transport_->logger) {
        transport_->logger->info("MvfstServerConnectionCallback: Connection ended for conn {}", conn_id_);
    }
    transport_->HandleConnectionEnd(conn_id_);
}

void MvfstServerConnectionCallback::onConnectionError(quic::QuicError error) noexcept
{
    if (transport_->logger) {
        transport_->logger->error("MvfstServerConnectionCallback: Connection error: {}", error.message);
    }
    transport_->HandleConnectionError(conn_id_, error.message);
}

void MvfstServerConnectionCallback::readAvailable(quic::StreamId id) noexcept
{
    transport_->HandleStreamData(conn_id_, id);
}

void MvfstServerConnectionCallback::readError(quic::StreamId id, quic::QuicError error) noexcept
{
    if (transport_->logger) {
        transport_->logger->warn("MvfstServerConnectionCallback: Read error on stream {}: {}",
                                 id, error.message);
    }
    transport_->HandleStreamError(conn_id_, id, error.message);
}

void MvfstServerConnectionCallback::onDatagramsAvailable() noexcept
{
    transport_->HandleDatagramsAvailable(conn_id_);
}

// ============================================================================
// MvfstTransport Constructor / Destructor
// ============================================================================

MvfstTransport::MvfstTransport(const TransportRemote& server,
                               const TransportConfig& tcfg,
                               TransportDelegate& delegate,
                               bool is_server_mode,
                               std::shared_ptr<TickService> tick_service,
                               std::shared_ptr<spdlog::logger> logger,
                               MvfstTransportMode transport_mode)
  : logger(std::move(logger))
  , is_server_mode(is_server_mode)
  , transport_mode(transport_mode)
  , stop_(false)
  , transportStatus_(TransportStatus::kDisconnected)
  , serverInfo_(server)
  , delegate_(delegate)
  , tconfig_(tcfg)
  , tick_service_(std::move(tick_service))
{
    debug = tcfg.debug;

    // OpenSSL 1.1.0+ handles thread-safe initialization automatically

    if (this->logger) {
        this->logger->info("MvfstTransport: Initializing {} mode for {}:{}, transport_mode={}",
                           is_server_mode ? "server" : "client",
                           server.host_or_ip, server.port,
                           static_cast<int>(transport_mode));
    }
}

MvfstTransport::~MvfstTransport()
{
    Shutdown();
}

// ============================================================================
// ITransport Interface Implementation
// ============================================================================

TransportStatus MvfstTransport::Status() const
{
    return transportStatus_.load();
}

TransportConnId MvfstTransport::Start()
{
    if (is_server_mode) {
        return StartServer();
    }
    return StartClient();
}

TransportConnId MvfstTransport::StartClient()
{
    if (logger) {
        logger->info("MvfstTransport: Starting client connection to {}:{}",
                     serverInfo_.host_or_ip, serverInfo_.port);
    }

    SetStatus(TransportStatus::kConnecting);

    // Create connection context first
    auto& conn_ctx = CreateConnContext();
    TransportConnId conn_id = conn_ctx.conn_id;

    // Create callback handler
    auto callback = std::make_shared<MvfstClientCallback>(this, conn_id);
    conn_ctx.client_callback = callback;

    // Start event base thread
    evb_thread_ = std::make_unique<std::thread>([this, conn_id, callback]() {
        // Create socket address
        folly::SocketAddress serverAddr(serverInfo_.host_or_ip, serverInfo_.port, true);

        // Create fizz client context
        auto fizzCtx = createFizzClientContext();

        // Create handshake context (FizzClientQuicHandshakeContext is a ClientHandshakeFactory)
        auto builder = quic::FizzClientQuicHandshakeContext::Builder()
            .setFizzClientContext(fizzCtx);

        // Set certificate verifier for self-signed certs if requested
        if (tconfig_.tls_skip_verify) {
            builder = std::move(builder).setCertificateVerifier(
                std::make_shared<fizz::InsecureAcceptAnyCertificate>());
        }

        auto handshakeCtx = std::move(builder).build();

        // Create QuicEventBase wrapper around folly::EventBase
        auto qEvb = std::make_shared<quic::FollyQuicEventBase>(&folly_evb_);

        // Create UDP socket with the QuicEventBase
        auto sock = std::make_unique<quic::FollyQuicAsyncUDPSocket>(qEvb);

        // Create client transport with new API
        client_transport_ = std::make_shared<quic::QuicClientTransport>(
            qEvb,
            std::move(sock),
            std::move(handshakeCtx));

        // Configure transport settings
        quic::TransportSettings settings;
        settings.datagramConfig.enabled = true;
        settings.datagramConfig.readBufSize = 1500;
        settings.datagramConfig.writeBufSize = 1500;
        settings.idleTimeout = std::chrono::milliseconds(tconfig_.idle_timeout_ms);
        client_transport_->setTransportSettings(settings);

        // Use standard QUIC v1 for interoperability with non-mvfst servers (e.g., picoquic)
        client_transport_->setSupportedVersions({quic::QuicVersion::QUIC_V1});

        // Set hostname and server address
        client_transport_->setHostname(serverInfo_.host_or_ip);
        client_transport_->addNewPeerAddress(serverAddr);

        // ALPN is set on FizzClientContext in createFizzClientContext()

        // Store socket in connection context and set on callback
        {
            std::lock_guard<std::mutex> lock(state_mutex_);
            auto* ctx = GetConnContext(conn_id);
            if (ctx) {
                ctx->quic_socket = client_transport_;
            }
        }

        // Set socket on callback so it can set read callbacks directly
        callback->setSocket(client_transport_);

        // Set datagram callback
        client_transport_->setDatagramCallback(callback.get());

        // Start the connection
        client_transport_->start(callback.get(), callback.get());

        // Run event loop
        folly_evb_.loopForever();
    });

    // Start callback notifier thread
    cbNotifyThread_ = std::thread(&MvfstTransport::CbNotifier, this);

    // Start TX worker thread
    txWorkerThread_ = std::thread(&MvfstTransport::TxWorker, this);

    return conn_id;
}

TransportConnId MvfstTransport::StartServer()
{
    if (logger) {
        logger->info("MvfstTransport: Starting server on {}:{}", serverInfo_.host_or_ip, serverInfo_.port);
    }

    SetStatus(TransportStatus::kConnecting);

    // Create a placeholder connection context for server
    auto& conn_ctx = CreateConnContext();
    TransportConnId server_conn_id = conn_ctx.conn_id;

    // Start event base thread with server
    evb_thread_ = std::make_unique<std::thread>([this]() {
        // Configure transport settings
        quic::TransportSettings settings;
        settings.datagramConfig.enabled = true;
        settings.datagramConfig.readBufSize = 1500;
        settings.datagramConfig.writeBufSize = 1500;
        settings.idleTimeout = std::chrono::milliseconds(tconfig_.idle_timeout_ms);

        // Create server with settings
        quic_server_ = quic::QuicServer::createQuicServer(settings);

        // Create fizz server context
        auto fizzCtx = createFizzServerContext();

        // Set fizz context
        quic_server_->setFizzContext(fizzCtx);

        // Set supported QUIC version
        quic_server_->setSupportedVersion({quic::QuicVersion::QUIC_V1});

        // Set transport factory
        quic_server_->setQuicServerTransportFactory(
            std::make_unique<MvfstServerTransportFactory>(this));

        // Create socket address
        folly::SocketAddress addr(serverInfo_.host_or_ip, serverInfo_.port, true);

        // Start the server
        quic_server_->start(addr, 1);  // 1 worker thread

        SetStatus(TransportStatus::kReady);

        if (logger) {
            logger->info("MvfstTransport: Server started on {}:{}", serverInfo_.host_or_ip, serverInfo_.port);
        }

        // Run event loop
        folly_evb_.loopForever();
    });

    // Start callback notifier thread
    cbNotifyThread_ = std::thread(&MvfstTransport::CbNotifier, this);

    // Start TX worker thread
    txWorkerThread_ = std::thread(&MvfstTransport::TxWorker, this);

    return server_conn_id;
}

void MvfstTransport::Close(const TransportConnId& conn_id, uint64_t app_reason_code)
{
    std::lock_guard<std::mutex> lock(state_mutex_);

    auto it = conn_context_.find(conn_id);
    if (it == conn_context_.end()) {
        if (logger) {
            logger->warn("MvfstTransport::Close: Connection {} not found", conn_id);
        }
        return;
    }

    if (logger) {
        logger->info("MvfstTransport::Close: Closing connection {} with reason {}", conn_id, app_reason_code);
    }

    // Close the QUIC socket
    if (it->second.quic_socket) {
        folly_evb_.runInEventBaseThread([socket = it->second.quic_socket, app_reason_code]() {
            socket->close(quic::QuicError(
                quic::ApplicationErrorCode{app_reason_code},
                std::to_string(app_reason_code)));
        });
    }

    conn_context_.erase(it);
}

void MvfstTransport::Shutdown()
{
    if (stop_.exchange(true)) {
        return; // Already stopped
    }

    if (logger) {
        logger->info("MvfstTransport: Shutting down");
    }

    SetStatus(TransportStatus::kShuttingDown);

    // Stop event loop
    folly_evb_.terminateLoopSoon();

    // Stop server if running
    if (quic_server_) {
        quic_server_->shutdown();
    }

    // Unblock the callback notifier thread
    cbNotifyQueue_.StopWaiting();

    // Join event base thread
    if (evb_thread_ && evb_thread_->joinable()) {
        evb_thread_->join();
    }

    // Join callback notifier thread
    if (cbNotifyThread_.joinable()) {
        cbNotifyThread_.join();
    }

    // Join TX worker thread
    if (txWorkerThread_.joinable()) {
        txWorkerThread_.join();
    }

    // Clear all connections
    {
        std::lock_guard<std::mutex> lock(state_mutex_);
        conn_context_.clear();
    }

    SetStatus(TransportStatus::kShutdown);
}

bool MvfstTransport::GetPeerAddrInfo(const TransportConnId& conn_id, sockaddr_storage* addr)
{
    std::lock_guard<std::mutex> lock(state_mutex_);

    auto it = conn_context_.find(conn_id);
    if (it == conn_context_.end()) {
        return false;
    }

    *addr = it->second.peer_addr;
    return true;
}

DataContextId MvfstTransport::CreateDataContext(TransportConnId conn_id,
                                                bool /*use_reliable_transport*/,
                                                uint8_t priority,
                                                bool bidir)
{
    std::lock_guard<std::mutex> lock(state_mutex_);

    auto conn_it = conn_context_.find(conn_id);
    if (conn_it == conn_context_.end()) {
        throw TransportException(TransportError::kInvalidConnContextId);
    }

    auto& conn_ctx = conn_it->second;
    DataContextId data_ctx_id = conn_ctx.next_data_ctx_id++;

    DataContext data_ctx;
    data_ctx.data_ctx_id = data_ctx_id;
    data_ctx.conn_id = conn_id;
    data_ctx.is_bidir = bidir;
    data_ctx.priority = priority;
    data_ctx.tx_data = std::make_unique<PriorityQueue<ConnData>>(tconfig_.time_queue_max_duration,
                                                                    tconfig_.time_queue_bucket_interval,
                                                                    tick_service_,
                                                                    tconfig_.time_queue_init_queue_size);

    conn_ctx.active_data_contexts.emplace(data_ctx_id, std::move(data_ctx));

    if (logger) {
        logger->debug("MvfstTransport: Created data context {} for connection {}", data_ctx_id, conn_id);
    }

    return data_ctx_id;
}

void MvfstTransport::DeleteDataContext(const TransportConnId& conn_id,
                                       DataContextId data_ctx_id,
                                       bool delete_on_empty)
{
    DeleteDataContextInternal(conn_id, data_ctx_id, delete_on_empty);
}

void MvfstTransport::DeleteDataContextInternal(TransportConnId conn_id,
                                               DataContextId data_ctx_id,
                                               bool delete_on_empty)
{
    std::lock_guard<std::mutex> lock(state_mutex_);

    auto conn_it = conn_context_.find(conn_id);
    if (conn_it == conn_context_.end()) {
        return;
    }

    auto& conn_ctx = conn_it->second;
    auto data_it = conn_ctx.active_data_contexts.find(data_ctx_id);
    if (data_it == conn_ctx.active_data_contexts.end()) {
        return;
    }

    if (delete_on_empty) {
        data_it->second.delete_on_empty = true;
    } else {
        // Close any associated stream
        if (data_it->second.current_stream_id && conn_ctx.quic_socket) {
            auto stream_id = *data_it->second.current_stream_id;
            folly_evb_.runInEventBaseThread([socket = conn_ctx.quic_socket, stream_id]() {
                socket->resetStream(stream_id, quic::GenericApplicationErrorCode::UNKNOWN);
            });
        }
        conn_ctx.active_data_contexts.erase(data_it);
    }
}

TransportError MvfstTransport::Enqueue(const TransportConnId& conn_id,
                                       const DataContextId& data_ctx_id,
                                       std::uint64_t group_id,
                                       std::shared_ptr<const std::vector<uint8_t>> bytes,
                                       uint8_t priority,
                                       uint32_t ttl_ms,
                                       uint32_t /*delay_ms*/,
                                       EnqueueFlags flags)
{
    std::lock_guard<std::mutex> lock(state_mutex_);

    auto conn_it = conn_context_.find(conn_id);
    if (conn_it == conn_context_.end()) {
        return TransportError::kInvalidConnContextId;
    }

    auto& conn_ctx = conn_it->second;

    // Queue the data for transmission
    ConnData conn_data;
    conn_data.conn_id = conn_id;
    conn_data.data_ctx_id = data_ctx_id;
    conn_data.priority = priority;
    conn_data.data = bytes;
    conn_data.tick_microseconds = tick_service_ ? tick_service_->Microseconds() : 0;

    if (flags.use_reliable) {
        // Stream-based transport
        auto data_it = conn_ctx.active_data_contexts.find(data_ctx_id);
        if (data_it == conn_ctx.active_data_contexts.end()) {
            return TransportError::kInvalidDataContextId;
        }

        auto& data_ctx = data_it->second;

        if (flags.new_stream) {
            data_ctx.tx_start_stream = true;
        }

        if (flags.clear_tx_queue && data_ctx.tx_data) {
            data_ctx.tx_data->Clear();
        }

        if (data_ctx.tx_data) {
            data_ctx.tx_data->Push(group_id ? group_id : data_ctx_id, conn_data, ttl_ms ? ttl_ms : 1000, priority, 0);
        }
    } else {
        // Datagram-based transport
        if (conn_ctx.dgram_tx_data) {
            conn_ctx.dgram_tx_data->Push(group_id ? group_id : data_ctx_id, conn_data, ttl_ms ? ttl_ms : 350, priority, 0);
            conn_ctx.mark_dgram_ready = true;
        }
    }

    return TransportError::kNone;
}

std::shared_ptr<const std::vector<uint8_t>> MvfstTransport::Dequeue(TransportConnId conn_id,
                                                                    std::optional<DataContextId> /*data_ctx_id*/)
{
    std::lock_guard<std::mutex> lock(state_mutex_);

    auto conn_it = conn_context_.find(conn_id);
    if (conn_it == conn_context_.end()) {
        return nullptr;
    }

    auto& conn_ctx = conn_it->second;

    // Dequeue from datagram queue
    if (conn_ctx.dgram_rx_data && !conn_ctx.dgram_rx_data->Empty()) {
        auto result = conn_ctx.dgram_rx_data->Pop();
        if (result) {
            return *result;
        }
    }

    return nullptr;
}

std::shared_ptr<StreamRxContext> MvfstTransport::GetStreamRxContext(TransportConnId conn_id, uint64_t stream_id)
{
    std::lock_guard<std::mutex> lock(state_mutex_);

    auto conn_it = conn_context_.find(conn_id);
    if (conn_it == conn_context_.end()) {
        throw TransportException(TransportError::kInvalidConnContextId);
    }

    auto& conn_ctx = conn_it->second;
    auto stream_it = conn_ctx.rx_stream_buffer.find(stream_id);
    if (stream_it == conn_ctx.rx_stream_buffer.end()) {
        throw TransportException(TransportError::kInvalidStreamId);
    }

    return stream_it->second.rx_ctx;
}

int MvfstTransport::CloseWebTransportSession(TransportConnId conn_id,
                                             uint32_t error_code,
                                             const char* error_msg)
{
    if (transport_mode != MvfstTransportMode::kWebTransport) {
        if (logger) {
            logger->warn("MvfstTransport::CloseWebTransportSession: conn_id={} is not in WebTransport mode",
                         conn_id);
        }
        return -1;
    }

    if (logger) {
        logger->info("MvfstTransport::CloseWebTransportSession: conn_id={} error_code={} msg={}",
                     conn_id, error_code, error_msg ? error_msg : "(none)");
    }

#ifdef HAVE_PROXYGEN_WEBTRANSPORT
    std::lock_guard<std::mutex> lock(state_mutex_);
    auto* conn_ctx = GetConnContext(conn_id);
    if (!conn_ctx) {
        if (logger) {
            logger->warn("MvfstTransport::CloseWebTransportSession: Connection {} not found", conn_id);
        }
        return -1;
    }

    if (conn_ctx->webtransport) {
        // Use proxygen's WebTransport closeSession which properly handles
        // WebTransport protocol close semantics
        auto result = static_cast<proxygen::WebTransport*>(conn_ctx->webtransport.get())->closeSession(
            error_code == 0 ? folly::none : folly::Optional<uint32_t>(error_code));

        if (result.hasError()) {
            if (logger) {
                logger->warn("MvfstTransport::CloseWebTransportSession: Failed to close session, error={}",
                             static_cast<int>(result.error()));
            }
            return -1;
        }

        if (logger) {
            logger->info("MvfstTransport::CloseWebTransportSession: Session closed via proxygen WebTransport");
        }
        return 0;
    }
#endif

    // Fallback: close the underlying QUIC connection
    if (logger) {
        logger->info("MvfstTransport::CloseWebTransportSession: Falling back to QUIC connection close");
    }
    Close(conn_id, error_code);
    return 0;
}

int MvfstTransport::DrainWebTransportSession(TransportConnId conn_id)
{
    if (transport_mode != MvfstTransportMode::kWebTransport) {
        if (logger) {
            logger->warn("MvfstTransport::DrainWebTransportSession: conn_id={} is not in WebTransport mode",
                         conn_id);
        }
        return -1;
    }

    if (logger) {
        logger->info("MvfstTransport::DrainWebTransportSession: conn_id={}", conn_id);
    }

#ifdef HAVE_PROXYGEN_WEBTRANSPORT
    std::lock_guard<std::mutex> lock(state_mutex_);
    auto* conn_ctx = GetConnContext(conn_id);
    if (!conn_ctx) {
        if (logger) {
            logger->warn("MvfstTransport::DrainWebTransportSession: Connection {} not found", conn_id);
        }
        return -1;
    }

    if (conn_ctx->webtransport) {
        // Note: proxygen's QuicWebTransport doesn't have a separate drain API
        // The drain semantics in WebTransport mean "stop accepting new streams"
        // which is handled via HTTP/3 GOAWAY. For now we just close cleanly.
        if (logger) {
            logger->info("MvfstTransport::DrainWebTransportSession: Initiating graceful close via proxygen");
        }
        auto result = static_cast<proxygen::WebTransport*>(conn_ctx->webtransport.get())->closeSession(folly::none);
        if (result.hasError()) {
            if (logger) {
                logger->warn("MvfstTransport::DrainWebTransportSession: Drain failed");
            }
            return -1;
        }
        return 0;
    }
#endif

    // Without proxygen, drain is a no-op but returns success
    if (logger) {
        logger->info("MvfstTransport::DrainWebTransportSession: No proxygen support, session continues normally");
    }
    return 0;
}

void MvfstTransport::SetRemoteDataCtxId([[maybe_unused]] TransportConnId conn_id,
                                        [[maybe_unused]] DataContextId data_ctx_id,
                                        [[maybe_unused]] DataContextId remote_data_ctx_id)
{
    // Remote data context ID mapping is not currently used in mvfst transport
    // This matches the behavior of picoquic transport
    return;
}

void MvfstTransport::SetStreamIdDataCtxId(TransportConnId conn_id, DataContextId data_ctx_id, uint64_t stream_id)
{
    std::lock_guard<std::mutex> lock(state_mutex_);

    auto conn_it = conn_context_.find(conn_id);
    if (conn_it == conn_context_.end()) {
        return;
    }

    auto& conn_ctx = conn_it->second;
    auto data_it = conn_ctx.active_data_contexts.find(data_ctx_id);
    if (data_it == conn_ctx.active_data_contexts.end()) {
        return;
    }

    data_it->second.current_stream_id = stream_id;
}

void MvfstTransport::SetDataCtxPriority(TransportConnId conn_id, DataContextId data_ctx_id, uint8_t priority)
{
    std::lock_guard<std::mutex> lock(state_mutex_);

    auto conn_it = conn_context_.find(conn_id);
    if (conn_it == conn_context_.end()) {
        return;
    }

    auto& conn_ctx = conn_it->second;
    auto data_it = conn_ctx.active_data_contexts.find(data_ctx_id);
    if (data_it == conn_ctx.active_data_contexts.end()) {
        return;
    }

    data_it->second.priority = priority;
}

void MvfstTransport::CloseStreamById(TransportConnId conn_id, uint64_t stream_id, bool use_reset)
{
    std::lock_guard<std::mutex> lock(state_mutex_);

    auto conn_it = conn_context_.find(conn_id);
    if (conn_it == conn_context_.end()) {
        return;
    }

    auto& conn_ctx = conn_it->second;

    if (conn_ctx.quic_socket) {
        folly_evb_.runInEventBaseThread([socket = conn_ctx.quic_socket, stream_id, use_reset]() {
            if (use_reset) {
                socket->resetStream(stream_id, quic::GenericApplicationErrorCode::UNKNOWN);
            } else {
                // Send FIN by writing empty buffer with EOF
                auto buf = folly::IOBuf::create(0);
                socket->writeChain(stream_id, std::move(buf), true /* eof */);
            }
        });
    }

    if (logger) {
        logger->debug("MvfstTransport::CloseStreamById: conn={}, stream={}, reset={}",
                      conn_id, stream_id, use_reset);
    }
}

// ============================================================================
// Internal Methods
// ============================================================================

MvfstTransport::ConnectionContext* MvfstTransport::GetConnContext(const TransportConnId& conn_id)
{
    auto it = conn_context_.find(conn_id);
    if (it == conn_context_.end()) {
        return nullptr;
    }
    return &it->second;
}

void MvfstTransport::SetStatus(TransportStatus status)
{
    transportStatus_.store(status);
}

MvfstTransport::DataContext* MvfstTransport::CreateDataContextBiDirRecvInternal(
    ConnectionContext& conn_ctx, uint64_t stream_id)
{
    // NOTE: Caller must hold state_mutex_
    DataContextId data_ctx_id = conn_ctx.next_data_ctx_id++;

    DataContext data_ctx;
    data_ctx.data_ctx_id = data_ctx_id;
    data_ctx.conn_id = conn_ctx.conn_id;
    data_ctx.is_bidir = true;
    data_ctx.current_stream_id = stream_id;
    data_ctx.tx_data = std::make_unique<PriorityQueue<ConnData>>(tconfig_.time_queue_max_duration,
                                                                    tconfig_.time_queue_bucket_interval,
                                                                    tick_service_,
                                                                    tconfig_.time_queue_init_queue_size);

    auto [it, inserted] = conn_ctx.active_data_contexts.emplace(data_ctx_id, std::move(data_ctx));
    if (!inserted) {
        return nullptr;
    }

    return &it->second;
}

MvfstTransport::DataContext* MvfstTransport::CreateDataContextBiDirRecv(TransportConnId conn_id, uint64_t stream_id)
{
    std::lock_guard<std::mutex> lock(state_mutex_);

    auto conn_it = conn_context_.find(conn_id);
    if (conn_it == conn_context_.end()) {
        return nullptr;
    }

    return CreateDataContextBiDirRecvInternal(conn_it->second, stream_id);
}

MvfstTransport::ConnectionContext& MvfstTransport::CreateConnContext(std::shared_ptr<quic::QuicSocket> socket)
{
    TransportConnId conn_id = next_conn_id_++;

    ConnectionContext conn_ctx;
    conn_ctx.conn_id = conn_id;
    conn_ctx.transport_mode = transport_mode;
    conn_ctx.dgram_tx_data = std::make_shared<PriorityQueue<ConnData>>(tconfig_.time_queue_max_duration,
                                                                       tconfig_.time_queue_bucket_interval,
                                                                       tick_service_,
                                                                       tconfig_.time_queue_init_queue_size);
    conn_ctx.quic_socket = socket;

#ifdef HAVE_PROXYGEN_WEBTRANSPORT
    // Initialize proxygen WebTransport wrapper when in WebTransport mode
    if (transport_mode == MvfstTransportMode::kWebTransport && socket) {
        conn_ctx.webtransport = std::make_shared<proxygen::QuicWebTransport>(socket);
        if (logger) {
            logger->debug("MvfstTransport: Created QuicWebTransport wrapper for conn_id={}", conn_id);
        }
    }
#endif

    auto [it, inserted] = conn_context_.emplace(conn_id, std::move(conn_ctx));
    return it->second;
}

void MvfstTransport::HandleConnectionReady(TransportConnId conn_id)
{
    SetStatus(TransportStatus::kReady);
    OnConnectionStatus(conn_id, TransportStatus::kReady);
}

void MvfstTransport::HandleConnectionError(TransportConnId conn_id, const std::string& error_msg)
{
    if (logger) {
        logger->error("MvfstTransport: Connection {} error: {}", conn_id, error_msg);
    }
    SetStatus(TransportStatus::kDisconnected);
    OnConnectionStatus(conn_id, TransportStatus::kDisconnected);
}

void MvfstTransport::HandleConnectionEnd(TransportConnId conn_id)
{
    SetStatus(TransportStatus::kDisconnected);
    OnConnectionStatus(conn_id, TransportStatus::kDisconnected);
}

void MvfstTransport::HandleNewStream(TransportConnId conn_id, quic::StreamId stream_id, bool is_bidir)
{
    std::lock_guard<std::mutex> lock(state_mutex_);

    auto* conn_ctx = GetConnContext(conn_id);
    if (!conn_ctx) {
        return;
    }

    // Create RX buffer for this stream
    conn_ctx->rx_stream_buffer[stream_id] = ConnectionContext::RxStreamBuffer();

    // Note: For client-side, the read callback is set directly in onNewUnidirectionalStream/
    // onNewBidirectionalStream before calling this function to ensure we don't miss data.
    // For server-side, it's also set directly in MvfstServerConnectionCallback.

    // Create data context for bidir streams
    if (is_bidir) {
        // Use internal version since we already hold the lock
        auto* data_ctx = CreateDataContextBiDirRecvInternal(*conn_ctx, stream_id);
        if (data_ctx) {
            cbNotifyQueue_.Push([this, conn_id, data_ctx_id = data_ctx->data_ctx_id]() {
                delegate_.OnNewDataContext(conn_id, data_ctx_id);
            });
        }
    }
}

void MvfstTransport::HandleStreamData(TransportConnId conn_id, quic::StreamId stream_id)
{
    std::shared_ptr<quic::QuicSocket> socket;
    {
        std::lock_guard<std::mutex> lock(state_mutex_);
        auto* conn_ctx = GetConnContext(conn_id);
        if (!conn_ctx || !conn_ctx->quic_socket) {
            return;
        }
        socket = conn_ctx->quic_socket;
    }

    // Read data from stream - must be done in event base thread
    folly_evb_.runInEventBaseThread([this, conn_id, stream_id, socket]() {
        auto readResult = socket->read(stream_id, 64 * 1024);
        if (readResult.hasError()) {
            if (logger) {
                logger->warn("MvfstTransport: Read error on stream {}", stream_id);
            }
            return;
        }

        auto& data = readResult.value();
        bool eof = data.second;

        if (data.first) {
            // Convert IOBuf to vector
            auto iobuf = std::move(data.first);
            auto bytes = std::make_shared<std::vector<uint8_t>>();
            bytes->reserve(iobuf->computeChainDataLength());

            for (auto& buf : *iobuf) {
                bytes->insert(bytes->end(), buf.begin(), buf.end());
            }

            // Store in RX buffer
            {
                std::lock_guard<std::mutex> lock(state_mutex_);
                auto* conn_ctx = GetConnContext(conn_id);
                if (conn_ctx) {
                    auto& rx_buf = conn_ctx->rx_stream_buffer[stream_id];
                    rx_buf.rx_ctx->data_queue.Push(bytes);
                    // Note: Do NOT set is_new = false here. The is_new flag is used by
                    // Transport::OnRecvStream to detect the first data on a stream and
                    // process the stream header. It will be set to false after header
                    // processing in transport.cpp (OnRecvSubgroup/OnRecvFetch).
                    if (eof) {
                        rx_buf.closed = true;
                    }
                }
            }

            // Notify delegate
            // QUIC stream ID encoding: bit 1 indicates unidirectional (0=bidir, 1=unidir)
            bool is_bidir = (stream_id & 0x02) == 0;
            cbNotifyQueue_.Push([this, conn_id, stream_id, is_bidir]() {
                delegate_.OnRecvStream(conn_id, stream_id, std::nullopt, is_bidir);
            });
        }

        if (eof) {
            std::lock_guard<std::mutex> lock(state_mutex_);
            auto* conn_ctx = GetConnContext(conn_id);
            if (conn_ctx) {
                auto stream_it = conn_ctx->rx_stream_buffer.find(stream_id);
                if (stream_it != conn_ctx->rx_stream_buffer.end()) {
                    cbNotifyQueue_.Push([this, conn_id, stream_id,
                                         rx_ctx = stream_it->second.rx_ctx]() {
                        delegate_.OnStreamClosed(conn_id, stream_id, rx_ctx, StreamClosedFlag::Fin);
                    });
                }
            }
        }
    });
}

void MvfstTransport::HandleStreamError(TransportConnId conn_id, quic::StreamId stream_id, const std::string& /*error*/)
{
    std::lock_guard<std::mutex> lock(state_mutex_);

    auto* conn_ctx = GetConnContext(conn_id);
    if (!conn_ctx) {
        return;
    }

    auto stream_it = conn_ctx->rx_stream_buffer.find(stream_id);
    if (stream_it != conn_ctx->rx_stream_buffer.end()) {
        stream_it->second.closed = true;

        cbNotifyQueue_.Push([this, conn_id, stream_id, rx_ctx = stream_it->second.rx_ctx]() {
            delegate_.OnStreamClosed(conn_id, stream_id, rx_ctx, StreamClosedFlag::Reset);
        });
    }
}

void MvfstTransport::HandleDatagramsAvailable(TransportConnId conn_id)
{
    std::shared_ptr<quic::QuicSocket> socket;
    {
        std::lock_guard<std::mutex> lock(state_mutex_);
        auto* conn_ctx = GetConnContext(conn_id);
        if (!conn_ctx || !conn_ctx->quic_socket) {
            return;
        }
        socket = conn_ctx->quic_socket;
    }

    // Read datagrams in event base thread
    folly_evb_.runInEventBaseThread([this, conn_id, socket]() {
        auto result = socket->readDatagrams();
        if (result.hasError()) {
            return;
        }

        for (auto& datagram : *result) {
            auto& bufQueue = datagram.bufQueue();
            if (!bufQueue.empty()) {
                auto iobuf = bufQueue.front()->cloneCoalesced();

                auto bytes = std::make_shared<std::vector<uint8_t>>();
                bytes->reserve(iobuf->computeChainDataLength());
                for (auto& buf : *iobuf) {
                    bytes->insert(bytes->end(), buf.begin(), buf.end());
                }

                // Store in RX queue
                {
                    std::lock_guard<std::mutex> lock(state_mutex_);
                    auto* conn_ctx = GetConnContext(conn_id);
                    if (conn_ctx && conn_ctx->dgram_rx_data) {
                        conn_ctx->dgram_rx_data->Push(bytes);
                    }
                }
            }
        }

        // Notify delegate
        cbNotifyQueue_.Push([this, conn_id]() {
            delegate_.OnRecvDgram(conn_id, std::nullopt);
        });
    });
}

void MvfstTransport::OnConnectionStatus(TransportConnId conn_id, TransportStatus status)
{
    cbNotifyQueue_.Push([this, conn_id, status]() {
        delegate_.OnConnectionStatus(conn_id, status);
    });
}

void MvfstTransport::OnNewConnection(TransportConnId conn_id)
{
    auto* conn_ctx = GetConnContext(conn_id);
    if (!conn_ctx) {
        return;
    }

    TransportRemote remote;
    remote.host_or_ip = conn_ctx->peer_addr_text;
    remote.port = conn_ctx->peer_port;

    cbNotifyQueue_.Push([this, conn_id, remote]() {
        delegate_.OnNewConnection(conn_id, remote);
    });
}

void MvfstTransport::CreateStream(ConnectionContext& conn_ctx, DataContext* data_ctx)
{
    if (!conn_ctx.quic_socket || !data_ctx) {
        return;
    }

    folly_evb_.runInEventBaseThread([this, socket = conn_ctx.quic_socket,
                                      data_ctx_id = data_ctx->data_ctx_id,
                                      conn_id = conn_ctx.conn_id,
                                      is_bidir = data_ctx->is_bidir]() {
        auto streamResult = is_bidir
            ? socket->createBidirectionalStream()
            : socket->createUnidirectionalStream();

        if (streamResult.hasError()) {
            if (logger) {
                logger->error("MvfstTransport: Failed to create stream");
            }
            return;
        }

        quic::StreamId stream_id = streamResult.value();

        // Update data context with stream ID and set up RX handling for bidir streams
        MvfstClientCallback* client_callback = nullptr;
        {
            std::lock_guard<std::mutex> lock(state_mutex_);
            auto* conn_ctx = GetConnContext(conn_id);
            if (conn_ctx) {
                auto data_it = conn_ctx->active_data_contexts.find(data_ctx_id);
                if (data_it != conn_ctx->active_data_contexts.end()) {
                    data_it->second.current_stream_id = stream_id;
                }

                // For bidir streams, set up RX handling to receive responses
                if (is_bidir) {
                    conn_ctx->rx_stream_buffer[stream_id] = ConnectionContext::RxStreamBuffer();
                    if (conn_ctx->client_callback) {
                        client_callback = conn_ctx->client_callback.get();
                    }
                }
            }
        }

        // Set read callback for bidir streams to receive responses (outside lock)
        if (is_bidir && client_callback) {
            socket->setReadCallback(stream_id, client_callback);
        }

        if (logger) {
            logger->debug("MvfstTransport: Created {} stream {} for data context {} conn {}",
                          is_bidir ? "bidir" : "unidir", stream_id, data_ctx_id, conn_id);
        }
    });
}

void MvfstTransport::CloseStream(ConnectionContext& conn_ctx, DataContext* data_ctx, bool send_reset)
{
    if (!conn_ctx.quic_socket || !data_ctx || !data_ctx->current_stream_id) {
        return;
    }

    auto stream_id = *data_ctx->current_stream_id;

    if (logger) {
        logger->debug("MvfstTransport: CloseStream stream {} send_reset={}", stream_id, send_reset);
    }

    folly_evb_.runInEventBaseThread([socket = conn_ctx.quic_socket, stream_id, send_reset]() {
        if (send_reset) {
            socket->resetStream(stream_id, quic::GenericApplicationErrorCode::UNKNOWN);
        } else {
            auto buf = folly::IOBuf::create(0);
            socket->writeChain(stream_id, std::move(buf), true /* eof */);
        }
    });

    data_ctx->current_stream_id.reset();
}

void MvfstTransport::CbNotifier()
{
    while (!stop_) {
        auto cb = cbNotifyQueue_.BlockPop();
        if (cb) {
            (*cb)();
        }
    }

    // Drain remaining callbacks
    while (!cbNotifyQueue_.Empty()) {
        auto cb = cbNotifyQueue_.Pop();
        if (cb) {
            (*cb)();
        }
    }
}

void MvfstTransport::TxWorker()
{
    while (!stop_) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));

        std::lock_guard<std::mutex> lock(state_mutex_);

        for (auto& [conn_id, conn_ctx] : conn_context_) {
            if (!conn_ctx.quic_socket) {
                continue;
            }

            // Process data contexts
            for (auto& [data_ctx_id, data_ctx] : conn_ctx.active_data_contexts) {
                if (!data_ctx.tx_data || data_ctx.tx_data->Empty()) {
                    continue;
                }

                // Create stream if needed
                if (!data_ctx.current_stream_id || data_ctx.tx_start_stream) {
                    data_ctx.tx_start_stream = false;
                    CreateStream(conn_ctx, &data_ctx);
                    continue; // Wait for stream creation
                }

                // Pop data and send
                TimeQueueElement<ConnData> elem;
                data_ctx.tx_data->PopFront(elem);
                if (!elem.has_value || !elem.value.data) {
                    continue;
                }

                auto stream_id = *data_ctx.current_stream_id;
                auto data = elem.value.data;

                if (logger) {
                    logger->debug("MvfstTransport: Writing {} bytes to stream {} conn {}",
                                  data->size(), stream_id, conn_id);
                }

                folly_evb_.runInEventBaseThread([this, socket = conn_ctx.quic_socket,
                                                  stream_id, data]() {
                    auto buf = folly::IOBuf::copyBuffer(data->data(), data->size());
                    auto result = socket->writeChain(stream_id, std::move(buf), false);
                    if (result.hasError()) {
                        if (logger) {
                            logger->warn("MvfstTransport: Write failed on stream {}", stream_id);
                        }
                    }
                });
            }

            // Process datagram TX queue
            if (conn_ctx.dgram_tx_data && !conn_ctx.dgram_tx_data->Empty()) {
                TimeQueueElement<ConnData> dgram_elem;
                conn_ctx.dgram_tx_data->PopFront(dgram_elem);
                if (dgram_elem.has_value && dgram_elem.value.data) {
                    auto data = dgram_elem.value.data;
                    folly_evb_.runInEventBaseThread([socket = conn_ctx.quic_socket, data]() {
                        auto buf = folly::IOBuf::copyBuffer(data->data(), data->size());
                        socket->writeDatagram(std::move(buf));
                    });
                }
            }
        }
    }
}

std::shared_ptr<fizz::client::FizzClientContext> MvfstTransport::createFizzClientContext()
{
    auto ctx = std::make_shared<fizz::client::FizzClientContext>();

    // Set supported cipher suites
    ctx->setSupportedCiphers({
        fizz::CipherSuite::TLS_AES_128_GCM_SHA256,
        fizz::CipherSuite::TLS_AES_256_GCM_SHA384,
        fizz::CipherSuite::TLS_CHACHA20_POLY1305_SHA256,
    });

    // Set supported versions
    ctx->setSupportedVersions({fizz::ProtocolVersion::tls_1_3});

    // Set supported signature schemes
    ctx->setSupportedSigSchemes({
        fizz::SignatureScheme::ecdsa_secp256r1_sha256,
        fizz::SignatureScheme::rsa_pss_sha256,
    });

    // Set supported groups
    ctx->setSupportedGroups({
        fizz::NamedGroup::x25519,
        fizz::NamedGroup::secp256r1,
    });

    // Set ALPN
    ctx->setSupportedAlpns({kMoqAlpn});

    return ctx;
}

std::shared_ptr<fizz::server::FizzServerContext> MvfstTransport::createFizzServerContext()
{
    auto ctx = std::make_shared<fizz::server::FizzServerContext>();

    // Validate certificate configuration
    if (tconfig_.tls_cert_filename.empty()) {
        throw InvalidConfigException("Missing TLS certificate filename for server mode");
    }
    if (tconfig_.tls_key_filename.empty()) {
        throw InvalidConfigException("Missing TLS key filename for server mode");
    }

    // Load certificate and key from files
    std::string certData, keyData;
    {
        std::ifstream certFile(tconfig_.tls_cert_filename);
        if (!certFile.is_open()) {
            throw InvalidConfigException("Failed to open certificate file: " + tconfig_.tls_cert_filename);
        }
        certData = std::string((std::istreambuf_iterator<char>(certFile)),
                               std::istreambuf_iterator<char>());
    }
    {
        std::ifstream keyFile(tconfig_.tls_key_filename);
        if (!keyFile.is_open()) {
            throw InvalidConfigException("Failed to open key file: " + tconfig_.tls_key_filename);
        }
        keyData = std::string((std::istreambuf_iterator<char>(keyFile)),
                              std::istreambuf_iterator<char>());
    }

    // Create self cert from the certificate and key data
    auto selfCert = fizz::openssl::CertUtils::makeSelfCert(certData, keyData);

    // Create certificate manager and add the cert
    auto certManager = std::make_unique<fizz::server::CertManager>();
    certManager->addCertAndSetDefault(std::move(selfCert));
    ctx->setCertManager(std::move(certManager));

    // Set supported cipher suites (each inner vector is a group of ciphers with equal preference)
    ctx->setSupportedCiphers({
        {fizz::CipherSuite::TLS_AES_128_GCM_SHA256},
        {fizz::CipherSuite::TLS_AES_256_GCM_SHA384},
        {fizz::CipherSuite::TLS_CHACHA20_POLY1305_SHA256},
    });

    // Set supported versions
    ctx->setSupportedVersions({fizz::ProtocolVersion::tls_1_3});

    // Set supported signature schemes
    ctx->setSupportedSigSchemes({
        fizz::SignatureScheme::ecdsa_secp256r1_sha256,
        fizz::SignatureScheme::ecdsa_secp384r1_sha384,
        fizz::SignatureScheme::rsa_pss_sha256,
        fizz::SignatureScheme::rsa_pss_sha384,
    });

    // Set ALPN - support both MOQ and H3 for WebTransport
    ctx->setSupportedAlpns({kMoqAlpn, "h3"});

    // Don't require client cert by default
    ctx->setClientAuthMode(fizz::server::ClientAuthMode::None);

    // Omit early record layer for QUIC
    ctx->setOmitEarlyRecordLayer(true);

    if (logger) {
        logger->info("MvfstTransport: Server TLS context created with cert: {}", tconfig_.tls_cert_filename);
    }

    return ctx;
}

} // namespace quicr

#endif // USE_MVFST
