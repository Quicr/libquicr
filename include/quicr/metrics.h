// SPDX-FileCopyrightText: Copyright (c) 2024 Cisco Systems
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

#include "quicr/containers/safe_queue.h"

#include <chrono>
#include <string>
#include <vector>

namespace quicr {
    /// @brief Running minimum, maximum, average, sum, and count for a sample period.
    struct MinMaxAvg
    {
        uint64_t min{ 0 }; ///< Minimum value observed in the period.
        uint64_t max{ 0 }; ///< Maximum value observed in the period.
        uint64_t avg{ 0 }; ///< Integer arithmetic mean for the period.

        uint64_t value_sum{ 0 };   ///< Sum of values observed in the period.
        uint64_t value_count{ 0 }; ///< Number of values observed in the period.

        /// @brief Compare all aggregate fields.
        /// @param other Aggregate to compare.
        /// @return Lexicographic comparison of the aggregate fields.
        constexpr auto operator<=>(const MinMaxAvg& other) const = default;

        /**
         * @brief  Add value to period
         *
         * @details Add value will update min/max/avg based on the value being added.
         *
         * @param value The value to add.
         */
        void AddValue(const uint64_t value)
        {
            min = value_count ? std::min(min, value) : value;
            max = std::max(max, value);

            value_sum += value;
            value_count++;

            if (!value_count)
                value_count = 1;

            avg = value_sum / value_count;
        }

        /**
         * @brief Fold another period's values into this one
         *
         * @details Used to report a quantity that several producers contribute to, without having
         *      them share a counter.
         *
         * @param other Aggregate values to add to this period.
         */
        void Merge(const MinMaxAvg& other)
        {
            if (!other.value_count) {
                return;
            }

            min = value_count ? std::min(min, other.min) : other.min;
            max = std::max(max, other.max);

            value_sum += other.value_sum;
            value_count += other.value_count;

            avg = value_sum / value_count;
        }

        /// @brief Reset every aggregate field to zero.
        void Clear()
        {
            min = 0;
            max = 0;
            avg = 0;
            value_sum = 0;
            value_count = 0;
        }
    };

    /// @brief QUIC connection counters and period aggregates.
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

        /// @brief Compare all metric fields.
        /// @param other Metrics to compare.
        /// @return Lexicographic comparison of the metric fields.
        auto operator<=>(const QuicConnectionMetrics& other) const = default;

        /// @brief Clear the rate, congestion-window, in-transit, and RTT period aggregates.
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

    /**
     * @brief Metrics produced by a single QUIC stream over one sample period
     *
     * @details Unlike the connection's, these are taken away from the stream at each sample rather
     *      than read from it, so every value describes the period alone and a consumer adds
     *      successive samples to build a total.
     */
    struct QuicStreamMetrics
    {
        uint64_t enqueued_objs{ 0 }; ///< Number of objects enqueued by the application for transmission.

        uint64_t rx_stream_cb{ 0 };    ///< Number of receive callbacks.
        uint64_t rx_stream_bytes{ 0 }; ///< Number of stream bytes received.

        uint64_t tx_buffer_drops{ 0 };   ///< Write-buffer drops caused by stream reset.
        uint64_t tx_queue_discards{ 0 }; ///< Objects discarded by queue clear or expiry.
        uint64_t tx_queue_expired{ 0 };  ///< Objects found expired when removed from the queue.

        uint64_t tx_delayed_callback{ 0 }; ///< Number of delayed transmit callbacks.
        MinMaxAvg tx_queue_size;           ///< Transmit queue size during the period.
        MinMaxAvg tx_callback_ms;          ///< Transmit callback duration in milliseconds.
        MinMaxAvg tx_object_duration_us;   ///< Time objects spent queued, in microseconds.

        uint64_t tx_stream_cb{ 0 };      ///< Number of stream-send callbacks.
        uint64_t tx_stream_objects{ 0 }; ///< Number of stream objects sent.
        uint64_t tx_stream_bytes{ 0 };   ///< Number of stream bytes sent.

        /// @brief Compare all metric fields.
        /// @param other Metrics to compare.
        /// @return Lexicographic comparison of the metric fields.
        constexpr auto operator<=>(const QuicStreamMetrics& other) const = default;

