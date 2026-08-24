// SPDX-FileCopyrightText: Copyright (c) 2026 Cisco Systems
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

#include "quicr/connection.h"
#include "quicr/containers/safe_queue.h"
#include "quicr/metrics.h"
#include "quicr/transport.h"
#include "stream.h"

#include <msquic.h>

#include <atomic>
#include <condition_variable>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <vector>

namespace quicr {

    class MsQuicTransport;
    class MsQuicConnection;
    class MsQuicStream : public Stream
    {
      public:
        MsQuicStream(std::uint64_t stream_id, std::uint64_t connection_id)
          : Stream(stream_id, connection_id)
        {
        }

        std::shared_ptr<MsQuicStream> SharedFromThis()
        {
            return std::static_pointer_cast<MsQuicStream>(shared_from_this());
        }

        void SetAssignedId(std::uint64_t stream_id) noexcept { SetStreamId(stream_id); }

        MsQuicTransport* transport{ nullptr };
        std::weak_ptr<MsQuicConnection> connection;
        HQUIC handle{ nullptr };
        HQUIC native_handle{ nullptr };
        bool id_ready{ false };
        bool start_complete{ false };
        bool start_abandoned{ false };
        QUIC_STATUS start_status{ QUIC_STATUS_PENDING };
        bool is_bidir{ false };
        bool locally_created{ false };
        bool close_notified{ false };
        std::shared_ptr<StreamRxContext> rx_context{ std::make_shared<StreamRxContext>() };
        std::atomic<std::size_t> outstanding_sends{ 0 };
        QuicStreamMetrics metrics;
        std::condition_variable start_cv;
        std::mutex mutex;
        mutable std::mutex metrics_mutex;
    };

    class MsQuicConnection
      : public Connection
      , public std::enable_shared_from_this<MsQuicConnection>
    {
      public:
        MsQuicConnection(const QUIC_API_TABLE* api, HQUIC handle);
        ~MsQuicConnection() override = default;

        QuicMetricsSample TakeMetricsSample() override;
        void ReportMetricsSample(const MetricsTimeStamp& sample_time, const QuicMetricsSample& sample) override;

        void AddStream(const std::shared_ptr<MsQuicStream>& stream);
        void RetainPendingStream(const std::shared_ptr<MsQuicStream>& stream);
        bool PromotePendingStream(const std::shared_ptr<MsQuicStream>& stream);
        void ReleasePendingStream(HQUIC stream_handle);
        std::shared_ptr<MsQuicStream> GetStream(std::uint64_t stream_id) const;
        std::shared_ptr<MsQuicStream> RemoveStream(std::uint64_t stream_id);
        std::vector<std::shared_ptr<MsQuicStream>> GetStreams() const;

        const QUIC_API_TABLE* api{ nullptr };
        MsQuicTransport* transport{ nullptr };
        HQUIC TakeHandle();

        std::atomic<HQUIC> handle{ nullptr };
        std::shared_ptr<SafeQueue<std::shared_ptr<const std::vector<std::uint8_t>>>> datagram_rx_queue;
        std::atomic<bool> datagram_send_enabled{ false };
        std::atomic<std::uint16_t> max_datagram_send_length{ 0 };
        sockaddr_storage peer_address{};
        bool peer_address_valid{ false };
        QuicConnectionMetrics quic_metrics;
        QuicDatagramMetrics datagram_metrics;
        mutable std::mutex handle_mutex;
        mutable std::mutex metrics_mutex;

      private:
        std::map<std::uint64_t, std::shared_ptr<MsQuicStream>> streams_;
        std::map<HQUIC, std::shared_ptr<MsQuicStream>> pending_streams_;
        std::vector<QuicMetricsSample::Stream> unreported_stream_metrics_;
        mutable std::mutex streams_mutex_;
    };
}
