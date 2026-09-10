// SPDX-FileCopyrightText: Copyright (c) 2024 Cisco Systems
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

#include "quicr/config.h"
#include "quicr/containers/safe_queue.h"
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
#include <sys/socket.h>
#include <vector>

namespace quicr {

    class Connection;
    class Logger;
    class Stream;
    class SubscribeTrackHandler;

    /**
     * Close Connection App Reasons
     */
    enum class AppReasonForClose : uint8_t
    {
        kShutdown = 1,
        kIdleTimeout,
        kRemoteRequestClose,
        kInternalError,
        kProtocolViolation,
        kNotAuthorized,
        kUnknown,
    };

    /**
     * Transport status/state values
     */
    enum class TransportStatus : uint8_t
    {
        kReady = 0,
        kConnecting,
        kRemoteRequestClose,
        kDisconnected,
        kIdleTimeout,
        kShutdown,
        kShuttingDown,
    };

    /**
     * Transport errors
     */
    enum class TransportError : uint8_t
    {
        kNone = 0,
        kQueueFull,
        kUnknownError,
        kPeerDisconnected,
        kPeerUnreachable,
        kCannotResolveHostname,
        kInvalidConnContextId,
        kInvalidIpv4Address,
        kInvalidIpv6Address,
        kInvalidStreamId,
        kFailedToCreateQuicInstance
    };

    /**
     * Transport Protocol to use
     */
    enum class TransportProtocol : uint8_t
    {
        kQuic,
        kWebTransport
    };

    /**
     * @brief Remote/Destination endpoint address info.
     *
     * @details Remote destination is either a client or server hostname/ip and port
     */
    struct TransportRemote
    {
        std::string host_or_ip;      /// IPv4/v6 or FQDN (user input)
        uint16_t port;               /// Port (user input)
        TransportProtocol proto;     /// Protocol to use for the transport
        std::string path{ "relay" }; /// When using WT, the path to use
    };

    /// Stream action that should be done by send/receive processing
    enum class StreamAction : uint8_t
    {
        kNoAction = 0,
        kCloseStreamUseReset,
        kCloseStreamUseFin,
    };

    struct ConnData
    {
        std::uint64_t conn_id;
        uint8_t priority;
        StreamAction stream_action{ StreamAction::kNoAction };

        /// Shared pointer is used so transport can take ownership of the vector without copy/new allocation
        std::shared_ptr<const std::vector<uint8_t>> data;

        uint64_t tick_microseconds; // Tick value in microseconds
    };

    /// Received stream data and the handler that consumes it
    struct StreamRxContext
    {
        /**
         * Handler consuming this stream, bound once the stream header identifies its track
         *
         * @details Weak so a handler that goes away while data is still arriving is detected rather
         *      than kept alive by the transport. Empty until the header is parsed, which is the same
         *      condition as `is_new`.
         */
        std::weak_ptr<SubscribeTrackHandler> handler;

        bool is_new{ true }; ///< Indicates if new stream, on read set to false

        /**
         * Future tick value in milliseconds that indicates this context has
         * expired due to being unknown.  A value of zero indicates
         * It's no longer unknown and will not expire.
         */
        uint64_t unknown_expiry_tick_ms{ 0 };

        /// Data queue for received data on the stream
        SafeQueue<std::shared_ptr<const std::vector<uint8_t>>> data_queue;

        /// True if we're waiting to be read.
        std::atomic<bool> notify_pending{ false };
    };

    struct TransportException : std::runtime_error
    {
        TransportException(TransportError, std::source_location = std::source_location::current());

        TransportError Error;
    };

    enum class StreamOperation : uint8_t
    {
        kFin,
        kReset,
        kStopSending,
        // Cancel a bidirectional stream: RESET the send side and STOP_SENDING the receive side.
        kCancel,
    };

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
     * @details A single threaded, async transport interface.
     * 	The transport implementations own the queues
     * 	on which the applications can enqueue the messages
     * 	for transmitting and dequeue for consumption
     *
     * 	Applications using this transport interface
     * 	MUST treat it as thread-unsafe and the same
     * 	is ensured by the transport owing the lock and
     * 	access to the queues.
     *
     * @note Some implementations may choose to
     * 	have enqueue/dequeue being blocking. However
     * 	in such cases applications needs to
     * 	take the burden of non-blocking flows.
     */
    class Transport
    {
      public:
        /* Factory APIs */

        /**
         * @brief Create a new client transport based on the remote (server) host/ip
         *
         * @param[in] server        Transport remote server information
         * @param[in] tcfg          Transport configuration
         * @param[in] tick_service  Shared pointer to the tick service to use
         * @param[in] logger        Shared pointer to logger
         *
         * @return shared_ptr for the under lining transport.
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
         * @param[in] server      Transport remote server information (server.proto is ignored)
         * @param[in] tcfg        Transport configuration
         * @param[in] tick_service Shared pointer to tick service
         * @param[in] logger      Shared pointer to logger
         *
         * @return shared_ptr for the underlying transport
         */
        static std::shared_ptr<Transport> MakeServerTransport(const TransportRemote& server,
                                                              const TransportConfig& tcfg,
                                                              std::shared_ptr<timeq::tick_service> tick_service,
                                                              std::shared_ptr<Logger> logger);

