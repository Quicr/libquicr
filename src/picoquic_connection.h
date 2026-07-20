// SPDX-FileCopyrightText: Copyright (c) 2026 Cisco Systems
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

#include "quicr/connection.h"
#include "quicr/containers/priority_queue.h"
#include "quicr/containers/safe_queue.h"
#include "quicr/containers/safe_time_queue.h"
#include "quicr/metrics.h"
#include "quicr/transport.h"

#include <pico_webtransport.h>
#include <picoquic.h>
#include <picoquic_config.h>

#include <memory>

namespace quicr {

    /**
     * Data context information
     *  Data context is intended to be a container for metrics and other state that is
     *  related to a flow of data that may use datagram or one or more stream QUIC frames
     */
    struct DataContext
    {
      public:
        DataContext() = default;
        DataContext(DataContext&& other)
          : data_ctx_id(other.data_ctx_id)
          , conn_id(other.conn_id)
          , is_bidir(other.is_bidir)
          , uses_reset_wait(other.uses_reset_wait)
          , delete_on_empty(other.delete_on_empty)
          , metrics(std::move(other.metrics))
        {
        }

        DataContext& operator=(const DataContext&) = delete;
        DataContext& operator=(DataContext&&) = delete;

        ~DataContext() = default;

      public:
        std::uint64_t data_ctx_id{ 0 };     /// The ID of this context
        std::uint64_t conn_id{ 0 };         /// The connection ID this context is under
        bool is_bidir : 1 { false };        /// Indicates if the stream is bidir (true) or unidir (false)
        bool uses_reset_wait : 1 { false }; /// Indicates if data context can/uses reset wait strategy
        bool delete_on_empty{ false };      /// Instructs TX objects to be discarded on POP instead

        QuicDataContextMetrics metrics;

        struct StreamContext
        {
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

            /// Instructs TX objects to be discarded on POP instead
            bool tx_reset_wait_discard{ false };

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
        };

        std::map<std::uint64_t, StreamContext> streams;
        std::mutex stream_mutex;
    };

    class PicoQuicConnection : public Connection
    {
      public:
        /**
         * Active stream buffers for received unidirectional streams
         */
        struct RxStreamBuffer
        {
            RxStreamBuffer()
              : rx_ctx(std::make_shared<StreamRxContext>())
            {
                rx_ctx->caller_any.reset();
                rx_ctx->data_queue.Clear();
            }

            /// Stream RX context that holds data and caller info
            std::shared_ptr<StreamRxContext> rx_ctx;

            /// Indicates if stream is active or in closed state
            bool closed{ false };

            /// True if closed and checked once to close
            bool checked_once{ false };
        };

      public:
        PicoQuicConnection(picoquic_cnx_t* cnx, API api = API::kNativeQuic)
          : Connection(reinterpret_cast<std::uint64_t>(cnx), api)
          , pq_cnx(cnx)
          , dgram_rx_data(std::make_shared<SafeQueue<std::shared_ptr<const Bytes>>>())
        {
        }

        virtual ~PicoQuicConnection() = default;

        void SampleMetrics(const MetricsTimeStamp& sample_time) override;

      public:
        /// Picoquic connection/path context
        picoquic_cnx_t* pq_cnx = nullptr;

        /// last stream Id
        std::uint64_t last_stream_id{ 0 };

        /// Picoquic shard ID.
        std::size_t shard_idx{ 0 };

        /// Next data context ID; zero is reserved for default context
        std::uint64_t next_data_ctx_id{ 1 };

        /// Datagram pending objects to be written to the network
        std::shared_ptr<PriorityQueue<ConnData>> dgram_tx_data;

        /// Buffered datagrams received from the network
        std::shared_ptr<SafeQueue<std::shared_ptr<const Bytes>>> dgram_rx_data;

        /// Instructs datagram to be marked ready/active
        bool mark_dgram_ready{ false };

        /// Map of stream receive buffers, key is stream_id
        std::map<std::uint64_t, RxStreamBuffer> rx_stream_buffer;

        /**
         * Active data contexts (streams bidir/unidir and datagram)
         */
        std::map<std::uint64_t, DataContext> active_data_contexts;

        /**
         * WebTransport stream ID to data context ID mapping
         * Used in WebTransport mode to look up data context for a stream
         */
        std::map<std::uint64_t, std::uint64_t> wt_stream_to_data_ctx;

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
    };
}
