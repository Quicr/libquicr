// SPDX-FileCopyrightText: Copyright (c) 2024 Cisco Systems
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

#include "quicr/config.h"
#include "quicr/containers/stream_buffer.h"
#include "quicr/metrics.h"

#include <timeq/tick_service.h>

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <queue>
#include <source_location>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <sys/socket.h>
#endif

namespace quicr {

    class Connection;
    class Logger;
    class Stream;

    /// @brief Application reason sent when closing a connection.
    enum class AppReasonForClose : uint8_t
    {
        kShutdown = 1,       ///< Local application shutdown.
        kIdleTimeout,        ///< Connection exceeded its idle timeout.
        kRemoteRequestClose, ///< Closure requested by the remote endpoint.
        kInternalError,      ///< Unspecified local internal failure.
        kProtocolViolation,  ///< Peer traffic violated the protocol.
        kNotAuthorized,      ///< Peer is not authorized to continue.
        kUnknown,            ///< No more specific reason is available.
    };

    /// @brief Transport lifecycle state.
    enum class TransportStatus : uint8_t
    {
        kReady = 0,          ///< Ready to carry application traffic.
        kConnecting,         ///< Connection establishment is in progress.
        kRemoteRequestClose, ///< The peer requested closure.
        kDisconnected,       ///< No longer connected.
        kIdleTimeout,        ///< Closed because the idle timeout elapsed.
        kShutdown,           ///< Shutdown is complete.
        kShuttingDown,       ///< Shutdown is in progress.
    };

    /// @brief Result codes returned by transport operations.
    enum class TransportError : uint8_t
    {
        kNone = 0,                   ///< Operation succeeded.
        kQueueFull,                  ///< The relevant transmit queue has reached its limit.
        kUnknownError,               ///< An unclassified transport failure occurred.
        kPeerDisconnected,           ///< The peer is no longer connected.
        kPeerUnreachable,            ///< The peer cannot be reached.
        kCannotResolveHostname,      ///< DNS or address resolution failed.
        kInvalidConnContextId,       ///< The connection handle is invalid or no longer active.
        kInvalidIpv4Address,         ///< An IPv4 address is malformed.
        kInvalidIpv6Address,         ///< An IPv6 address is malformed.
        kInvalidStreamId,            ///< The stream handle or identifier is invalid.
        kFailedToCreateQuicInstance, ///< The underlying QUIC instance could not be created.
    };

    /// @brief Wire transport selected for a client connection.
    enum class TransportProtocol : uint8_t
    {
        kQuic,        ///< Native MoQ over QUIC.
        kWebTransport ///< MoQ over WebTransport and HTTP/3.
    };

    /**
     * @brief Remote/Destination endpoint address info.
     *
     * @details Remote destination is either a client or server hostname/ip and port
     */
    struct TransportRemote
    {
        std::string host_or_ip;      ///< IPv4/IPv6 address or fully qualified host name.
        uint16_t port;               ///< UDP destination or listening port.
        TransportProtocol proto;     ///< Client protocol; ignored when creating a server transport.
        std::string path{ "relay" }; ///< WebTransport HTTP path.
    };

    /// Stream action that should be done by send/receive processing
    enum class StreamAction : uint8_t
    {
        kNoAction = 0,        ///< Keep the current stream open.
        kCloseStreamUseReset, ///< Close the current stream abruptly with RESET_STREAM.
        kCloseStreamUseFin,   ///< Close the current stream normally with FIN.
    };

    /// @brief Queued connection payload and stream action used by transport internals.
    struct ConnData
    {
        std::uint64_t conn_id; ///< Transport-defined connection identifier.

        /// @brief Payload priority; lower values are higher priority.
        uint8_t priority;

        StreamAction stream_action{
            StreamAction::kNoAction
        }; ///< Stream action to apply while processing this payload.

        /// @brief Immutable payload whose ownership can be transferred without copying.
        std::shared_ptr<const std::vector<uint8_t>> data;

        uint64_t tick_microseconds; ///< Queue timestamp in tick-service microseconds.
    };

