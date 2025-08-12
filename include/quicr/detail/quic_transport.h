// SPDX-FileCopyrightText: Copyright (c) 2024 Cisco Systems
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

#include "quic_transport_metrics.h"
#include "quicr/detail/data_storage.h"
#include "quicr/detail/tick_service.h"
#include "safe_queue.h"
#include "stream_buffer.h"
#include "uintvar.h"
#include <span>

#include <spdlog/spdlog.h>

#include <any>
#include <array>
#include <chrono>
#include <cstdlib>
#include <memory>
#include <mutex>
#include <optional>
#include <queue>
#include <source_location>
#include <string>
#include <sys/socket.h>
#include <vector>

namespace quicr {

    using TransportConnId = uint64_t; ///< Connection Id is a 64bit number that is used as a key to maps
    using DataContextId = uint64_t;   ///< Data Context 64bit number that identifies a data flow/track/stream
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
        kShuttingDown
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
        kInvalidDataContextId,
        kInvalidIpv4Address,
        kInvalidIpv6Address,
        kInvalidStreamId
    };

    /**
     * Transport Protocol to use
     */
    enum class TransportProtocol : uint8_t
    {
        kUdp = 0,
        kQuic
    };

    /**
     * @brief Remote/Destination endpoint address info.
     *
     * @details Remote destination is either a client or server hostname/ip and port
     */
    struct TransportRemote
    {
        std::string host_or_ip;  /// IPv4/v6 or FQDN (user input)
        uint16_t port;           /// Port (user input)
        TransportProtocol proto; /// Protocol to use for the transport
    };

    /**
     * Transport configuration parameters
     */
    struct TransportConfig
    {
        std::string tls_cert_filename;               /// QUIC TLS certificate to use
        std::string tls_key_filename;                /// QUIC TLS private key to use
        uint32_t time_queue_init_queue_size{ 1000 }; /// Initial queue size to reserve upfront
        uint32_t time_queue_max_duration{ 2000 };    /// Max duration for the time queue in milliseconds
        uint32_t time_queue_bucket_interval{ 1 };    /// The bucket interval in milliseconds
        uint32_t time_queue_rx_size{ 1000 };         /// Receive queue size
        bool debug{ false };                         /// Enable debug logging/processing
        uint64_t quic_cwin_minimum{ 131072 };        /// QUIC congestion control minimum size (default is 128k)
        uint32_t quic_wifi_shadow_rtt_us{ 20000 };   /// QUIC wifi shadow RTT in microseconds

        uint64_t pacing_decrease_threshold_bps{ 16000 }; /// QUIC pacing rate decrease threshold for notification in Bps
        uint64_t pacing_increase_threshold_bps{ 16000 }; /// QUIC pacing rate increase threshold for notification in Bps

        uint64_t idle_timeout_ms{ 30000 };     /// Idle timeout for transport connection(s) in milliseconds
        bool use_reset_wait_strategy{ false }; /// Use Reset and wait strategy for congestion control
        bool use_bbr{ true };                  /// Use BBR if true, NewReno if false
        std::string quic_qlog_path;            /// If present, log QUIC LOG file to this path
        uint8_t quic_priority_limit{ 0 };      /// Lowest priority that will not be bypassed from pacing/CC in picoquic
        std::size_t max_connections{ 1 };
        bool ssl_keylog{ false }; ///< Enable SSL key logging for QUIC connections
    };

    /// Stream action that should be done by send/receive processing
    enum class StreamAction : uint8_t
    {
        kNoAction = 0,
        kReplaceStreamUseReset,
        kReplaceStreamUseFin,
    };

    struct ConnData
    {
        TransportConnId conn_id;
        DataContextId data_ctx_id;
        uint8_t priority;
        StreamAction stream_action{ StreamAction::kNoAction };

        /// Shared pointer is used so transport can take ownership of the vector without copy/new allocation
        std::shared_ptr<const std::vector<uint8_t>> data;

        uint64_t tick_microseconds; // Tick value in microseconds
    };

    /// Stream receive data context
    struct StreamRxContext
    {
        std::any caller_any; ///< Caller any object - Set and used by caller/app
        bool is_new{ true }; ///< Indicates if new stream, on read set to false

        /// Data queue for received data on the stream
        SafeQueue<std::shared_ptr<const std::vector<uint8_t>>> data_queue;
    };

    struct TransportException : std::runtime_error
    {
        TransportException(TransportError, std::source_location = std::source_location::current());

        TransportError Error;
    };

    /**
     * @brief ITransport interface
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
     * @note Some implementations may cho/ose to
     * 	have enqueue/dequeue being blocking. However
     * 	in such cases applications needs to
     * 	take the burden of non-blocking flows.
     */
    class ITransport
    {
      public:
        /**
         * @brief Async Callback API on the transport
         */
        class TransportDelegate
        {
          public:
            virtual ~TransportDelegate() = default;

            /**
             * @brief Event notification for connection status changes
             *
             * @details Called when the connection changes state/status
             *
             * @param[in] conn_id           Transport context Id
             * @param[in] status 	    Transport Status value
             */
            virtual void OnConnectionStatus(const TransportConnId& conn_id, TransportStatus status) = 0;

            /**
             * @brief Report arrival of a new connection
             *
             * @details Called when new connection is received. This is only used in
             * server mode.
             *
             * @param[in] conn_id	Transport context identifier mapped to the connection
             * @param[in] remote	Transport information for the connection
             */
            virtual void OnNewConnection(const TransportConnId& conn_id, const TransportRemote& remote) = 0;

            /**
             * @brief Report a new data context created
             *
             * @details Report that a new data context was created for a new bi-directional
             *  stream that was received. This method is not called for app created
             *  data contexts.
             *
             * @param[in] conn_id	Transport context identifier mapped to the connection
             * @param[in] data_ctx_id	Data context id for a new data context received by the transport
             */
            virtual void OnNewDataContext(const TransportConnId& conn_id, const DataContextId& data_ctx_id) = 0;

            /**
             * @brief callback notification that data has been received and should be processed
             *
             * @param[in] conn_id 	Transport context identifier mapped to the connection
             * @param[in] data_ctx_id	If known, Data context id that the data was received on
             */
            virtual void OnRecvDgram(const TransportConnId& conn_id, std::optional<DataContextId> data_ctx_id) = 0;

            /**
             * @brief callback notification that data has been received and should be processed
             *
             * @param[in] conn_id 	Transport context identifier mapped to the connection
             * @param[in] stream_id     Transport stream ID
             * @param[in] data_ctx_id	If known, Data context id that the data was received on
             * @param[in] is_bidir      True if the message is from a bidirectional stream
             */
            virtual void OnRecvStream(const TransportConnId& conn_id,
                                      uint64_t stream_id,
                                      std::optional<DataContextId> data_ctx_id,
                                      bool is_bidir = false) = 0;

            /**
             * @brief callback notification on connection metrics sampled
             *
             * @details This callback will be called when the connection metrics are sampled per connection
             *
             * @param sample_time                    Sample time in microseconds
             * @param conn_id                        Connection ID for the metrics
             * @param quic_connection_metrics        Connection specific metrics for sample period
             */
            virtual void OnConnectionMetricsSampled(
              [[maybe_unused]] const MetricsTimeStamp sample_time,
              [[maybe_unused]] const TransportConnId conn_id,
              [[maybe_unused]] const QuicConnectionMetrics& quic_connection_metrics)
            {
            }

            /**
             * @brief callback notification on data context metrics sampled
             *
             * @details This callback will be called when the data context metrics are sampled
             *
             * @param sample_time                   Sample time in microseconds
             * @param conn_id                       Connection ID for metrics
             * @param data_ctx_id                   Data context ID for metrics
             * @param quic_data_context_metrics     Data context metrics for sample period
             */
            virtual void OnDataMetricsSampled([[maybe_unused]] const MetricsTimeStamp sample_time,
                                              [[maybe_unused]] const TransportConnId conn_id,
                                              [[maybe_unused]] const DataContextId data_ctx_id,
                                              [[maybe_unused]] const QuicDataContextMetrics& quic_data_context_metrics)
            {
            }
        };

        /* Factory APIs */

        /**
         * @brief Create a new client transport based on the remote (server) host/ip
         *
         * @param[in] server        Transport remote server information
         * @param[in] tcfg          Transport configuration
         * @param[in] delegate      Implemented callback methods
         * @param[in] tick_service  Shared pointer to the tick service to use
         * @param[in] logger        Shared pointer to logger
         *
         * @return shared_ptr for the under lining transport.
         */
        static std::shared_ptr<ITransport> MakeClientTransport(const TransportRemote& server,
                                                               const TransportConfig& tcfg,
                                                               TransportDelegate& delegate,
                                                               std::shared_ptr<TickService> tick_service,
                                                               std::shared_ptr<spdlog::logger> logger);

        /**
         * @brief Create a new server transport based on the remote (server) ip and
         * port
         *
         * @param[in] server      Transport remote server information
         * @param[in] tcfg        Transport configuration
         * @param[in] delegate    Implemented callback methods
         * @param[in] logger      Shared pointer to logger
         *
         * @return shared_ptr for the under lining transport.
         */
        static std::shared_ptr<ITransport> MakeServerTransport(const TransportRemote& server,
                                                               const TransportConfig& tcfg,
                                                               TransportDelegate& delegate,
                                                               std::shared_ptr<TickService> tick_service,
                                                               std::shared_ptr<spdlog::logger> logger);

      public:
        virtual ~ITransport() = default;

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
        virtual TransportConnId Start() = 0;

        /**
         * @brief Create a data context
         * @details Data context is flow of data (track, namespace). This is similar to a pipe of data to be
         * transmitted. Metrics, shaping, etc. maintained at the data context level.
         *
         * @param[in] conn_id                 Connection ID to create data context
         * @param[in] use_reliable_transport 	Indicates a reliable stream is
         *                                 	preferred for transporting data
         * @param[in] priority                Priority for stream (default is 1)
         * @param[in] bidir                   Set context to be bi-directional or unidirectional
         *
         * @return DataContextId identifying the data context via the connection
         */
        virtual DataContextId CreateDataContext(TransportConnId conn_id,
                                                bool use_reliable_transport,
                                                uint8_t priority = 1,
                                                bool bidir = false) = 0;

        /**
         * @brief Close a transport context
         *
         * @param conn_id           Connection ID to close
         * @param app_reason_code   Application reason code to use when closing QUIC connnection
         */
        virtual void Close(const TransportConnId& conn_id, uint64_t app_reason_code = 0) = 0;

        /**
         * @brief Delete data context
         * @details Deletes a data context for the given connection id. If reliable, the stream will
         *    be closed by FIN (graceful).
         *
         * @param[in] conn_id                 Connection ID to create data context
         * @param[in] data_ctx_id             Data context ID to delete
         */
        virtual void DeleteDataContext(const TransportConnId& conn_id, DataContextId data_ctx_id) = 0;

        /**
         * @brief Get the peer IP address and port associated with the stream
         *
         * @param[in]  context_id	Identifying the connection
         * @param[out] addr	Peer address
         *
         * @returns True if the address was successfully returned, false otherwise
         */
        virtual bool GetPeerAddrInfo(const TransportConnId& context_id, sockaddr_storage* addr) = 0;

        /**
         * @brief Set the data context id for RX unidir stream id
         *
         * @param conn_id                 Connection ID of the data context ID
         * @param data_ctx_id             Local data context ID
         * @param stream_id               RX stream ID
         */
        virtual void SetStreamIdDataCtxId(TransportConnId conn_id, DataContextId data_ctx_id, uint64_t stream_id) = 0;

        /**
         * @brief Set/update prioirty for the data context
         *
         * @param conn_id                 Connection ID of the data context ID
         * @param data_ctx_id             Local data context ID
         * @param priority                Priority for data context stream, range should be 0 - 255
         */
        virtual void SetDataCtxPriority(TransportConnId conn_id, DataContextId data_ctx_id, uint8_t priority) = 0;

        /**
         * @brief Set the remote data context id
         * @details sets the remote data context id for data objects transmitted
         *
         * @param conn_id                  Connection ID
         * @param data_ctx_id              Local data context ID
         * @param remote_data_ctx_id       Remote data context ID (learned via subscribe/publish)
         */
        virtual void SetRemoteDataCtxId(TransportConnId conn_id,
                                        DataContextId data_ctx_id,
                                        DataContextId remote_data_ctx_id) = 0;

        /**
         * Enqueue flags
         */
        struct EnqueueFlags
        {
            bool use_reliable{ false };   /// Indicates if object should use reliable stream or unreliable
            bool new_stream{ false };     /// Indicates that a new stream should be created to replace existing one
            bool clear_tx_queue{ false }; /// Indicates that the TX queue should be cleared before adding new object
            bool use_reset{ false };      /// Indicates new stream created will close the previous using reset/abrupt
        };

        /**
         * @brief Enqueue application data within the transport
         *
         * @details Add data to the transport queue. Data enqueued will be transmitted
         * when available.
         *
         * @param[in] context_id	Identifying the connection
         * @param[in] data_ctx_id	stream Id to send data on
         * @param[in] group_id     The id of the group we're currently enqueuing.
         * @param[in] bytes		    Data to send/write
         * @param[in] priority      Priority of the object, range should be 0 - 255
         * @param[in] ttl_ms        The age the object should exist in queue in milliseconds
         * @param[in] delay_ms      Delay the pop by millisecond value
         * @param[in] flags         Flags for stream and queue handling on enqueue of object
         *
         * @returns TransportError is returned indicating status of the operation
         */
        virtual TransportError Enqueue(const TransportConnId& context_id,
                                       const DataContextId& data_ctx_id,
                                       std::uint64_t group_id,
                                       std::shared_ptr<const std::vector<uint8_t>> bytes,
                                       uint8_t priority = 1,
                                       uint32_t ttl_ms = 350,
                                       uint32_t delay_ms = 0,
                                       EnqueueFlags flags = { true, false, false, false }) = 0;

        /**
         * @brief Dequeue datagram application data from transport buffer
         *
         * @details Data received by the transport will be queued and made available
         * to the caller using this method.  An empty return will be
         *
         * @param[in] conn_id		        Identifying the connection
         * @param[in] data_ctx_id             Data context ID if known
         *
         * @returns std::nullopt if there is no data
         */
        virtual std::shared_ptr<const std::vector<uint8_t>> Dequeue(TransportConnId conn_id,
                                                                    std::optional<DataContextId> data_ctx_id) = 0;

        /**
         * @brief Get the stream RX context by connection ID and stream ID
         *
         * @param conn_id                   Connection ID to get stream context from
         * @param stream_id                 Context stream ID
         *
         * @returns Shared pointer to StreamRxContext
         * @throws TransportError for invalid connection or stream id
         */
        virtual std::shared_ptr<StreamRxContext> GetStreamRxContext(TransportConnId conn_id, uint64_t stream_id) = 0;
    };
} // namespace quicr
