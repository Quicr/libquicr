// SPDX-FileCopyrightText: Copyright (c) 2024 Cisco Systems
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

#include "quicr/version.h"

#include <cstdint>
#include <string>

namespace quicr {
    /// Supported congestion control algorithms.
    enum class CongestionControl : std::uint8_t
    {
        kBbr,     ///< Bottleneck Bandwidth and Round-trip propagation time (BBR).
        kC4,      ///< Picoquic C4 congestion controller.
        kCubic,   ///< CUBIC congestion controller.
        kNewReno, ///< NewReno congestion controller.
    };

    /**
     * @brief Configuration shared by client and server QUIC transports.
     */
    struct TransportConfig
    {
        /// @brief Path to the external PEM certificate chain used by a server.
        ///
        /// The certificate is loaded from this file; certificate material is not stored in this configuration
        /// object. Verify its validity period, issuer, signature algorithm, and public-key strength before use.
        std::string tls_cert_filename;

        /// @brief Path to the external private-key file corresponding to @ref tls_cert_filename.
        ///
        /// Protect this file with restrictive permissions and keep it outside source control.
        std::string tls_key_filename;

        uint32_t time_queue_init_queue_size{ 1000 }; ///< Initial number of time-queue entries to reserve.
        uint32_t time_queue_max_duration{ 2000 };    ///< Maximum time-queue duration in milliseconds.
        uint32_t time_queue_bucket_interval{ 500 };  ///< Time-queue bucket width in milliseconds.
        uint32_t time_queue_rx_size{ 1000 };         ///< Maximum receive time-queue size.
        bool debug{ false };                         ///< Whether to enable transport debug processing and logging.
        uint64_t quic_cwin_minimum{ 131072 };        ///< Minimum QUIC congestion window in bytes.
        uint32_t quic_wifi_shadow_rtt_us{ 20000 };   ///< Wi-Fi shadow round-trip time in microseconds.
        uint64_t idle_timeout_ms{ 30000 }; ///< Connection idle timeout and client startup wait in milliseconds.
        CongestionControl congestion_control{ CongestionControl::kBbr }; ///< Congestion controller to use.
        std::string quic_qlog_path;       ///< Path passed to picoquic for qlog output; empty disables qlog.
        uint8_t quic_priority_limit{ 0 }; ///< Lowest numeric priority exempted from picoquic pacing/CC bypass.
        std::size_t max_connections{ 1 }; ///< Maximum active connections per QUIC instance.

        /// @brief Whether to log TLS traffic secrets for QUIC connections.
        ///
        /// Enabling this permits captured traffic to be decrypted. Use only for controlled diagnostics, protect
        /// the resulting key log as secret material, and delete it when no longer needed.
        bool ssl_keylog{ false };

        std::size_t socket_buffer_size{ 1'000'000 }; ///< Requested UDP socket buffer size in bytes.
        uint32_t callback_queue_size{ 2000 };        ///< Maximum queued transport callbacks before notifications drop.
        uint64_t metrics_sample_ms{ 5000 };          ///< Metrics sampling interval in milliseconds.
        uint64_t initial_max_stream_data{ 0 }; ///< Initial per-stream receive window; zero uses picoquic's default.
        uint64_t initial_max_stream_id{ 512 }; ///< Initial bidirectional and unidirectional stream limit.
        std::size_t quic_shards{ 1 };          ///< Number of picoquic instances used in server mode.
        bool use_af_xdp{ true }; ///< Whether to use AF_XDP transmit when available, falling back to sendmsg.
    };

    /// @brief Configuration common to client-mode and server-mode sessions.
    struct Config
    {
        std::string endpoint_id; ///< Endpoint identifier advertised in SETUP; should identify this endpoint uniquely.

        quicr::TransportConfig transport_config;   ///< Underlying QUIC transport configuration.
        uint32_t unknown_stream_expiry_ms{ 3000 }; ///< Time to retain an unclassified stream, in milliseconds.
    };

    /// @brief Configuration for an outbound client session.
    struct ClientConfig : Config
    {
        std::string connect_uri;                          ///< Destination URI using `moq://`, `moqt://`, or `https://`.
        std::uint64_t tick_service_sleep_delay_us{ 333 }; ///< Default tick-service sleep delay in microseconds.
    };

    /// @brief Configuration for a listening server transport.
    struct ServerConfig : Config
    {
        std::string server_bind_ip; ///< Local IPv4/IPv6 address; empty binds to the wildcard address.
        uint16_t server_port;       ///< Local UDP port on which to listen.
        std::uint64_t tick_service_sleep_delay_us{ 333 }; ///< Default tick-service sleep delay in microseconds.
    };

} // namespace moq
