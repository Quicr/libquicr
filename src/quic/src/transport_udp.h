
#pragma once

#include <cassert>
#include <cstdint>
#include <vector>
#include <map>
#include <mutex>
#include <array>
#include <queue>
#include <string>
#include <thread>

#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>

#include <transport/transport.h>
#include <spdlog/spdlog.h>

#include "transport_udp_protocol.h"
#include "transport/priority_queue.h"
#include "transport/safe_queue.h"
#include "transport/transport_metrics.h"

namespace qtransport {
    constexpr size_t kUdpMaxPacketSize = 64000;
    constexpr size_t kUdpMinKbps = 62;                /// Minimum KB bytes per second 62 = 500Kbps

    struct AddrId {
        uint64_t ip_hi;
        uint64_t ip_lo;
        uint16_t port;

        AddrId() {
            ip_hi = 0;
            ip_lo = 0;
            port = 0;
        }

        bool operator==(const AddrId &o) const {
            return ip_hi == o.ip_hi && ip_lo == o.ip_lo && port == o.port;
        }

        bool operator<(const AddrId &o) const {
            return std::tie(ip_hi, ip_lo, port) < std::tie(o.ip_hi, o.ip_lo, o.port);
        }
    };

    class UDPTransport : public ITransport {
    public:
        UDPTransport(const TransportRemote &server,
                     const TransportConfig &tcfg,
                     TransportDelegate &delegate,
                     bool is_server_mode,
                     std::shared_ptr<spdlog::logger> logger);

        virtual ~UDPTransport();

        TransportStatus Status() const override;

      TransportConnId Start(std::shared_ptr<SafeQueue<MetricsConnSample>> metrics_conn_samples,
                            std::shared_ptr<SafeQueue<MetricsDataSample>> metrics_data_samples) override;

        void Close(const TransportConnId &conn_id, uint64_t app_reason_code=0) override;

        virtual bool GetPeerAddrInfo(const TransportConnId &conn_id,
                                     sockaddr_storage *addr) override;

        DataContextId CreateDataContext(TransportConnId conn_id,
                                        bool use_reliable_transport,
                                        uint8_t priority, bool bidir) override;

        void DeleteDataContext(const TransportConnId &conn_id, DataContextId data_ctx_id) override;

        TransportError Enqueue(const TransportConnId &conn_id,
                               const DataContextId &data_ctx_id,
                               std::vector<uint8_t> &&bytes,
                               std::vector<qtransport::MethodTraceItem> &&trace,
                               uint8_t priority,
                               uint32_t ttl_ms,
                               uint32_t delay_ms,
                               EnqueueFlags flags) override;

        std::optional<std::vector<uint8_t>> Dequeue(TransportConnId conn_id,
                                                    std::optional<DataContextId> data_ctx_id) override;

        std::shared_ptr<StreamBuffer<uint8_t>> GetStreamBuffer(TransportConnId conn_id, uint64_t stream_id) override { return nullptr;}

        void SetRemoteDataCtxId(TransportConnId conn_id,
                                DataContextId data_ctx_id,
                                DataContextId remote_data_ctx_id) override;

        void SetStreamIdDataCtxId([[maybe_unused]] const TransportConnId conn_id,
                                  [[maybe_unused]] DataContextId data_ctx_id,
                                  [[maybe_unused]] uint64_t stream_id) override {}

        void SetDataCtxPriority([[maybe_unused]] const TransportConnId conn_id,
                                [[maybe_unused]] DataContextId data_ctx_id,
                                [[maybe_unused]] uint8_t priority) override {}

    private:
        TransportConnId ConnectClient();
        TransportConnId ConnectServer();

        TransportRemote CreateAddrRemote(const sockaddr_storage& addr);
        AddrId CreateAddrId(const sockaddr_storage& addr);



        /* Threads */
        void FdReader();
        void FdWriter();

        std::atomic<bool> stop_;
        std::vector<std::thread> running_threads_;

        struct Addr {
            socklen_t addr_len;
            struct sockaddr_storage addr {0};
            AddrId id;
            bool is_ipv6 { false };

            Addr() {
                addr_len = sizeof(addr);
            }
        };

        struct DataContext {
            DataContextId data_ctx_id{0};
            uint8_t priority {10};

            DataContextId remote_data_ctx_id {0};              /// Remote data context ID to use for this context
            UintVT remote_data_ctx_id_v {0};                  /// Remote data context ID as variable length integer

            UdpDataContextMetrics metrics;

            uint64_t in_data_cb_skip_count {0};               /// Number of times callback was skipped due to size

            SafeQueue<ConnData> rx_data;                     /// Receive queue
        };

        struct ConnectionContext {
            Addr addr;
            TransportConnId id {0};                     // This/conn ID
            DataContextId next_data_ctx_id {0};
            std::map<DataContextId, DataContext> data_contexts;

            UdpConnectionMetrics metrics;

            TransportStatus status { TransportStatus::kDisconnected };
            std::unique_ptr<PriorityQueue<ConnData>> tx_data;  // TX priority queue

