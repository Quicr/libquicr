// SPDX-FileCopyrightText: Copyright (c) 2026 Cisco Systems
// SPDX-License-Identifier: BSD-2-Clause

#include "transport_msquic.h"

#include <spdlog/spdlog.h>

#include <algorithm>
#include <arpa/inet.h>
#include <chrono>
#include <future>
#include <limits>
#include <stdexcept>
#include <utility>

namespace quicr {
    namespace {
        constexpr std::string_view kAlpn{ "moqt-18" };
        constexpr std::uint16_t kPeerStreamLimit{ 512 };
        constexpr auto kShutdownTimeout = std::chrono::seconds(5);

        std::uint16_t MsQuicPriority(const std::uint8_t priority)
        {
            return static_cast<std::uint16_t>(0xFFFFU - (static_cast<std::uint16_t>(priority) * 0x0101U));
        }

        std::shared_ptr<MsQuicConnection> AsMsQuicConnection(const std::shared_ptr<Connection>& connection)
        {
            return std::dynamic_pointer_cast<MsQuicConnection>(connection);
        }

        std::shared_ptr<MsQuicDataContext> AsMsQuicDataContext(const std::shared_ptr<DataContext>& data_context)
        {
            return std::dynamic_pointer_cast<MsQuicDataContext>(data_context);
        }
    }

    struct MsQuicTransport::StreamSendContext
    {
        explicit StreamSendContext(std::shared_ptr<const std::vector<std::uint8_t>> bytes,
                                   std::shared_ptr<MsQuicStream> stream,
                                   std::shared_ptr<MsQuicDataContext> data_context,
                                   std::shared_ptr<MsQuicConnection> connection,
                                   const std::uint64_t enqueued_at)
          : bytes(std::move(bytes))
          , stream(std::move(stream))
          , data_context(std::move(data_context))
          , connection(std::move(connection))
          , enqueued_at(enqueued_at)
        {
            buffer.Length = static_cast<std::uint32_t>(this->bytes ? this->bytes->size() : 0);
            buffer.Buffer = buffer.Length == 0 ? nullptr : const_cast<std::uint8_t*>(this->bytes->data());
        }

        QUIC_BUFFER buffer{};
        std::shared_ptr<const std::vector<std::uint8_t>> bytes;
        std::shared_ptr<MsQuicStream> stream;
        std::shared_ptr<MsQuicDataContext> data_context;
        std::shared_ptr<MsQuicConnection> connection;
        std::uint64_t enqueued_at{ 0 };
    };

    struct MsQuicTransport::DatagramSendContext
    {
        DatagramSendContext(std::shared_ptr<const std::vector<std::uint8_t>> bytes,
                            std::shared_ptr<MsQuicDataContext> data_context,
                            std::shared_ptr<MsQuicConnection> connection,
                            const std::uint64_t enqueued_at)
          : bytes(std::move(bytes))
          , data_context(std::move(data_context))
          , connection(std::move(connection))
          , enqueued_at(enqueued_at)
        {
            buffer.Length = static_cast<std::uint32_t>(this->bytes ? this->bytes->size() : 0);
            buffer.Buffer = buffer.Length == 0 ? nullptr : const_cast<std::uint8_t*>(this->bytes->data());
        }

        QUIC_BUFFER buffer{};
        std::shared_ptr<const std::vector<std::uint8_t>> bytes;
        std::shared_ptr<MsQuicDataContext> data_context;
        std::shared_ptr<MsQuicConnection> connection;
        std::uint64_t enqueued_at{ 0 };
    };

    MsQuicTransport::MsQuicTransport(const TransportRemote& remote,
                                     const TransportConfig& config,
                                     const bool server_mode,
                                     std::shared_ptr<timeq::tick_service> tick_service,
                                     std::shared_ptr<spdlog::logger> logger)
      : remote_(remote)
      , config_(config)
      , server_mode_(server_mode)
      , tick_service_(std::move(tick_service))
      , logger_(std::move(logger))
    {
        if (server_mode_ && config_.tls_cert_filename.empty()) {
            throw std::invalid_argument("MsQuic server requires a TLS certificate filename");
        }
        if (server_mode_ && config_.tls_key_filename.empty()) {
            throw std::invalid_argument("MsQuic server requires a TLS private-key filename");
        }

        QUIC_STATUS status = MsQuicOpen2(&api_);
        if (QUIC_FAILED(status)) {
            throw TransportException(TransportError::kFailedToCreateQuicInstance);
        }

        const QUIC_REGISTRATION_CONFIG registration_config{ "libquicr", QUIC_EXECUTION_PROFILE_LOW_LATENCY };
        status = api_->RegistrationOpen(&registration_config, &registration_);
        if (QUIC_FAILED(status)) {
            MsQuicClose(api_);
            api_ = nullptr;
            throw TransportException(TransportError::kFailedToCreateQuicInstance);
        }

        alpn_.Length = static_cast<std::uint32_t>(kAlpn.size());
        alpn_.Buffer = reinterpret_cast<std::uint8_t*>(const_cast<char*>(kAlpn.data()));
        callback_queue_.SetLimit(std::numeric_limits<std::uint32_t>::max());
    }

    MsQuicTransport::~MsQuicTransport()
    {
        Shutdown();
    }