        /**
         * @brief Fold another stream's metrics into this one
         *
         * @details A flow may be carried by several streams at once, so its sample is the sum over
         *      the streams carrying it.
         *
         * @param other Period metrics to add to this object.
         */
        void Merge(const QuicStreamMetrics& other)
        {
            enqueued_objs += other.enqueued_objs;

            rx_stream_cb += other.rx_stream_cb;
            rx_stream_bytes += other.rx_stream_bytes;

            tx_buffer_drops += other.tx_buffer_drops;
            tx_queue_discards += other.tx_queue_discards;
            tx_queue_expired += other.tx_queue_expired;

            tx_delayed_callback += other.tx_delayed_callback;
            tx_queue_size.Merge(other.tx_queue_size);
            tx_callback_ms.Merge(other.tx_callback_ms);
            tx_object_duration_us.Merge(other.tx_object_duration_us);

            tx_stream_cb += other.tx_stream_cb;
            tx_stream_objects += other.tx_stream_objects;
            tx_stream_bytes += other.tx_stream_bytes;
        }
    };

    /**
     * @brief Metrics for a connection's datagram channel
     *
     * @details Datagrams share one queue per connection, so these are not attributable to a single
     *      track the way stream metrics are.
     */
    struct QuicDatagramMetrics
    {
        uint64_t enqueued_objs{ 0 };     ///< Number of objects enqueued by the application.
        uint64_t tx_queue_discards{ 0 }; ///< Objects discarded by queue clear or expiry.
        uint64_t tx_queue_expired{ 0 };  ///< Objects found expired when removed from the queue.
        MinMaxAvg tx_object_duration_us; ///< Time objects spent queued, in microseconds.

        uint64_t tx_dgrams{ 0 };       ///< Number of datagrams sent.
        uint64_t tx_dgrams_bytes{ 0 }; ///< Number of datagram bytes sent.

        /// @brief Compare all metric fields.
        /// @param other Metrics to compare.
        /// @return Lexicographic comparison of the metric fields.
        constexpr auto operator<=>(const QuicDatagramMetrics& other) const = default;

        /// @brief Clear the object queue-duration aggregate for the next period.
        void ResetPeriod() { tx_object_duration_us.Clear(); }
    };

    /**
     * @brief One sample period's metrics for a connection and the streams on it
     *
     * @details Sampling takes the values away from the live counters rather than reading them in
     *      place, so the sample can be assembled on the thread that writes those counters and
     *      handed to whichever thread reports it.
     */
    struct QuicMetricsSample
    {
        /// A single stream's part of the period
        struct Stream
        {
            std::uint64_t stream_id; ///< QUIC stream identifier.

            QuicStreamMetrics metrics; ///< Metrics removed from the stream for this period.

            /// The stream closed during the period; nothing further will be reported for it
            bool is_final;
        };

        QuicConnectionMetrics connection; ///< Connection-level metrics for the sample period.

        std::vector<Stream> streams; ///< Metrics for streams active or closed during the sample period.
    };

    /// @cond

    struct UdpConnectionMetrics
    {
        uint64_t rx_no_context{ 0 }; ///< Received objects for which no receive context exists.

        uint64_t tx_no_context{ 0 };      ///< Transmit attempts for which no send context exists.
        uint64_t tx_discard_objects{ 0 }; ///< Discard-object messages sent.
    };

    /// @brief Wall-clock timestamp used by internal metrics samples.
    using MetricsTimeStamp = std::chrono::time_point<std::chrono::system_clock>;

    /// @brief Internal transport metrics sample associated with a connection.
    struct MetricsConnSample
    {
        MetricsTimeStamp sample_time; ///< Time at which the sample was taken.
        uint64_t conn_ctx_id{ 0 };    ///< Transport connection context identifier.

        /// @brief UDP metrics, when sampling a UDP transport.
        std::optional<UdpConnectionMetrics> udp_sample;

        /// @brief QUIC metrics, when sampling a QUIC transport.
        std::optional<QuicConnectionMetrics> quic_sample;

        /// @brief Construct an empty sample timestamped with the current system time.
        MetricsConnSample()
          : sample_time(std::chrono::system_clock::now())
        {
        }

        /// @brief Construct a current-time UDP sample.
        /// @param conn_id Connection context identifier.
        /// @param udp_sample UDP metrics to store.
        MetricsConnSample(const uint64_t conn_id, const UdpConnectionMetrics udp_sample)
          : sample_time(std::chrono::system_clock::now())
          , conn_ctx_id(conn_id)
          , udp_sample(udp_sample)
        {
        }