    /// @brief Exception carrying a failed transport operation's result code and call site.
    struct TransportException : std::runtime_error
    {
        /**
         * @brief Construct an exception for a transport failure.
         * @param error Transport error that caused the failure.
         * @param location Source location at which the exception was created.
         */
        TransportException(TransportError error, std::source_location location = std::source_location::current());

        TransportError Error; ///< Machine-readable transport failure.
    };

    /// @brief Operation used to close one or both directions of a QUIC stream.
    enum class StreamOperation : uint8_t
    {
        kFin,         ///< Finish the locally writable direction normally.
        kReset,       ///< Reset the locally writable direction.
        kStopSending, ///< Request that the peer stop its writable direction.
        kCancel,      ///< Reset and stop both directions of a bidirectional stream.
    };

    /**
     * @brief Validate that a close operation is legal for a QUIC stream's direction.
     * @param stream_id QUIC stream identifier encoding initiator and direction.
     * @param is_server Whether the local endpoint is the server.
     * @param operation Requested close operation.
     * @throws std::invalid_argument if the local endpoint cannot perform @p operation on the stream direction.
     */
    constexpr void CheckCloseStream(std::uint64_t stream_id, bool is_server, StreamOperation operation)
    {
        const bool is_bidir = (stream_id & 0x2) == 0;
        const bool is_locally_initiated = ((stream_id & 0x1) != 0) == is_server;
        const bool can_send = is_bidir || is_locally_initiated;
        const bool can_receive = is_bidir || !is_locally_initiated;

        switch (operation) {
            case StreamOperation::kFin:
                [[fallthrough]];
            case StreamOperation::kReset:
                if (can_send) {
                    return;
                }
                break;
            case StreamOperation::kStopSending:
                if (can_receive) {
                    return;
                }
                break;
            case StreamOperation::kCancel:
                if (is_bidir) {
                    return;
                }
                break;
        }

        throw std::invalid_argument("Stream close is invalid for the stream direction");
    }

    /**
     * @brief Transport interface
     *
     * @details Implementations own asynchronous network and notification queues. Public operations marshal work
     *      to the transport as needed; callbacks are delivered serially on the transport notification thread.
     *      Callback implementations must return promptly to avoid delaying unrelated connection and stream events.
     */
    class Transport
    {
      public:
        /* Factory APIs */

        /**
         * @brief Create a new client transport based on the remote (server) host/ip
         *
         * @param server Transport remote server information
         * @param tcfg Transport configuration
         * @param tick_service Shared pointer to the tick service to use
         * @param logger Shared pointer to logger
         *
         * @return Shared ownership of the created transport.
         * @throws std::runtime_error if @p server selects an unsupported protocol.
         * @throws std::runtime_error if the QUIC/TLS configuration cannot be initialized.
         */
        static std::shared_ptr<Transport> MakeClientTransport(const TransportRemote& server,
                                                              const TransportConfig& tcfg,
                                                              std::shared_ptr<timeq::tick_service> tick_service,
                                                              std::shared_ptr<Logger> logger);

        /**
         * @brief Create a new server transport based on the remote (server) ip and port
         *
         * @details Server mode automatically supports BOTH raw QUIC (ALPN: moq-00) and
         * WebTransport (ALPN: h3) simultaneously. The transport mode for each connection
         * is determined dynamically based on the ALPN negotiated with each client during
         * the TLS handshake.
         *
         * @param server Transport remote server information (server.proto is ignored)
         * @param tcfg Transport configuration
         * @param tick_service Shared pointer to tick service
         * @param logger Shared pointer to logger
         *
         * @return Shared ownership of the created transport.
         * @throws std::runtime_error if the QUIC/TLS configuration cannot be initialized.
         */
        static std::shared_ptr<Transport> MakeServerTransport(const TransportRemote& server,
                                                              const TransportConfig& tcfg,
                                                              std::shared_ptr<timeq::tick_service> tick_service,
                                                              std::shared_ptr<Logger> logger);

      public:
        /// @brief Destroy the transport after derived-class shutdown has completed.
        virtual ~Transport() = default;

        /**
         * @brief Status of the transport
         *
         * @details Return the status of the transport. In server mode, the transport
         * will reflect the status of the listening socket. In client mode it will
         * reflect the status of the server connection.
         *
         * @return Current transport lifecycle state.
         */
        virtual TransportStatus Status() const = 0;