    void MsQuicTransport::OpenConfiguration()
    {
        QUIC_SETTINGS settings{};
        settings.IdleTimeoutMs = config_.idle_timeout_ms;
        settings.IsSet.IdleTimeoutMs = TRUE;
        settings.PeerBidiStreamCount = kPeerStreamLimit;
        settings.IsSet.PeerBidiStreamCount = TRUE;
        settings.PeerUnidiStreamCount = kPeerStreamLimit;
        settings.IsSet.PeerUnidiStreamCount = TRUE;
        settings.DatagramReceiveEnabled = TRUE;
        settings.IsSet.DatagramReceiveEnabled = TRUE;
        if (config_.initial_max_stream_data > 0) {
            const auto receive_window = static_cast<std::uint32_t>(
              std::min<std::uint64_t>(config_.initial_max_stream_data, std::numeric_limits<std::uint32_t>::max()));
            settings.StreamRecvWindowDefault = receive_window;
            settings.IsSet.StreamRecvWindowDefault = TRUE;
            settings.StreamRecvWindowBidiLocalDefault = receive_window;
            settings.IsSet.StreamRecvWindowBidiLocalDefault = TRUE;
            settings.StreamRecvWindowBidiRemoteDefault = receive_window;
            settings.IsSet.StreamRecvWindowBidiRemoteDefault = TRUE;
            settings.StreamRecvWindowUnidiDefault = receive_window;
            settings.IsSet.StreamRecvWindowUnidiDefault = TRUE;
        }

        auto status =
          api_->ConfigurationOpen(registration_, &alpn_, 1, &settings, sizeof(settings), this, &configuration_);
        if (QUIC_FAILED(status)) {
            throw TransportException(TransportError::kFailedToCreateQuicInstance);
        }

        QUIC_CREDENTIAL_CONFIG credential{};
        QUIC_CERTIFICATE_FILE certificate_file{};
        if (server_mode_) {
            certificate_file.PrivateKeyFile = config_.tls_key_filename.c_str();
            certificate_file.CertificateFile = config_.tls_cert_filename.c_str();
            credential.Type = QUIC_CREDENTIAL_TYPE_CERTIFICATE_FILE;
            credential.CertificateFile = &certificate_file;
        } else {
            credential.Type = QUIC_CREDENTIAL_TYPE_NONE;
            credential.Flags = QUIC_CREDENTIAL_FLAG_CLIENT;
            if (config_.tls_client_certificate_validation) {
                credential.Flags |= QUIC_CREDENTIAL_FLAG_USE_TLS_BUILTIN_CERTIFICATE_VALIDATION;
            } else {
                credential.Flags |= QUIC_CREDENTIAL_FLAG_NO_CERTIFICATE_VALIDATION;
            }
        }

        status = api_->ConfigurationLoadCredential(configuration_, &credential);
        if (QUIC_FAILED(status)) {
            api_->ConfigurationClose(configuration_);
            configuration_ = nullptr;
            throw TransportException(TransportError::kFailedToCreateQuicInstance);
        }
    }

    std::shared_ptr<Connection> MsQuicTransport::Start()
    {
        OpenConfiguration();
        callback_thread_ = std::thread(&MsQuicTransport::CallbackLoop, this);
        maintenance_thread_ = std::thread(&MsQuicTransport::MaintenanceLoop, this);

        if (server_mode_) {
            StartServer();
            status_.store(TransportStatus::kReady);
            return nullptr;
        }

        return StartClient();
    }

    std::shared_ptr<Connection> MsQuicTransport::StartClient()
    {
        HQUIC handle = nullptr;
        auto status = api_->ConnectionOpen(registration_, ConnectionCallback, nullptr, &handle);
        if (QUIC_FAILED(status)) {
            throw TransportException(TransportError::kFailedToCreateQuicInstance);
        }

        auto connection = std::make_shared<MsQuicConnection>(api_, handle);
        connection->transport = this;
        api_->SetCallbackHandler(handle, reinterpret_cast<void*>(ConnectionCallback), connection.get());
        {
            std::lock_guard lock(connections_mutex_);
            connections_.emplace(connection->GetID(), connection);
        }

        status_.store(TransportStatus::kConnecting);
        status = api_->ConnectionStart(
          handle, configuration_, QUIC_ADDRESS_FAMILY_UNSPEC, remote_.host_or_ip.c_str(), remote_.port);
        if (QUIC_FAILED(status)) {
            RemoveConnection(connection->GetID());
            if (const auto connection_handle = connection->TakeHandle(); connection_handle != nullptr) {
                api_->ConnectionClose(connection_handle);
            }
            throw TransportException(TransportError::kPeerUnreachable);
        }

        return connection;
    }

    QUIC_ADDR MsQuicTransport::MakeAddress(const TransportRemote& remote)
    {
        QUIC_ADDR address{};
        if (remote.host_or_ip.empty() || remote.host_or_ip == "0.0.0.0" || remote.host_or_ip == "::") {
            QuicAddrSetFamily(&address, QUIC_ADDRESS_FAMILY_UNSPEC);
            QuicAddrSetPort(&address, remote.port);
            return address;
        }

        auto* ipv4 = reinterpret_cast<sockaddr_in*>(&address);
        if (inet_pton(AF_INET, remote.host_or_ip.c_str(), &ipv4->sin_addr) == 1) {
            ipv4->sin_family = AF_INET;
            ipv4->sin_port = htons(remote.port);
            return address;
        }

        auto* ipv6 = reinterpret_cast<sockaddr_in6*>(&address);
        if (inet_pton(AF_INET6, remote.host_or_ip.c_str(), &ipv6->sin6_addr) == 1) {
            ipv6->sin6_family = AF_INET6;
            ipv6->sin6_port = htons(remote.port);
            return address;
        }

        throw std::invalid_argument("MsQuic server bind address must be an IPv4 or IPv6 literal");
    }

