// SPDX-FileCopyrightText: Copyright (c) 2024 Cisco Systems
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

#include "detail/quic_transport_metrics.h"
#include <string>

namespace moq {
    using namespace qtransport;

    struct ConnectionMetrics
    {
        struct Quic
        {
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
        } quic;
    };

    struct SubscribeTrackMetrics
    {
        uint64_t bytes_received{ 0 };   ///< sum of payload bytes received
        uint64_t objects_received{ 0 }; ///< count of objects received
    };

    struct PublishTrackMetrics
    {
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