        /**
         * @brief Setup the transport connection
         *
         * @details In server mode this will create the listening socket and will
         * 		start listening on the socket for new connections. In client
         * mode this will initiate a connection to the remote/server.
         *
         * @return Client connection, or the server listener's connection object as defined by the implementation.
         */
        virtual std::shared_ptr<Connection> Start() = 0;

        /**
         * @brief Close a transport context
         *
         * @param connection Connection to close.
         * @param app_reason Application reason to close the connection
         * @pre @p connection must refer to a connection created by this transport.
         */
        virtual void Close(const std::shared_ptr<Connection>& connection,
                           AppReasonForClose app_reason = AppReasonForClose::kRemoteRequestClose) = 0;

        /**
         * @brief Close a stream
         *
         * @param connection Connection the stream belongs to
         * @param stream Stream to close; no-op if null or already closed
         * @param operation Directional stream-close operation.
         * @pre A non-null @p stream must belong to @p connection.
         * @throws std::invalid_argument if @p operation is invalid for the stream direction.
         */
        virtual void CloseStream(const std::shared_ptr<Connection>& connection,
                                 const std::shared_ptr<Stream>& stream,
                                 StreamOperation operation) = 0;

        /**
         * @brief Get the peer IP address and port associated with the stream
         *
         * @param connection Connection whose peer address is requested.
         * @param addr Destination for the peer socket address; must not be null.
         *
         * @returns True if the address was successfully returned, false otherwise
         */
        virtual bool GetPeerAddrInfo(const std::shared_ptr<Connection>& connection, sockaddr_storage* addr) = 0;

        /// @brief Options controlling reliability, queue state, and stream rollover during enqueue.
        struct EnqueueFlags
        {
            bool use_reliable{ false };   ///< Send on a reliable stream rather than as a datagram.
            bool close_stream{ false };   ///< Roll over to a replacement stream after this object.
            bool clear_tx_queue{ false }; ///< Clear queued transmit objects before adding this object.
            bool use_reset{ false };      ///< Reset, rather than finish, the stream being replaced.
        };

        /**
         * @brief Enqueue application data on a stream
         *
         * @details Add data to the stream's transmit queue, to be sent when the stream is next able to.
         *
         * @param connection Identifying the connection
         * @param stream Stream to send on
         * @param bytes Data to send/write
         * @param priority Priority of the object, range should be 0 - 255
         * @param ttl_ms The age the object should exist in queue in milliseconds
         * @param flags Flags for stream and queue handling on enqueue of object
         *
         * @returns TransportError is returned indicating status of the operation
         */
        virtual TransportError Enqueue(const std::shared_ptr<Connection>& connection,
                                       const std::shared_ptr<Stream>& stream,
                                       std::shared_ptr<const std::vector<uint8_t>> bytes,
                                       uint8_t priority = 1,
                                       uint32_t ttl_ms = 350,
                                       EnqueueFlags flags = { true, false, false, false }) = 0;

        /**
         * @brief Enqueue application data as a datagram
         *
         * @details The datagram queue belongs to the connection, so datagrams from every track share it
         *      and are ordered against each other by priority alone.
         *
         * @param connection Identifying the connection
         * @param bytes Data to send/write
         * @param priority Priority of the object, range should be 0 - 255
         * @param ttl_ms The age the object should exist in queue in milliseconds
         *
         * @returns TransportError is returned indicating status of the operation
         */
        virtual TransportError EnqueueDatagram(const std::shared_ptr<Connection>& connection,
                                               std::shared_ptr<const std::vector<uint8_t>> bytes,
                                               uint8_t priority = 1,
                                               uint32_t ttl_ms = 350) = 0;

        /**
         * @brief Dequeue datagram application data from transport buffer
         *
         * @details Removes the next received datagram from the connection's receive queue.
         *
         * @param connection Identifying the connection
         *
         * @returns Shared immutable datagram bytes, or null if no datagram is queued.
         */
        virtual std::shared_ptr<const std::vector<uint8_t>> Dequeue(const std::shared_ptr<Connection>& connection) = 0;