    void MsQuicTransport::StartServer()
    {
        auto status = api_->ListenerOpen(registration_, ListenerCallback, this, &listener_);
        if (QUIC_FAILED(status)) {
            throw TransportException(TransportError::kFailedToCreateQuicInstance);
        }

        const auto address = MakeAddress(remote_);
        status = api_->ListenerStart(listener_, &alpn_, 1, &address);
        if (QUIC_FAILED(status)) {
            api_->ListenerClose(listener_);
            listener_ = nullptr;
            throw TransportException(TransportError::kFailedToCreateQuicInstance);
        }
    }

    QUIC_STATUS QUIC_API MsQuicTransport::ListenerCallback(HQUIC, void* context, QUIC_LISTENER_EVENT* event)
    {
        try {
            return static_cast<MsQuicTransport*>(context)->OnListenerEvent(*event);
        } catch (...) {
            return QUIC_STATUS_INTERNAL_ERROR;
        }
    }

    QUIC_STATUS MsQuicTransport::OnListenerEvent(QUIC_LISTENER_EVENT& event)
    {
        if (event.Type != QUIC_LISTENER_EVENT_NEW_CONNECTION || stop_.load()) {
            return QUIC_STATUS_NOT_SUPPORTED;
        }

        const auto handle = event.NEW_CONNECTION.Connection;
        auto connection = std::make_shared<MsQuicConnection>(api_, handle);
        connection->transport = this;
        if (event.NEW_CONNECTION.Info != nullptr && event.NEW_CONNECTION.Info->RemoteAddress != nullptr) {
            connection->peer_address = {};
            std::copy_n(reinterpret_cast<const std::byte*>(event.NEW_CONNECTION.Info->RemoteAddress),
                        sizeof(QUIC_ADDR),
                        reinterpret_cast<std::byte*>(&connection->peer_address));
            connection->peer_address_valid = true;
        }

        {
            std::lock_guard lock(connections_mutex_);
            if (connections_.size() >= config_.max_connections) {
                return QUIC_STATUS_CONNECTION_REFUSED;
            }
            connections_.emplace(connection->GetID(), connection);
        }

        api_->SetCallbackHandler(handle, reinterpret_cast<void*>(ConnectionCallback), connection.get());
        const auto status = api_->ConnectionSetConfiguration(handle, configuration_);
        if (QUIC_FAILED(status)) {
            RemoveConnection(connection->GetID());
        }
        return status;
    }

    QUIC_STATUS QUIC_API MsQuicTransport::ConnectionCallback(HQUIC, void* context, QUIC_CONNECTION_EVENT* event)
    {
        try {
            auto connection = static_cast<MsQuicConnection*>(context)->shared_from_this();
            auto* transport = connection->transport;
            if (transport == nullptr) {
                return QUIC_STATUS_INVALID_STATE;
            }
            return transport->OnConnectionEvent(connection, *event);
        } catch (...) {
            return QUIC_STATUS_INTERNAL_ERROR;
        }
    }