        /// @brief Construct a UDP sample with an explicit timestamp.
        /// @param sample_time Time at which the metrics were sampled.
        /// @param conn_id Connection context identifier.
        /// @param udp_sample UDP metrics to store.
        MetricsConnSample(const MetricsTimeStamp sample_time,
                          const uint64_t conn_id,
                          const UdpConnectionMetrics udp_sample)
          : sample_time(sample_time)
          , conn_ctx_id(conn_id)
          , udp_sample(udp_sample)
        {
        }

        /// @brief Construct a current-time QUIC sample.
        /// @param conn_id Connection context identifier.
        /// @param quic_sample QUIC metrics to store.
        MetricsConnSample(const uint64_t conn_id, const QuicConnectionMetrics quic_sample)
          : sample_time(std::chrono::system_clock::now())
          , conn_ctx_id(conn_id)
          , quic_sample(quic_sample)
        {
        }

        /// @brief Construct a QUIC sample with an explicit timestamp.
        /// @param sample_time Time at which the metrics were sampled.
        /// @param conn_id Connection context identifier.
        /// @param quic_sample QUIC metrics to store.
        MetricsConnSample(const MetricsTimeStamp sample_time,
                          const uint64_t conn_id,
                          const QuicConnectionMetrics quic_sample)
          : sample_time(sample_time)
          , conn_ctx_id(conn_id)
          , quic_sample(quic_sample)
        {
        }
    };

    constexpr size_t kMaxMetricsSamplesQueue = 500; ///< Maximum number of internal metric samples pending for output.

    /// @endcond

    using MetricsTimeStampUs = uint64_t; ///< Microseconds since the Unix epoch.

    /// @brief Application-facing metrics for one connection sample period.
    struct ConnectionMetrics
    {
        MetricsTimeStampUs last_sample_time; ///< Last sampled time in microseconds

        QuicConnectionMetrics quic; ///< QUIC connection metrics

        uint64_t rx_dgram_unknown_track_alias{ 0 }; ///< Received datagram with unknown track alias
        uint64_t rx_dgram_invalid_type{ 0 };        ///< Received datagram with invalid type of kObjectDatagram
        uint64_t rx_dgram_decode_failed{ 0 };       ///< Failed to decode datagram

        uint64_t rx_stream_buffer_error{ 0 };        ///< Stream buffer error that results in bad parsing
        uint64_t rx_stream_unknown_track_alias{ 0 }; ///< Received stream header with unknown track alias
        uint64_t rx_stream_invalid_type{ 0 };        ///< Invalid message type

        /// Invalid control stream message received. Should always be 0.
        uint64_t invalid_ctrl_stream_msg{ 0 };
    };

    /// @brief Application-facing receive metrics for one subscribed track.
    struct SubscribeTrackMetrics
    {
        MetricsTimeStampUs last_sample_time; ///< Last sampled time in microseconds

        uint64_t bytes_received{ 0 };   ///< sum of payload bytes received
        uint64_t objects_received{ 0 }; ///< count of objects received
    };

    /// @brief Application-facing transmit metrics for one published track.
    struct PublishTrackMetrics
    {
        MetricsTimeStampUs last_sample_time; ///< Last sampled time in microseconds

        uint64_t bytes_published{ 0 };   ///< sum of payload bytes published
        uint64_t objects_published{ 0 }; ///< count of objects published

        uint64_t objects_dropped_not_ok{ 0 }; ///< Objects dropped upon publish object call due to status not being OK

        /// @brief QUIC-specific period metrics for the published track.
        struct Quic
        {
            uint64_t tx_buffer_drops{ 0 };   ///< count of write buffer drops of data due to RESET request
            uint64_t tx_queue_discards{ 0 }; ///< count of objects discarded due clear and transition to new stream
            uint64_t tx_queue_expired{ 0 };  ///< count of objects expired before pop/front due to TTL expiry

            uint64_t tx_delayed_callback{ 0 }; ///< count of times transmit callbacks were delayed

            MinMaxAvg tx_queue_size;         ///< TX queue size in period
            MinMaxAvg tx_callback_ms;        ///< Callback time in milliseconds in period
            MinMaxAvg tx_object_duration_us; ///< TX object time in queue duration in microseconds
        } quic;                              ///< QUIC-specific metrics for the sample period.
    };

} // namespace moq
