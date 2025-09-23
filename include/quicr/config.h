// SPDX-FileCopyrightText: Copyright (c) 2024 Cisco Systems
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

#include "quicr/version.h"

#include <string>

namespace quicr {
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

    struct Config
    {
        std::string endpoint_id; ///< Endpoint ID for the client or server, should be unique
                                 ///< working to add to protocol: https://github.com/moq-wg/moq-transport/issues/461

        quicr::TransportConfig transport_config;
        uint64_t metrics_sample_ms{ 5000 };
    };

    struct ClientConfig : Config
    {
        std::string connect_uri; ///< URI such as moqt://relay[:port][/path?query]
        std::uint64_t tick_service_sleep_delay_us{ 333 };
    };

    struct ServerConfig : Config
    {

        std::string server_bind_ip; ///< IP address to bind to, can be 0.0.0.0 or ::
                                    ///< Empty will be treated as ANY
        uint16_t server_port;       ///< Listening port for server
        std::uint64_t tick_service_sleep_delay_us{ 333 };
    };

} // namespace moq