    QUIC_STATUS MsQuicTransport::OnConnectionEvent(const std::shared_ptr<MsQuicConnection>& connection,
                                                   QUIC_CONNECTION_EVENT& event)
    {
        switch (event.Type) {
            case QUIC_CONNECTION_EVENT_CONNECTED: {
                QUIC_ADDR peer_address{};
                std::uint32_t peer_address_size = sizeof(peer_address);
                const auto connection_handle = connection->handle.load();
                if (connection_handle != nullptr &&
                    QUIC_SUCCEEDED(api_->GetParam(
                      connection_handle, QUIC_PARAM_CONN_REMOTE_ADDRESS, &peer_address_size, &peer_address))) {
                    connection->peer_address = {};
                    std::copy_n(reinterpret_cast<const std::byte*>(&peer_address),
                                sizeof(peer_address),
                                reinterpret_cast<std::byte*>(&connection->peer_address));
                    connection->peer_address_valid = true;
                }
                QueueCallback([this, connection]() {
                    connection->SetStatus(Connection::Status::kReady);
                    if (server_mode_ && OnNewConnection) {
                        OnNewConnection(connection);
                    }
                });
                status_.store(TransportStatus::kReady);
                break;
            }
            case QUIC_CONNECTION_EVENT_SHUTDOWN_INITIATED_BY_TRANSPORT:
                QueueCallback([connection]() { connection->SetStatus(Connection::Status::kDisconnected); });
                break;
            case QUIC_CONNECTION_EVENT_SHUTDOWN_INITIATED_BY_PEER:
                QueueCallback([connection]() { connection->SetStatus(Connection::Status::kRemoteRequestClose); });
                break;
            case QUIC_CONNECTION_EVENT_SHUTDOWN_COMPLETE: {
                const auto connection_id = connection->GetID();
                if (const auto connection_handle = connection->TakeHandle(); connection_handle != nullptr) {
                    api_->ConnectionClose(connection_handle);
                }
                RemoveConnection(connection_id);
                QueueCallback([this, connection]() {
                    connection->SetStatus(Connection::Status::kShutdown);
                    if (OnConnectionClosed) {
                        OnConnectionClosed(connection);
                    }
                });
                break;
            }
            case QUIC_CONNECTION_EVENT_PEER_STREAM_STARTED: {
                auto stream = std::make_shared<MsQuicStream>();
                stream->transport = this;
                stream->connection = connection;
                stream->handle = event.PEER_STREAM_STARTED.Stream;
                stream->native_handle = stream->handle;
                stream->is_bidir = (event.PEER_STREAM_STARTED.Flags & QUIC_STREAM_OPEN_FLAG_UNIDIRECTIONAL) == 0;

                std::uint32_t id_size = sizeof(stream->id);
                const auto status = api_->GetParam(stream->handle, QUIC_PARAM_STREAM_ID, &id_size, &stream->id);
                if (QUIC_FAILED(status)) {
                    return status;
                }
                stream->id_ready = true;
                stream->rx_context->data_queue.SetLimit(config_.time_queue_rx_size);

                if (stream->is_bidir) {
                    auto data_context = connection->AddDataContext(true, true);
                    stream->data_context = data_context;
                    std::lock_guard lock(data_context->mutex);
                    data_context->streams.emplace(stream->id, stream);
                }

                connection->AddStream(stream);
                api_->SetCallbackHandler(stream->handle, reinterpret_cast<void*>(StreamCallback), stream.get());
                break;
            }
            case QUIC_CONNECTION_EVENT_DATAGRAM_STATE_CHANGED:
                connection->datagram_send_enabled.store(event.DATAGRAM_STATE_CHANGED.SendEnabled != FALSE);
                connection->max_datagram_send_length.store(event.DATAGRAM_STATE_CHANGED.MaxSendLength);
                break;
            case QUIC_CONNECTION_EVENT_DATAGRAM_RECEIVED: {
                const auto& buffer = *event.DATAGRAM_RECEIVED.Buffer;
                auto bytes =
                  std::make_shared<const std::vector<std::uint8_t>>(buffer.Buffer, buffer.Buffer + buffer.Length);
                connection->datagram_rx_queue->Push(std::move(bytes));
                {
                    std::lock_guard lock(connection->metrics_mutex);
                    connection->quic_metrics.rx_dgrams++;
                    connection->quic_metrics.rx_dgrams_bytes += buffer.Length;
                }
                QueueCallback([connection]() { connection->OnRecvDgram(nullptr); });
                break;
            }
            case QUIC_CONNECTION_EVENT_DATAGRAM_SEND_STATE_CHANGED: {
                if (!QUIC_DATAGRAM_SEND_STATE_IS_FINAL(event.DATAGRAM_SEND_STATE_CHANGED.State)) {
                    break;
                }
                std::unique_ptr<DatagramSendContext> send(
                  static_cast<DatagramSendContext*>(event.DATAGRAM_SEND_STATE_CHANGED.ClientContext));
                if (!send) {
                    break;
                }
                {
                    std::lock_guard lock(send->connection->metrics_mutex);
                    switch (event.DATAGRAM_SEND_STATE_CHANGED.State) {
                        case QUIC_DATAGRAM_SEND_ACKNOWLEDGED:
                            send->connection->quic_metrics.tx_dgram_ack++;
                            break;
                        case QUIC_DATAGRAM_SEND_ACKNOWLEDGED_SPURIOUS:
                            send->connection->quic_metrics.tx_dgram_spurious++;
                            break;
                        case QUIC_DATAGRAM_SEND_LOST_DISCARDED:
                            send->connection->quic_metrics.tx_dgram_lost++;
                            break;
                        default:
                            break;
                    }
                }
                if (send->data_context->outstanding_sends.fetch_sub(1) == 1) {
                    MaybeDeleteDataContext(send->connection, send->data_context);
                }
                break;
            }
            default:
                break;
        }
        return QUIC_STATUS_SUCCESS;
    }

    QUIC_STATUS QUIC_API MsQuicTransport::StreamCallback(HQUIC, void* context, QUIC_STREAM_EVENT* event)
    {
        try {
            auto stream = static_cast<MsQuicStream*>(context)->shared_from_this();
            return stream->transport->OnStreamEvent(stream, *event);
        } catch (...) {
            return QUIC_STATUS_INTERNAL_ERROR;
        }
    }