            uint64_t last_rx_msg_tick { 0 };            /// Tick value (ms) when last message was received
            uint64_t last_tx_msg_tick { 0 };            /// Tick value (ms) when last message was sent
            uint16_t last_rx_hdr_tick { 0 };            /// Last received tick from data/keepalive header

            /*
             * Received/negotiated config parameters
             */
            uint32_t idle_timeout_ms { 120'000 };       /// Idle timeout in milliseconds
            uint32_t ka_interval_ms { 40'000 };         /// Interval in ms for when to send a keepalive (1/3 of idle_timeout)

            /*
             * Report variables
             */
            uint16_t tx_report_ott {0};                 // Last received report one-way trip time to receiver (as seen by receiver)
            uint16_t rx_report_ott {0};                 // Last RX OTT based on received data from receiver

            uint64_t tx_zero_loss_count {0};            // Consecutive count of reports with ZERO packet loss

            uint16_t tx_report_id {0};                  // Report ID increments on interval. Wrap is okay
            uint16_t tx_report_interval_ms { 100 };     // Report ID interval in milliseconds
            uint64_t tx_report_start_tick { 0 };        // Tick value on report change (new report interval)
            uint64_t tx_next_report_tick {0};           // Tick value to start a new report ID

            udp_protocol::ReportMessage report;          // Report to be sent back to sender upon received tx_report_id change
            uint64_t report_rx_start_tick { 0 };        // Tick value at start of the RX report interval

            udp_protocol::ReportMetrics tx_report_metrics;
            std::array<udp_protocol::ReportMessage, 5> tx_prev_reports;

            /*
             * Shaping variables
             */
            uint64_t wait_for_tick {0};
            uint64_t running_wait_us {0};   // Running wait time in microseconds - When more than 1ms, the wait for tick will be updated

            double bytes_per_us {6.4};     // Default to 50Mbps

            bool SetKBps(uint32_t k_bps, bool max_of= false) {
                if (k_bps < kUdpMinKbps) return false;

                const auto bp_us = (k_bps * 1024) / 1'000'000.0 /* 1 second double value */;
                if (!max_of || bp_us > bytes_per_us) {
                    bytes_per_us = bp_us;
                    return true;
                }

                return false;
            }
        };

        /* Protocol methods */
        /**
         * @brief Send UDP protocol connect message
         *
         * @param conn_id       Connection context ID
         * @param addr          Address to send the message to
         *
         * @return True if sent, false if not sent/error
         */
        bool SendConnect(TransportConnId conn_id, const Addr& addr);

        /**
         * @brief Send UDP protocol connect OK message
         *
         * @param conn_id       Connection context ID
         * @param addr          Address to send the message to
         *
         * @return True if sent, false if not sent/error
         */
        bool SendConnectOk(TransportConnId conn_id, const Addr& addr);

        /**
         * @brief Send UDP protocol disconnect message
         *
         * @param conn_id       Connection context ID
         * @param addr          Address to send the message to
         *
         * @return True if sent, false if not sent/error
         */
        bool SendDisconnect(TransportConnId conn_id, const Addr& addr);

        /**
         * @brief Send UDP protocol keepalive message
         *
         * @param conn[in,out]      Connection context reference, will be updated
         *
         * @return True if sent, false if not sent/error
         */
        bool SendKeepalive(ConnectionContext& conn);

        /**
         * @brief Send UDP protocol data message
         *
         * @notes: REQUIRES locking since the connection context will be updated
         *
         * @param conn[in,out]      Connection context reference, will be updated
         * @param data_ctx[in,out]  Data context reference, will be updated
         * @param cd[in]            Connection data to send
         * @param discard[in]       True if data should be discarded on receive
         *
         * @return True if sent, false if not sent/error
         */
        bool SendData(ConnectionContext& conn, DataContext& data_ctx, const ConnData& cd, bool discard=false);

        /**
         * @brief Send UDP protocol report message
         *
         * @notes: REQUIRES locking since the connection context will be updated
         *
         * @param conn[in,out]      Connection context reference, will be updated
         *
         * @return True if sent, false if not sent/error
         */
        bool SendReport(ConnectionContext& conn);

        std::shared_ptr<spdlog::logger> logger_;
        int fd_; // UDP socket
        bool isServerMode_;

        std::atomic<TransportStatus> clientStatus_ {TransportStatus::kDisconnected };

        TransportRemote serverInfo_;
        Addr serverAddr_;
        TransportConfig tconfig_;

        TransportDelegate &delegate_;
        std::mutex writer_mutex_;                              /// Mutex for writer
        std::mutex reader_mutex_;                              /// Mutex for reader

        TransportConnId last_conn_id_{0};
        std::map<TransportConnId, std::shared_ptr<ConnectionContext>> conn_contexts_;
        std::map<AddrId, std::shared_ptr<ConnectionContext>> addr_conn_contexts_;

        std::shared_ptr<TickService> tick_service_;
    };

} // namespace qtransport
