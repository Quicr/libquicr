// SPDX-FileCopyrightText: Copyright (c) 2026 Cisco Systems
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

#include "quicr/connection.h"
#include "quicr/containers/priority_queue.h"
#include "quicr/containers/safe_queue.h"
#include "quicr/containers/safe_time_queue.h"
#include "quicr/metrics.h"
#include "quicr/transport.h"
#include "stream.h"

#include <pico_webtransport.h>
#include <picoquic.h>
#include <picoquic_config.h>

#include <atomic>
#include <map>
#include <memory>
#include <mutex>
#include <utility>
#include <vector>

namespace quicr {

    /**
     * @brief State for a single QUIC stream, sending and receiving
     *
     * @details Streams are created and torn down on the picoquic thread, but handles are held by
     *      application threads and by picoquic itself, which stores one as the opaque app stream
     *      context. Holding a handle keeps the state alive for the duration of a call even if
     *      another thread erases the stream concurrently.
     *
     *      A stream carries both directions, so a bidirectional stream is one object rather than a
     *      send entry and an unrelated receive buffer. Receive-only streams, which the remote peer
     *      opened, have no transmit queue; send-only streams never populate the receive context.
     */
    class PicoQuicStream : public Stream
    {
      public:
        PicoQuicStream(std::uint64_t stream_id,
                       std::uint64_t conn_id,
                       std::unique_ptr<SafeTimeQueue<ConnData>> tx_queue);

        ~PicoQuicStream() = default;

        /// @returns Owning handle to this stream, for internals that hold only a raw pointer.
        std::shared_ptr<PicoQuicStream> SharedFromThis()
        {
            return std::static_pointer_cast<PicoQuicStream>(shared_from_this());
        }

        /**
         * Reset the TX object buffer
         */
        void ResetTxObject()
        {
            tx_object = nullptr;
            tx_object_offset = 0;
        }

      public:
        /// Instructs that the stream should be closed upon empty
        bool close_on_empty : 1 { false };

        /// Instructs to use reset when closing stream
        bool close_using_reset : 1 { false };

        /// The priority of the stream.
        uint8_t priority{ 0 };

        /// Pending objects to be written to the network
        std::unique_ptr<SafeTimeQueue<ConnData>> tx_data;

        /// Current object that is being sent as a byte stream
        std::shared_ptr<const std::vector<uint8_t>> tx_object;

        /// Pointer offset to next byte to send
        size_t tx_object_offset{ 0 };

        // The last ticks when TX callback was run
        uint64_t last_tx_tick{ 0 };

        /// WebTransport stream context (only used in WebTransport mode)
        h3zero_stream_ctx_t* wt_stream_ctx{ nullptr };

        /// Metrics for this stream, taken and reset each sample period
        QuicStreamMetrics metrics;

        /// Delayed transmit callbacks since the last congestion check, which runs on its own cadence
        std::uint64_t tx_delayed_since_cc_check{ 0 };

        /**
         * @name Receive state
         *
         * @details Touched only on the picoquic thread, except for `rx_ctx`, which is handed to the
         *      notify thread and is internally synchronised.
         */
        ///@{

        /// Received data queue and the caller state that consumes it
        std::shared_ptr<StreamRxContext> rx_ctx;

        /// Indicates if the receive side is closed
        bool rx_closed{ false };

        /// True once the closed stream has been checked for removal, so its queue gets one drain pass
        bool rx_checked_once{ false };

        ///@}
    };

    class PicoQuicConnection : public Connection
    {
      public:
        PicoQuicConnection(picoquic_cnx_t* cnx, API api = API::kNativeQuic)
          : Connection(reinterpret_cast<std::uint64_t>(cnx), api)
          , pq_cnx(cnx)
          , dgram_rx_data(std::make_shared<SafeQueue<std::shared_ptr<const Bytes>>>())
        {
        }

        virtual ~PicoQuicConnection() = default;

        QuicMetricsSample TakeMetricsSample() override;

        void ReportMetricsSample(const MetricsTimeStamp& sample_time, const QuicMetricsSample& sample) override;