    QUIC_STATUS MsQuicTransport::OnStreamEvent(const std::shared_ptr<MsQuicStream>& stream, QUIC_STREAM_EVENT& event)
    {
        switch (event.Type) {
            case QUIC_STREAM_EVENT_START_COMPLETE: {
                HQUIC failed_stream_handle = nullptr;
                {
                    std::lock_guard lock(stream->mutex);
                    stream->start_status = event.START_COMPLETE.Status;
                    stream->start_complete = true;
                    if (QUIC_SUCCEEDED(event.START_COMPLETE.Status)) {
                        stream->id = event.START_COMPLETE.ID;
                        stream->id_ready = true;
                    } else if (stream->start_abandoned) {
                        failed_stream_handle = std::exchange(stream->handle, nullptr);
                    }
                    stream->start_cv.notify_all();
                }
                if (failed_stream_handle != nullptr) {
                    api_->StreamClose(failed_stream_handle);
                    if (const auto connection = stream->connection.lock()) {
                        connection->ReleasePendingStream(stream->native_handle);
                    }
                }
                break;
            }
            case QUIC_STREAM_EVENT_RECEIVE:
                OnStreamReceive(stream, event);
                break;
            case QUIC_STREAM_EVENT_SEND_COMPLETE: {
                std::unique_ptr<StreamSendContext> send(
                  static_cast<StreamSendContext*>(event.SEND_COMPLETE.ClientContext));
                if (!send) {
                    break;
                }
                {
                    std::lock_guard lock(send->data_context->metrics_mutex);
                    send->data_context->metrics.tx_stream_cb++;
                    send->data_context->metrics.tx_object_duration_us.AddValue(
                      static_cast<std::uint64_t>(tick_service_->get().count()) - send->enqueued_at);
                }
                if (send->data_context->outstanding_sends.fetch_sub(1) == 1) {
                    MaybeDeleteDataContext(send->connection, send->data_context);
                }
                break;
            }
            case QUIC_STREAM_EVENT_PEER_SEND_SHUTDOWN:
                NotifyStreamClosed(stream, StreamClosedFlag::kFin);
                break;
            case QUIC_STREAM_EVENT_PEER_SEND_ABORTED:
                NotifyStreamClosed(stream, StreamClosedFlag::kReset);
                break;
            case QUIC_STREAM_EVENT_SHUTDOWN_COMPLETE: {
                auto connection = stream->connection.lock();
                auto data_context = stream->data_context.lock();
                HQUIC stream_handle = nullptr;
                {
                    std::lock_guard lock(stream->mutex);
                    stream_handle = std::exchange(stream->handle, nullptr);
                }
                if (stream_handle != nullptr) {
                    api_->StreamClose(stream_handle);
                }
                if (connection) {
                    connection->ReleasePendingStream(stream->native_handle);
                }
                QueueCallback([connection, data_context, stream_id = stream->id]() {
                    if (connection) {
                        connection->RemoveStream(stream_id);
                    }
                    if (data_context) {
                        std::lock_guard lock(data_context->mutex);
                        data_context->streams.erase(stream_id);
                    }
                });
                break;
            }
            default:
                break;
        }
        return QUIC_STATUS_SUCCESS;
    }

    void MsQuicTransport::OnStreamReceive(const std::shared_ptr<MsQuicStream>& stream, const QUIC_STREAM_EVENT& event)
    {
        const auto total_length = static_cast<std::size_t>(event.RECEIVE.TotalBufferLength);
        if (total_length > 0) {
            auto bytes = std::make_shared<std::vector<std::uint8_t>>();
            bytes->reserve(total_length);
            for (std::uint32_t i = 0; i < event.RECEIVE.BufferCount; ++i) {
                const auto& buffer = event.RECEIVE.Buffers[i];
                bytes->insert(bytes->end(), buffer.Buffer, buffer.Buffer + buffer.Length);
            }
            stream->rx_context->data_queue.Push(bytes);

            auto data_context = stream->data_context.lock();
            if (data_context) {
                std::lock_guard lock(data_context->metrics_mutex);
                data_context->metrics.rx_stream_cb++;
                data_context->metrics.rx_stream_bytes += bytes->size();
            }
            if (auto connection = stream->connection.lock()) {
                QueueCallback([connection, stream, data_context]() {
                    connection->OnRecvStream(stream->id, data_context, stream->is_bidir);
                });
            }
        }

        if ((event.RECEIVE.Flags & QUIC_RECEIVE_FLAG_FIN) != 0) {
            NotifyStreamClosed(stream, StreamClosedFlag::kFin);
        }
    }

    void MsQuicTransport::NotifyStreamClosed(const std::shared_ptr<MsQuicStream>& stream, const StreamClosedFlag flag)
    {
        {
            std::lock_guard lock(stream->mutex);
            if (stream->close_notified) {
                return;
            }
            stream->close_notified = true;
        }

        auto connection = stream->connection.lock();
        if (!connection) {
            return;
        }
        auto data_context = stream->data_context.lock();
        QueueCallback([connection, stream, data_context, flag]() {
            connection->OnStreamClosed(stream->id, stream->rx_context, nullptr, flag);
            if (data_context) {
                connection->OnStreamClosed(stream->id, nullptr, data_context, flag);
            }
        });
    }

    std::shared_ptr<DataContext> MsQuicTransport::CreateDataContext(const std::shared_ptr<Connection>& connection,
                                                                    const bool use_reliable_transport,
                                                                    [[maybe_unused]] const std::uint8_t priority,
                                                                    const bool bidir)
    {
        auto msquic_connection = AsMsQuicConnection(connection);
        if (!msquic_connection) {
            throw TransportException(TransportError::kInvalidConnContextId);
        }
        return msquic_connection->AddDataContext(bidir, use_reliable_transport);
    }

