// SPDX-FileCopyrightText: Copyright (c) 2024 Cisco Systems
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

#include <chrono>

#include "safe_queue.h"

namespace quicr {
    /*
     * Min/Max/Avg structure
     */
    struct MinMaxAvg
    {
        uint64_t min{ 0 }; /// Minimum value in period
        uint64_t max{ 0 }; /// Maximum value in period
        uint64_t avg{ 0 }; /// Average value in period

        uint64_t value_sum{ 0 };   /// Accumulating sum of values in period
        uint64_t value_count{ 0 }; /// Number of values in period

#if __cplusplus >= 202002L
        constexpr auto operator<=>(const MinMaxAvg&) const = default;
#else
        constexpr bool operator==(const MinMaxAvg& other) const
        {
            return min == other.min && max == other.max && avg == other.avg && value_sum == other.value_sum;
        }
        constexpr bool operator!=(const MinMaxAvg& other) const { return !(*this == other); }
#endif

        /**
         * @brief  Add value to period
         *
         * @details Add value will update min/max/avg based on the value being added.
         *
         * @param value           The value to add.
         */
        void AddValue(const uint64_t value)
        {
            min = min ? std::min(min, value) : value;
            max = std::max(max, value);

            value_sum += value;
            value_count++;

            if (!value_count)
                value_count = 1;

            avg = value_sum / value_count;
        }

        void Clear()
        {
            min = 0;
            max = 0;
            avg = 0;
            value_sum = 0;
            value_count = 0;
        }
    };

    /*
     *  QUIC Metrics
     */
    struct QuicConnectionMetrics
    {
        uint64_t cwin_congested{ 0 };      ///< Number of times CWIN is low or zero (congested)
        uint64_t prev_cwin_congested{ 0 }; ///< Previous number of times CWIN is congested

        uint64_t tx_congested{ 0 }; ///< count of times transmit connection is considered congested

        MinMaxAvg tx_rate_bps;         ///< Rate in bits per second in period
        MinMaxAvg rx_rate_bps;         ///< Estimated rate in bits per second in period
        MinMaxAvg tx_cwin_bytes;       ///< Congestion window bytes in period
        MinMaxAvg tx_in_transit_bytes; ///< Number of bytes in transit
        MinMaxAvg rtt_us;              ///< Round trip time in microseconds in period
        MinMaxAvg srtt_us;             ///< Smooth Round trip time in microseconds in period

        uint64_t tx_retransmits{ 0 };     ///< count of retransmits
        uint64_t tx_lost_pkts{ 0 };       ///< Number of lost packets sent
        uint64_t tx_timer_losses{ 0 };    ///< Packet losses detected due to timer expiring
        uint64_t tx_spurious_losses{ 0 }; ///< Number of packet lost that were later acked

        uint64_t rx_dgrams{ 0 };       ///< count of datagrams received
        uint64_t rx_dgrams_bytes{ 0 }; ///< Number of receive datagram bytes

        uint64_t tx_dgram_cb{ 0 };       ///< count of picoquic callback for datagram can be sent
        uint64_t tx_dgram_ack{ 0 };      ///< count of picoquic callback for acked datagrams
        uint64_t tx_dgram_lost{ 0 };     ///< count of picoquic callback for lost datagrams
        uint64_t tx_dgram_spurious{ 0 }; ///< count of picoquic callback for late/delayed dgram acks
        uint64_t tx_dgram_drops{ 0 };    ///< count of drops due to data context missing

#if __cplusplus >= 202002L
        auto operator<=>(const QuicConnectionMetrics&) const = default;
#else
        constexpr bool operator==(const QuicConnectionMetrics& other) const
        {
            return this->cwin_congested == other.cwin_congested &&
                   this->prev_cwin_congested == other.prev_cwin_congested && this->tx_congested == other.tx_congested &&
                   this->tx_rate_bps == other.tx_rate_bps && this->rx_rate_bps == other.rx_rate_bps &&
                   this->tx_cwin_bytes == other.tx_cwin_bytes &&
                   this->tx_in_transit_bytes == other.tx_in_transit_bytes && this->rtt_us == other.rtt_us &&
                   this->srtt_us == other.srtt_us && this->tx_retransmits == other.tx_retransmits &&
                   this->tx_lost_pkts == other.tx_lost_pkts && this->tx_timer_losses == other.tx_timer_losses &&
                   this->tx_spurious_losses == other.tx_spurious_losses && this->rx_dgrams == other.rx_dgrams &&
                   this->rx_dgrams_bytes == other.rx_dgrams_bytes && this->tx_dgram_cb == other.tx_dgram_cb &&
                   this->tx_dgram_ack == other.tx_dgram_ack && this->tx_dgram_lost == other.tx_dgram_lost &&
                   this->tx_dgram_spurious == other.tx_dgram_spurious && this->tx_dgram_drops == other.tx_dgram_drops;
        }
        constexpr bool operator!=(const QuicConnectionMetrics& other) const { return !(*this == other); }
#endif