        /**
         * @name Stream access
         *
         * @details The connection is the sole owner of every stream on it, sending and receiving
         *      alike, so a bidirectional stream is one object rather than an entry in two unrelated
         *      registries. Streams are added and removed on the picoquic thread while application
         *      threads queue data on them, so the container is guarded by its own mutex. Callers
         *      receive an owning handle rather than a pointer into the container, which keeps the
         *      stream alive for the duration of the call even if another thread removes it
         *      concurrently.
         *
         * @warning `stream_mutex_` MUST NOT be held while calling picoquic or any transport method, to
         *      keep it a leaf lock. It may be taken while holding the transport's state mutex, never
         *      the other way around.
         */
        ///@{

        /// @returns Handle to the stream, or nullptr if no such stream exists.
        std::shared_ptr<PicoQuicStream> GetStream(std::uint64_t stream_id) const;

        /**
         * @returns Handle to the newly created stream, replacing any existing stream with that ID.
         *
         * @param stream_id  QUIC stream ID
         * @param tx_queue   Transmit queue, or nullptr for a receive-only stream
         */
        std::shared_ptr<PicoQuicStream> AddStream(std::uint64_t stream_id,
                                                  std::unique_ptr<SafeTimeQueue<ConnData>> tx_queue);

        /**
         * @returns Handle to the existing stream, creating one if absent.
         *
         * @details For remote-initiated streams, which the transport first learns about when data
         *      arrives on them. A bidirectional stream is a request the application replies on, so it
         *      is given a TX queue; a unidirectional one is receive-only and passes nullptr.
         */
        std::shared_ptr<PicoQuicStream> GetOrAddStream(std::uint64_t stream_id,
                                                       std::unique_ptr<SafeTimeQueue<ConnData>> tx_queue);

        /// @returns Handle to the removed stream, or nullptr if no such stream existed.
        std::shared_ptr<PicoQuicStream> RemoveStream(std::uint64_t stream_id);

        /// @returns Snapshot of all streams, safe to iterate without holding the lock.
        std::vector<std::shared_ptr<PicoQuicStream>> GetStreams() const;

        ///@}

      public:
        /// Picoquic connection/path context
        picoquic_cnx_t* pq_cnx = nullptr;

        /// last stream Id
        std::uint64_t last_stream_id{ 0 };

        /// Picoquic shard ID.
        std::size_t shard_idx{ 0 };

        /// Datagram pending objects to be written to the network
        std::shared_ptr<PriorityQueue<ConnData>> dgram_tx_data;

        /// Buffered datagrams received from the network
        std::shared_ptr<SafeQueue<std::shared_ptr<const Bytes>>> dgram_rx_data;

        /// Priority last given to picoquic for this connection's datagrams; unset until the first send
        std::optional<std::uint8_t> dgram_priority;

        /// Metrics for the connection's single datagram channel
        QuicDatagramMetrics dgram_metrics;

        /**
         * WebTransport stream ID to stream mapping
         *
         * @details WebTransport cannot use picoquic's app stream context slot, because picowt owns it
         * and stores its own `h3zero_stream_ctx_t*` there. This is the fallback lookup for that path.
         * The mapping does not own the streams; the connection does.
         */
        std::map<std::uint64_t, std::weak_ptr<PicoQuicStream>> wt_stream_to_stream;

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

        std::uint16_t peer_port{ 0 };

        sockaddr_storage peer_addr;

        // States
        bool is_congested{ false };

        /// Interval gauge count of consecutive not congested checks
        std::uint16_t not_congested_gauge{ 0 };

        // Metrics
        QuicConnectionMetrics metrics;

      private:
        /// Every stream on this connection, sending and receiving, keyed by stream ID
        std::map<std::uint64_t, std::shared_ptr<PicoQuicStream>> streams_;

        /**
         * What streams removed since the last sample had left to report
         *
         * @details A stream that opens and closes between two samples is gone before the period it
         *      ran in ends, so what it did not report is held here for that period to carry.
         */
        std::vector<QuicMetricsSample::Stream> unreported_stream_metrics_;

        /// Guards streams_ and unreported_stream_metrics_
        mutable std::mutex stream_mutex_;
    };
}