    std::uint64_t MsQuicTransport::CreateStream(const std::shared_ptr<Connection>& connection,
                                                const std::shared_ptr<DataContext>& data_context,
                                                const std::uint8_t priority)
    {
        auto msquic_connection = AsMsQuicConnection(connection);
        auto msquic_data_context = AsMsQuicDataContext(data_context);
        if (!msquic_connection || !msquic_data_context) {
            throw TransportException(TransportError::kInvalidDataContextId);
        }

        auto stream = std::make_shared<MsQuicStream>();
        stream->transport = this;
        stream->connection = msquic_connection;
        stream->data_context = msquic_data_context;
        stream->is_bidir = msquic_data_context->IsBidir();
        stream->locally_created = true;
        stream->rx_context->data_queue.SetLimit(config_.time_queue_rx_size);

        const auto open_flags = stream->is_bidir ? QUIC_STREAM_OPEN_FLAG_NONE : QUIC_STREAM_OPEN_FLAG_UNIDIRECTIONAL;
        const auto connection_handle = msquic_connection->handle.load();
        if (connection_handle == nullptr) {
            throw TransportException(TransportError::kInvalidConnContextId);
        }
        auto status = api_->StreamOpen(connection_handle, open_flags, StreamCallback, stream.get(), &stream->handle);
        if (QUIC_FAILED(status)) {
            throw TransportException(TransportError::kInvalidStreamId);
        }
        stream->native_handle = stream->handle;
        msquic_connection->RetainPendingStream(stream);

        const auto msquic_priority = MsQuicPriority(priority);
        status = api_->SetParam(stream->handle, QUIC_PARAM_STREAM_PRIORITY, sizeof(msquic_priority), &msquic_priority);
        if (QUIC_FAILED(status)) {
            api_->StreamClose(stream->handle);
            stream->handle = nullptr;
            msquic_connection->ReleasePendingStream(stream->native_handle);
            throw TransportException(TransportError::kInvalidStreamId);
        }

        api_->StreamStart(stream->handle, QUIC_STREAM_START_FLAG_IMMEDIATE | QUIC_STREAM_START_FLAG_SHUTDOWN_ON_FAIL);

        bool start_timed_out = false;
        QUIC_STATUS start_status = QUIC_STATUS_PENDING;
        {
            std::unique_lock lock(stream->mutex);
            start_timed_out = !stream->start_cv.wait_for(
              lock, std::chrono::milliseconds(500), [&stream]() { return stream->start_complete; });
            if (start_timed_out) {
                stream->start_abandoned = true;
            } else {
                start_status = stream->start_status;
            }
        }
        if (start_timed_out) {
            HQUIC stream_handle = nullptr;
            {
                std::lock_guard lock(stream->mutex);
                stream_handle = stream->handle;
            }
            if (stream_handle != nullptr) {
                api_->StreamShutdown(
                  stream_handle, QUIC_STREAM_SHUTDOWN_FLAG_ABORT | QUIC_STREAM_SHUTDOWN_FLAG_IMMEDIATE, 0);
            }
            throw TransportException(TransportError::kInvalidStreamId);
        }
        if (QUIC_FAILED(start_status)) {
            HQUIC stream_handle = nullptr;
            {
                std::lock_guard lock(stream->mutex);
                stream_handle = std::exchange(stream->handle, nullptr);
            }
            if (stream_handle != nullptr) {
                api_->StreamClose(stream_handle);
            }
            msquic_connection->ReleasePendingStream(stream->native_handle);
            throw TransportException(TransportError::kInvalidStreamId);
        }

        if (!msquic_connection->PromotePendingStream(stream)) {
            throw TransportException(TransportError::kInvalidStreamId);
        }
        {
            std::lock_guard lock(msquic_data_context->mutex);
            msquic_data_context->streams.emplace(stream->id, stream);
        }
        return stream->id;
    }

    TransportError MsQuicTransport::Enqueue(const std::shared_ptr<Connection>& connection,
                                            const std::shared_ptr<DataContext>& data_context,
                                            std::uint64_t stream_id,
                                            std::shared_ptr<const std::vector<std::uint8_t>> bytes,
                                            const std::uint8_t priority,
                                            [[maybe_unused]] const std::uint32_t ttl_ms,
                                            [[maybe_unused]] const std::uint32_t delay_ms,
                                            const EnqueueFlags flags)
    {
        auto msquic_connection = AsMsQuicConnection(connection);
        auto msquic_data_context = AsMsQuicDataContext(data_context);
        if (!msquic_connection) {
            return TransportError::kInvalidConnContextId;
        }
        if (!msquic_data_context) {
            return TransportError::kInvalidDataContextId;
        }
        if (!bytes) {
            bytes = std::make_shared<const std::vector<std::uint8_t>>();
        }

        const auto enqueued_at = static_cast<std::uint64_t>(tick_service_->get().count());
        {
            std::lock_guard lock(msquic_data_context->metrics_mutex);
            msquic_data_context->metrics.enqueued_objs++;
        }

        if (!flags.use_reliable) {
            if (!msquic_connection->datagram_send_enabled.load() ||
                bytes->size() > msquic_connection->max_datagram_send_length.load()) {
                return TransportError::kQueueFull;
            }
            auto send = std::make_unique<DatagramSendContext>(
              std::move(bytes), msquic_data_context, msquic_connection, enqueued_at);
            auto* send_context = send.get();
            msquic_data_context->outstanding_sends.fetch_add(1);
            const auto send_flags = priority <= 1 ? QUIC_SEND_FLAG_DGRAM_PRIORITY : QUIC_SEND_FLAG_NONE;
            const auto status =
              api_->DatagramSend(msquic_connection->handle.load(), &send_context->buffer, 1, send_flags, send_context);
            if (QUIC_FAILED(status)) {
                msquic_data_context->outstanding_sends.fetch_sub(1);
                return TransportError::kQueueFull;
            }
            {
                std::lock_guard lock(msquic_data_context->metrics_mutex);
                msquic_data_context->metrics.tx_dgrams++;
                msquic_data_context->metrics.tx_dgrams_bytes += send_context->buffer.Length;
            }
            send.release();
            return TransportError::kNone;
        }

        if (data_context->IsBidir() || (stream_id == 0 && priority == 0)) {
            std::lock_guard lock(msquic_data_context->mutex);
            if (msquic_data_context->streams.empty()) {
                return TransportError::kInvalidStreamId;
            }
            stream_id = msquic_data_context->streams.begin()->first;
        }

        auto stream = msquic_connection->GetStream(stream_id);
        if (!stream || stream->handle == nullptr) {
            return TransportError::kInvalidStreamId;
        }
        if (flags.close_stream && flags.use_reset) {
            CloseStream(connection, data_context, stream_id, true);
            return TransportError::kNone;
        }

        auto send = std::make_unique<StreamSendContext>(
          std::move(bytes), stream, msquic_data_context, msquic_connection, enqueued_at);
        auto* send_context = send.get();
        msquic_data_context->outstanding_sends.fetch_add(1);
        const auto send_flags = flags.close_stream ? QUIC_SEND_FLAG_FIN : QUIC_SEND_FLAG_NONE;
        const auto status = api_->StreamSend(stream->handle, &send_context->buffer, 1, send_flags, send_context);
        if (QUIC_FAILED(status)) {
            msquic_data_context->outstanding_sends.fetch_sub(1);
            return TransportError::kQueueFull;
        }
        {
            std::lock_guard lock(msquic_data_context->metrics_mutex);
            msquic_data_context->metrics.tx_stream_objects++;
            msquic_data_context->metrics.tx_stream_bytes += send_context->buffer.Length;
            msquic_data_context->metrics.tx_queue_size.AddValue(msquic_data_context->outstanding_sends.load());
        }
        send.release();
        return TransportError::kNone;
    }