        /**
         * @brief Reset metrics for period
         */
        void ResetPeriod()
        {
            tx_rate_bps.Clear();
            rx_rate_bps.Clear();
            tx_cwin_bytes.Clear();
            tx_in_transit_bytes.Clear();
            rtt_us.Clear();
            srtt_us.Clear();
        }
    };

    struct QuicDataContextMetrics
    {
        uint64_t enqueued_objs{ 0 }; /// count of objects enqueued by the app to be transmitted

        uint64_t rx_stream_cb{ 0 };    /// count of callbacks to receive data
        uint64_t rx_stream_bytes{ 0 }; /// count of stream bytes received

        uint64_t tx_buffer_drops{ 0 };   /// Count of write buffer drops of data due to RESET request
        uint64_t tx_queue_discards{ 0 }; /// count of objects discarded due to TTL expiry or clear
        uint64_t tx_queue_expired{ 0 };  /// count of objects expired before pop/front

        uint64_t tx_delayed_callback{ 0 };      /// Count of times transmit callbacks were delayed
        uint64_t prev_tx_delayed_callback{ 0 }; /// Previous transmit delayed callback value, set each interval
        uint64_t tx_reset_wait{ 0 };            /// count of times data context performed a reset and wait
        MinMaxAvg tx_queue_size;                /// TX queue size in period
        MinMaxAvg tx_callback_ms;               /// Callback time in milliseconds in period
        MinMaxAvg tx_object_duration_us;        /// TX object time in queue duration in microseconds

        uint64_t tx_dgrams{ 0 };       /// count of datagrams sent
        uint64_t tx_dgrams_bytes{ 0 }; /// count of datagrams sent bytes

        uint64_t tx_stream_cb{ 0 };      /// count of stream callbacks to send data
        uint64_t tx_stream_objects{ 0 }; /// count of stream objects sent
        uint64_t tx_stream_bytes{ 0 };   /// count of stream bytes sent

#if __cplusplus >= 202002L
        constexpr auto operator<=>(const QuicDataContextMetrics&) const = default;
#else
        constexpr bool operator==(const QuicDataContextMetrics& other) const
        {
            return this->enqueued_objs == other.enqueued_objs && this->rx_stream_cb == other.rx_stream_cb &&
                   this->rx_stream_bytes == other.rx_stream_bytes && this->tx_buffer_drops == other.tx_buffer_drops &&
                   this->tx_queue_discards == other.tx_queue_discards &&
                   this->tx_queue_expired == other.tx_queue_expired &&
                   this->tx_delayed_callback == other.tx_delayed_callback &&
                   this->prev_tx_delayed_callback == other.prev_tx_delayed_callback &&
                   this->tx_reset_wait == other.tx_reset_wait && this->tx_queue_size == other.tx_queue_size &&
                   this->tx_callback_ms == other.tx_callback_ms &&
                   this->tx_object_duration_us == other.tx_object_duration_us && this->tx_dgrams == other.tx_dgrams &&
                   this->tx_dgrams_bytes == other.tx_dgrams_bytes && this->tx_stream_cb == other.tx_stream_cb &&
                   this->tx_stream_objects == other.tx_stream_objects && this->tx_stream_bytes == other.tx_stream_bytes;
        }
        constexpr bool operator!=(const QuicDataContextMetrics& other) const { return !(*this == other); }
#endif

        /**
         * @brief Reset metrics for period
         */
        void ResetPeriod()
        {
            tx_queue_size.Clear();
            tx_callback_ms.Clear();
            tx_object_duration_us.Clear();
        }
    };

    /// @cond
    /*
     * Custom UDP protocol metrics
     */
    struct UdpDataContextMetrics
    {
        uint64_t enqueued_objs{ 0 };

        uint64_t tx_queue_expired{ 0 }; /// count of objects expired before pop/front
        uint64_t tx_bytes{ 0 };         /// count of bytes sent
        uint64_t tx_objects{ 0 };       /// count of objects (messages) sent

        uint64_t rx_bytes{ 0 };   /// count of bytes received
        uint64_t rx_objects{ 0 }; /// count of objects received

#if __cplusplus >= 202002L
        constexpr auto operator<=>(const UdpDataContextMetrics&) const = default;
#else
        constexpr auto operator==(const UdpDataContextMetrics& other) const
        {
            return this->enqueued_objs == other.enqueued_objs && this->tx_queue_expired == other.tx_queue_expired &&
                   this->tx_bytes == other.tx_bytes && this->tx_objects == other.tx_objects &&
                   this->rx_bytes == other.rx_bytes && this->rx_objects == other.rx_objects;
        }
        constexpr auto operator!=(const UdpDataContextMetrics& other) const { return !(*this == other); }
#endif
    };