      public:
        virtual ~Transport() = default;

        /**
         * @brief Status of the transport
         *
         * @details Return the status of the transport. In server mode, the transport
         * will reflect the status of the listening socket. In client mode it will
         * reflect the status of the server connection.
         */
        virtual TransportStatus Status() const = 0;

        /**
         * @brief Setup the transport connection
         *
         * @details In server mode this will create the listening socket and will
         * 		start listening on the socket for new connections. In client
         * mode this will initiate a connection to the remote/server.
         *
         * @return TransportContextId: identifying the connection
         */
        virtual std::shared_ptr<Connection> Start() = 0;

        /**
         * @brief Close a transport context
         *
         * @param conn_id           Connection ID to close
         * @param app_reason        Application reason to close the connection
         */
        virtual void Close(const std::shared_ptr<Connection>& connection,
                           AppReasonForClose app_reason = AppReasonForClose::kRemoteRequestClose) = 0;

        /**
         * @brief Close a stream
         *
         * @param connection        Connection the stream belongs to
         * @param stream            Stream to close; no-op if null or already closed
         * @param use_reset         True to close by RESET, false to close by FIN
         */
        virtual void CloseStream(const std::shared_ptr<Connection>& connection,
                                 const std::shared_ptr<Stream>& stream,
                                 StreamOperation operation) = 0;

        /**
         * @brief Close a stream by ID
         *
         * @details For receive streams, which the caller only knows by ID. Prefer the handle form
         *      wherever one is available.
         *
         * @param connection        Connection the stream belongs to
         * @param stream_id         Stream ID to close
         * @param operation         Operation to use to close the stream
         */
        virtual void CloseStream(const std::shared_ptr<Connection>& connection,
                                 uint64_t stream_id,
                                 StreamOperation operation) = 0;

        /**
         * @brief Get the peer IP address and port associated with the stream
         *
         * @param[in]  context_id	Identifying the connection
         * @param[out] addr	Peer address
         *
         * @returns True if the address was successfully returned, false otherwise
         */
        virtual bool GetPeerAddrInfo(const std::shared_ptr<Connection>& connection, sockaddr_storage* addr) = 0;

        /**
         * Enqueue flags
         */
        struct EnqueueFlags
        {
            bool use_reliable{ false };   /// Indicates if object should use reliable stream or unreliable
            bool close_stream{ false };   /// Indicates that a new stream should be created to replace existing one
            bool clear_tx_queue{ false }; /// Indicates that the TX queue should be cleared before adding new object
            bool use_reset{ false };      /// Indicates new stream created will close the previous using reset/abrupt
        };

        /**
         * @brief Enqueue application data on a stream
         *
         * @details Add data to the stream's transmit queue, to be sent when the stream is next able to.
         *
         * @param[in] connection    Identifying the connection
         * @param[in] stream        Stream to send on
         * @param[in] bytes         Data to send/write
         * @param[in] priority      Priority of the object, range should be 0 - 255
         * @param[in] ttl_ms        The age the object should exist in queue in milliseconds
         * @param[in] flags         Flags for stream and queue handling on enqueue of object
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
         * @param[in] connection    Identifying the connection
         * @param[in] bytes         Data to send/write
         * @param[in] priority      Priority of the object, range should be 0 - 255
         * @param[in] ttl_ms        The age the object should exist in queue in milliseconds
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
         * @details Data received by the transport will be queued and made available
         * to the caller using this method.  An empty return will be
         *
         * @param[in] connection            Identifying the connection
         *
         * @returns std::nullopt if there is no data
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
         * @param conn_id           Connection ID to close WebTransport session
         * @param error_code        WebTransport error code (application-defined)
         * @param error_msg         Optional error message string (can be nullptr)
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
         * @param conn_id           Connection ID to drain WebTransport session
         *
         * @returns 0 on success, -1 on failure (e.g., not a WebTransport connection)
         */
        virtual int DrainWebTransportSession(const std::shared_ptr<Connection>& connection) = 0;

        /**
         * @brief Create a unidirectional stream to publish objects on.
         *
         * @param connection    The connection for the stream.
         * @param priority      Priority of the stream
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
         * @returns Handle to the created stream, which the caller holds for the life of the connection.
         */
        virtual std::shared_ptr<Stream> CreateControlStream(const std::shared_ptr<Connection>& connection) = 0;

        /**
         * @brief Create a bidirectional request stream.
         *
         * @details One request is carried per stream, in both directions, so it has nothing to share
         *      state with and carries no flow.
         *
         * @returns Handle to the created stream, which the caller holds for the life of the request.
         */
        virtual std::shared_ptr<Stream> CreateRequestStream(const std::shared_ptr<Connection>& connection) = 0;

        /**
         * @brief Stop network threads and release transport resources.
         */
        virtual void Shutdown() {}

      public:
        std::function<void(const std::shared_ptr<Connection>&)> OnNewConnection;
        std::function<void(const std::shared_ptr<Connection>&)> OnConnectionClosed;
    };
} // namespace quicr