    std::shared_ptr<const std::vector<std::uint8_t>> MsQuicTransport::Dequeue(
      const std::shared_ptr<Connection>& connection,
      [[maybe_unused]] const std::shared_ptr<DataContext>& data_context)
    {
        auto msquic_connection = AsMsQuicConnection(connection);
        if (!msquic_connection) {
            return nullptr;
        }
        const auto value = msquic_connection->datagram_rx_queue->Pop();
        return value.has_value() ? *value : nullptr;
    }

    std::shared_ptr<StreamRxContext> MsQuicTransport::GetStreamRxContext(const std::shared_ptr<Connection>& connection,
                                                                         const std::uint64_t stream_id)
    {
        auto msquic_connection = AsMsQuicConnection(connection);
        if (!msquic_connection) {
            throw TransportException(TransportError::kInvalidConnContextId);
        }
        auto stream = msquic_connection->GetStream(stream_id);
        if (!stream) {
            throw TransportException(TransportError::kInvalidStreamId);
        }
        return stream->rx_context;
    }

    void MsQuicTransport::CloseStream(const std::shared_ptr<Connection>& connection,
                                      [[maybe_unused]] const std::shared_ptr<DataContext>& data_context,
                                      const std::uint64_t stream_id,
                                      const bool use_reset)
    {
        auto msquic_connection = AsMsQuicConnection(connection);
        auto stream = msquic_connection ? msquic_connection->GetStream(stream_id) : nullptr;
        if (!stream || stream->handle == nullptr) {
            return;
        }
        const auto flags = use_reset ? static_cast<QUIC_STREAM_SHUTDOWN_FLAGS>(QUIC_STREAM_SHUTDOWN_FLAG_ABORT |
                                                                               QUIC_STREAM_SHUTDOWN_FLAG_IMMEDIATE)
                                     : QUIC_STREAM_SHUTDOWN_FLAG_GRACEFUL;
        api_->StreamShutdown(stream->handle, flags, 0);
    }

    void MsQuicTransport::DeleteDataContext(const std::shared_ptr<Connection>& connection,
                                            const std::shared_ptr<DataContext>& data_context,
                                            const bool delete_on_empty)
    {
        auto msquic_connection = AsMsQuicConnection(connection);
        auto msquic_data_context = AsMsQuicDataContext(data_context);
        if (!msquic_connection || !msquic_data_context) {
            return;
        }
        if (delete_on_empty) {
            msquic_data_context->delete_on_empty.store(true);
            if (msquic_data_context->outstanding_sends.load() != 0) {
                return;
            }
        }
        if (msquic_data_context->delete_started.exchange(true)) {
            return;
        }

        for (const auto& stream : [&]() {
                 std::lock_guard lock(msquic_data_context->mutex);
                 std::vector<std::shared_ptr<MsQuicStream>> streams;
                 streams.reserve(msquic_data_context->streams.size());
                 for (const auto& [_, value] : msquic_data_context->streams) {
                     streams.push_back(value);
                 }
                 return streams;
             }()) {
            if (stream->handle != nullptr) {
                api_->StreamShutdown(stream->handle, QUIC_STREAM_SHUTDOWN_FLAG_GRACEFUL, 0);
            }
        }
        msquic_connection->RemoveDataContext(msquic_data_context->GetID());
    }

    void MsQuicTransport::MaybeDeleteDataContext(const std::shared_ptr<MsQuicConnection>& connection,
                                                 const std::shared_ptr<MsQuicDataContext>& data_context)
    {
        if (data_context->delete_on_empty.load() && data_context->outstanding_sends.load() == 0) {
            DeleteDataContext(connection, data_context, false);
        }
    }