    struct UdpConnectionMetrics
    {
        uint64_t rx_no_context{ 0 }; /// count of times RX object data context doesn't exist

        uint64_t tx_no_context{ 0 };      /// count of times TX object data context doesn't exist
        uint64_t tx_discard_objects{ 0 }; /// count of discard objects sent
    };

    using TimeStampUs = std::chrono::time_point<std::chrono::steady_clock, std::chrono::microseconds>;

    struct MetricsConnSample
    {
        TimeStampUs sample_time;   /// Sample time
        uint64_t conn_ctx_id{ 0 }; /// Conn context ID
        std::optional<UdpConnectionMetrics> udp_sample;
        std::optional<QuicConnectionMetrics> quic_sample;

        MetricsConnSample()
          : sample_time(std::chrono::time_point_cast<std::chrono::microseconds>(std::chrono::steady_clock::now()))
        {
        }

        MetricsConnSample(const uint64_t conn_id, const UdpConnectionMetrics udp_sample)
          : sample_time(std::chrono::time_point_cast<std::chrono::microseconds>(std::chrono::steady_clock::now()))
          , conn_ctx_id(conn_id)
          , udp_sample(udp_sample)
        {
        }

        MetricsConnSample(const TimeStampUs sample_time, const uint64_t conn_id, const UdpConnectionMetrics udp_sample)
          : sample_time(sample_time)
          , conn_ctx_id(conn_id)
          , udp_sample(udp_sample)
        {
        }

        MetricsConnSample(const uint64_t conn_id, const QuicConnectionMetrics quic_sample)
          : sample_time(std::chrono::time_point_cast<std::chrono::microseconds>(std::chrono::steady_clock::now()))
          , conn_ctx_id(conn_id)
          , quic_sample(quic_sample)
        {
        }

        MetricsConnSample(const TimeStampUs sample_time,
                          const uint64_t conn_id,
                          const QuicConnectionMetrics quic_sample)
          : sample_time(sample_time)
          , conn_ctx_id(conn_id)
          , quic_sample(quic_sample)
        {
        }
    };

    struct MetricsDataSample
    {
        TimeStampUs sample_time;   /// Sample time
        uint64_t conn_ctx_id{ 0 }; /// Conn context ID
        uint64_t data_ctx_id{ 0 }; /// Data context ID
        std::optional<UdpDataContextMetrics> udp_sample;
        std::optional<QuicDataContextMetrics> quic_sample;

        MetricsDataSample()
          : sample_time(std::chrono::time_point_cast<std::chrono::microseconds>(std::chrono::steady_clock::now()))
        {
        }

        MetricsDataSample(const uint64_t conn_id, const uint64_t data_id, const UdpDataContextMetrics udp_sample)
          : sample_time(std::chrono::time_point_cast<std::chrono::microseconds>(std::chrono::steady_clock::now()))
          , conn_ctx_id(conn_id)
          , data_ctx_id(data_id)
          , udp_sample(udp_sample)
        {
        }

        MetricsDataSample(const TimeStampUs sample_time,
                          const uint64_t conn_id,
                          const uint64_t data_id,
                          const UdpDataContextMetrics udp_sample)
          : sample_time(sample_time)
          , conn_ctx_id(conn_id)
          , data_ctx_id(data_id)
          , udp_sample(udp_sample)
        {
        }

        MetricsDataSample(const uint64_t conn_id, const uint64_t data_id, const QuicDataContextMetrics quic_sample)
          : sample_time(std::chrono::time_point_cast<std::chrono::microseconds>(std::chrono::steady_clock::now()))
          , conn_ctx_id(conn_id)
          , data_ctx_id(data_id)
          , quic_sample(quic_sample)
        {
        }

        MetricsDataSample(const TimeStampUs sample_time,
                          const uint64_t conn_id,
                          const uint64_t data_id,
                          const QuicDataContextMetrics quic_sample)
          : sample_time(sample_time)
          , conn_ctx_id(conn_id)
          , data_ctx_id(data_id)
          , quic_sample(quic_sample)
        {
        }
    };

    constexpr uint64_t kMetricsIntervalUs = 5'000'000; /// Metrics interval for samples in microseconds
    constexpr size_t kMaxMetricsSamplesQueue = 500;    /// Max metric samples pending to be written

    /// @endcond
} // end namespace quicr
