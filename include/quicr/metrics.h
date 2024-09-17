// SPDX-FileCopyrightText: Copyright (c) 2024 Cisco Systems
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

#include "detail/quic_transport_metrics.h"
#include <string>

namespace quicr {
    using namespace quicr;
    using MetricsTimeStampUs = std::chrono::time_point<std::chrono::steady_clock, std::chrono::microseconds>;

    struct ConnectionMetrics
    {
        MetricsTimeStampUs last_sample_time; ///< Last sampled time in microseconds

        QuicConnectionMetrics quic; ///< QUIC connection metrics

        uint64_t rx_dgram_unknown_subscribe_id{ 0 }; ///< Received datagram with unknown subscribe ID
        uint64_t rx_dgram_invalid_type{ 0 };         ///< Received datagram with invalid type of OBJECT_DATAGRAM
        uint64_t rx_dgram_decode_failed{ 0 };        ///< Failed to decode datagram

        uint64_t rx_stream_buffer_error{ 0 };         ///< Stream buffer error that results in bad parsing
        uint64_t rx_stream_unknown_subscribe_id{ 0 }; ///< Received stream header with unknown subscribe ID
        uint64_t rx_stream_invalid_type{ 0 };         ///< Invalid message type
    };

    struct SubscribeTrackMetrics
    {
        MetricsTimeStampUs last_sample_time; ///< Last sampled time in microseconds

        uint64_t bytes_received{ 0 };   ///< sum of payload bytes received
        uint64_t objects_received{ 0 }; ///< count of objects received
    };

    struct PublishTrackMetrics
    {
        MetricsTimeStampUs last_sample_time; ///< Last sampled time in microseconds

        uint64_t bytes_published{ 0 };   ///< sum of payload bytes published
        uint64_t objects_published{ 0 }; ///< count of objects published

        struct Quic
        {
            uint64_t tx_buffer_drops{ 0 };   ///< count of write buffer drops of data due to RESET request
            uint64_t tx_queue_discards{ 0 }; ///< count of objects discarded due to TTL expiry or clear
            uint64_t tx_queue_expired{ 0 };  ///< count of objects expired before pop/front

            uint64_t tx_delayed_callback{ 0 }; ///< count of times transmit callbacks were delayed
            uint64_t tx_reset_wait{ 0 };       ///< count of times data context performed a reset and wait

            MinMaxAvg tx_queue_size;         ///< TX queue size in period
            MinMaxAvg tx_callback_ms;        ///< Callback time in milliseconds in period
            MinMaxAvg tx_object_duration_us; ///< TX object time in queue duration in microseconds
        } quic;
    };

} // namespace moq