    std::uint64_t MsQuicTransport::AppErrorCode(const AppReasonForClose reason)
    {
        return static_cast<std::uint64_t>(reason);
    }

    void MsQuicTransport::Close(const std::shared_ptr<Connection>& connection, const AppReasonForClose app_reason)
    {
        auto msquic_connection = AsMsQuicConnection(connection);
        if (!msquic_connection || msquic_connection->handle.load() == nullptr) {
            return;
        }
        QueueCallback([msquic_connection]() { msquic_connection->SetStatus(Connection::Status::kShuttingDown); });
        api_->ConnectionShutdown(
          msquic_connection->handle.load(), QUIC_CONNECTION_SHUTDOWN_FLAG_NONE, AppErrorCode(app_reason));
    }

    bool MsQuicTransport::GetPeerAddrInfo(const std::shared_ptr<Connection>& connection, sockaddr_storage* address)
    {
        auto msquic_connection = AsMsQuicConnection(connection);
        if (!msquic_connection || address == nullptr || !msquic_connection->peer_address_valid) {
            return false;
        }
        *address = msquic_connection->peer_address;
        return true;
    }

    void MsQuicTransport::RemoveConnection(const std::uint64_t connection_id)
    {
        {
            std::lock_guard lock(connections_mutex_);
            connections_.erase(connection_id);
        }
        connections_cv_.notify_all();
    }

    void MsQuicTransport::QueueCallback(std::function<void()> callback)
    {
        if (!callback_queue_.Push(std::move(callback))) {
            SPDLOG_LOGGER_ERROR(logger_, "MsQuic callback queue overflowed");
        }
    }

    void MsQuicTransport::CallbackLoop()
    {
        while (auto callback = callback_queue_.BlockPop()) {
            try {
                (*callback)();
            } catch (const std::exception& exception) {
                SPDLOG_LOGGER_ERROR(logger_, "MsQuic application callback failed: {}", exception.what());
            } catch (...) {
                SPDLOG_LOGGER_ERROR(logger_, "MsQuic application callback failed with an unknown exception");
            }
        }
    }

    void MsQuicTransport::MaintenanceLoop()
    {
        auto next_sample = std::chrono::steady_clock::now() + std::chrono::milliseconds(config_.metrics_sample_ms);
        while (!stop_.load()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            if (config_.metrics_sample_ms == 0 || std::chrono::steady_clock::now() < next_sample) {
                continue;
            }

            std::vector<std::shared_ptr<MsQuicConnection>> connections;
            {
                std::lock_guard lock(connections_mutex_);
                connections.reserve(connections_.size());
                for (const auto& [_, connection] : connections_) {
                    connections.push_back(connection);
                }
            }
            const auto sample_time = std::chrono::system_clock::now();
            for (const auto& connection : connections) {
                QueueCallback([connection, sample_time]() { connection->SampleMetrics(sample_time); });
            }
            next_sample = std::chrono::steady_clock::now() + std::chrono::milliseconds(config_.metrics_sample_ms);
        }
    }

    void MsQuicTransport::FlushCallbacks()
    {
        if (!callback_thread_.joinable()) {
            return;
        }
        auto done = std::make_shared<std::promise<void>>();
        auto future = done->get_future();
        QueueCallback([done]() { done->set_value(); });
        future.wait_for(kShutdownTimeout);
    }

    void MsQuicTransport::Shutdown()
    {
        if (stop_.exchange(true)) {
            return;
        }
        status_.store(TransportStatus::kShuttingDown);

        if (listener_ != nullptr) {
            api_->ListenerClose(listener_);
            listener_ = nullptr;
        }

        std::vector<std::shared_ptr<MsQuicConnection>> connections;
        {
            std::lock_guard lock(connections_mutex_);
            connections.reserve(connections_.size());
            for (const auto& [_, connection] : connections_) {
                connections.push_back(connection);
            }
        }
        for (const auto& connection : connections) {
            if (const auto connection_handle = connection->handle.load(); connection_handle != nullptr) {
                api_->ConnectionShutdown(
                  connection_handle, QUIC_CONNECTION_SHUTDOWN_FLAG_NONE, AppErrorCode(AppReasonForClose::kShutdown));
            }
        }

        std::vector<std::shared_ptr<MsQuicConnection>> remaining_connections;
        {
            std::unique_lock lock(connections_mutex_);
            connections_cv_.wait_for(lock, kShutdownTimeout, [this]() { return connections_.empty(); });
            remaining_connections.reserve(connections_.size());
            for (const auto& [_, connection] : connections_) {
                remaining_connections.push_back(connection);
            }
            connections_.clear();
        }
        for (const auto& connection : remaining_connections) {
            if (const auto connection_handle = connection->TakeHandle(); connection_handle != nullptr) {
                api_->ConnectionClose(connection_handle);
            }
        }

        if (maintenance_thread_.joinable()) {
            maintenance_thread_.join();
        }
        FlushCallbacks();
        callback_queue_.StopWaiting();
        if (callback_thread_.joinable()) {
            callback_thread_.join();
        }

        if (configuration_ != nullptr) {
            api_->ConfigurationClose(configuration_);
            configuration_ = nullptr;
        }
        if (registration_ != nullptr) {
            api_->RegistrationClose(registration_);
            registration_ = nullptr;
        }
        if (api_ != nullptr) {
            MsQuicClose(api_);
            api_ = nullptr;
        }
        tick_service_.reset();
        status_.store(TransportStatus::kShutdown);
    }
}