        /**
         * @brief Close a WebTransport session with error code and message
         *
         * @details Sends a CLOSE_WEBTRANSPORT_SESSION capsule to gracefully close
         * the WebTransport session. This is only valid for connections using
         * WebTransport over HTTP/3. For raw QUIC connections, this method has no effect.
         *
         * The close session message allows the application to provide:
         * - An error code to indicate the reason for closure
         * - An optional error message string for debugging
         *
         * After sending the close session message, the WebTransport session will be
         * terminated and all associated streams will be cleaned up. This is typically
         * used when the application wants to explicitly close the session due to an
         * error condition or when normal session termination is required.
         *
         * @param connection WebTransport connection to close.
         * @param error_code WebTransport error code (application-defined)
         * @param error_msg Optional error message string (can be nullptr)
         *
         * @returns 0 on success, -1 on failure (e.g., not a WebTransport connection)
         */
        virtual int CloseWebTransportSession(const std::shared_ptr<Connection>& connection,
                                             uint32_t error_code,
                                             const char* error_msg = nullptr) = 0;

        /**
         * @brief Drain a WebTransport session gracefully
         *
         * @details Sends a DRAIN_WEBTRANSPORT_SESSION capsule to indicate that the
         * peer should finish sending any pending data and then close the session.
         * This is a more graceful shutdown compared to CloseWebTransportSession,
         * allowing both peers to complete ongoing operations before closure.
         *
         * The drain message signals to the peer that:
         * - No new operations should be started
         * - Existing operations should be completed
         * - The session will be closed after all pending data is sent
         *
         * This is typically used during normal application shutdown when you want
         * to ensure all data is properly flushed before closing the connection.
         *
         * Only valid for WebTransport connections. For raw QUIC connections,
         * this method has no effect.
         *
         * @param connection WebTransport connection to drain.
         *
         * @returns 0 on success, -1 on failure (e.g., not a WebTransport connection)
         */
        virtual int DrainWebTransportSession(const std::shared_ptr<Connection>& connection) = 0;

        /**
         * @brief Create a unidirectional stream to publish objects on.
         *
         * @param connection The connection for the stream.
         * @param priority Priority of the stream
         *
         * @returns Handle to the created stream. Hold it for as long as the stream is being written
         *      to, rather than looking it up per object.
         */
        virtual std::shared_ptr<Stream> CreateDataStream(const std::shared_ptr<Connection>& connection,
                                                         uint8_t priority) = 0;

        /**
         * @brief Create the connection's unidirectional outbound control stream.
         *
         * @details Carries no flow: control messages are not a track, and the stream's metrics are
         *      reported against the stream itself.
         *
         * @param connection Connection that owns the stream.
         * @returns Handle to the created stream, which the caller holds for the life of the connection.
         */
        virtual std::shared_ptr<Stream> CreateControlStream(const std::shared_ptr<Connection>& connection) = 0;

        /**
         * @brief Create a bidirectional request stream.
         *
         * @details One request is carried per stream, in both directions, so it has nothing to share
         *      state with and carries no flow.
         *
         * @param connection Connection that owns the stream.
         * @returns Handle to the created stream, which the caller holds for the life of the request.
         */
        virtual std::shared_ptr<Stream> CreateRequestStream(const std::shared_ptr<Connection>& connection) = 0;

        /**
         * @brief Stop network threads and release transport resources.
         *
         * @note Implementations should permit repeated calls. No callbacks should be expected after shutdown
         *      completes.
         */
        virtual void Shutdown() {}

      private:
        virtual void QueueDeferredReply(std::function<void()>&& reply_handler) = 0;

        template<typename T, typename E>
        friend class Reply;

      public:
        /**
         * @brief Callback invoked when the transport creates or accepts a connection.
         *
         * @details Runs on the transport notification thread. The callback receives shared ownership of the
         *      connection for the duration of the call.
         */
        std::function<void(const std::shared_ptr<Connection>&)> OnNewConnection;

        /**
         * @brief Callback invoked when a connection closes.
         *
         * @details Runs on the transport notification thread after transport closure processing.
         */
        std::function<void(const std::shared_ptr<Connection>&)> OnConnectionClosed;
    };
} // namespace quicr
