// SPDX-FileCopyrightText: Copyright (c) 2026 Cisco Systems
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

#include "data_context.h"
#include "quicr/connection.h"
#include "quicr/containers/safe_queue.h"
#include "quicr/metrics.h"
#include "quicr/transport.h"

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
    struct MsQuicDataContext;

    struct MsQuicStream : public std::enable_shared_from_this<MsQuicStream>
    {
        MsQuicTransport* transport{ nullptr };
        std::weak_ptr<MsQuicConnection> connection;
        std::weak_ptr<MsQuicDataContext> data_context;
        HQUIC handle{ nullptr };
        HQUIC native_handle{ nullptr };
        std::uint64_t id{ 0 };
        bool id_ready{ false };
        bool start_complete{ false };
        bool start_abandoned{ false };
        QUIC_STATUS start_status{ QUIC_STATUS_PENDING };
        bool is_bidir{ false };
        bool locally_created{ false };
        bool close_notified{ false };
        std::shared_ptr<StreamRxContext> rx_context{ std::make_shared<StreamRxContext>() };
        std::condition_variable start_cv;
        std::mutex mutex;
    };

    struct MsQuicDataContext : public DataContext
    {
        MsQuicDataContext(std::uint64_t id, std::uint64_t connection_id, bool bidir, bool reliable)
          : DataContext(id, connection_id, bidir)
          , reliable(reliable)
        {
        }

        bool reliable{ true };
        std::atomic<bool> delete_on_empty{ false };
        std::atomic<bool> delete_started{ false };
        std::atomic<std::size_t> outstanding_sends{ 0 };
        QuicDataContextMetrics metrics;
        std::map<std::uint64_t, std::shared_ptr<MsQuicStream>> streams;
        mutable std::mutex mutex;
        mutable std::mutex metrics_mutex;
    };

    class MsQuicConnection
      : public Connection
      , public std::enable_shared_from_this<MsQuicConnection>
    {
      public:
        MsQuicConnection(const QUIC_API_TABLE* api, HQUIC handle);
        ~MsQuicConnection() override = default;

        void SampleMetrics(const MetricsTimeStamp& sample_time) override;

        std::shared_ptr<MsQuicDataContext> AddDataContext(bool bidir, bool reliable);
        std::shared_ptr<MsQuicDataContext> GetDataContext(std::uint64_t data_context_id) const;
        std::shared_ptr<MsQuicDataContext> RemoveDataContext(std::uint64_t data_context_id);
        std::vector<std::shared_ptr<MsQuicDataContext>> GetDataContexts() const;

        void AddStream(const std::shared_ptr<MsQuicStream>& stream);
        void RetainPendingStream(const std::shared_ptr<MsQuicStream>& stream);
        bool PromotePendingStream(const std::shared_ptr<MsQuicStream>& stream);
        void ReleasePendingStream(HQUIC stream_handle);
        std::shared_ptr<MsQuicStream> GetStream(std::uint64_t stream_id) const;
        void RemoveStream(std::uint64_t stream_id);
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
        mutable std::mutex handle_mutex;
        mutable std::mutex metrics_mutex;

      private:
        std::map<std::uint64_t, std::shared_ptr<MsQuicDataContext>> data_contexts_;
        std::map<std::uint64_t, std::shared_ptr<MsQuicStream>> streams_;
        std::map<HQUIC, std::shared_ptr<MsQuicStream>> pending_streams_;
        std::uint64_t next_data_context_id_{ 1 };
        mutable std::mutex data_contexts_mutex_;
        mutable std::mutex streams_mutex_;
    };
}
