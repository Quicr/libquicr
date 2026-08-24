// SPDX-FileCopyrightText: Copyright (c) 2026 Cisco Systems
// SPDX-License-Identifier: BSD-2-Clause

#include "msquic_connection.h"

#include <utility>

namespace quicr {

    MsQuicConnection::MsQuicConnection(const QUIC_API_TABLE* api, HQUIC handle)
      : Connection(reinterpret_cast<std::uint64_t>(handle), API::kNativeQuic)
      , api(api)
      , handle(handle)
      , datagram_rx_queue(std::make_shared<SafeQueue<std::shared_ptr<const std::vector<std::uint8_t>>>>())
    {
    }

    void MsQuicConnection::SampleMetrics(const MetricsTimeStamp& sample_time)
    {
        auto delegate = delegate_.lock();
        if (!delegate) {
            return;
        }

        QUIC_STATISTICS_V2 statistics{};
        bool statistics_available = false;
        {
            std::lock_guard lock(handle_mutex);
            if (const auto connection_handle = handle.load(); connection_handle != nullptr) {
                std::uint32_t statistics_size = QUIC_STATISTICS_V2_SIZE_4;
                statistics_available = QUIC_SUCCEEDED(
                  api->GetParam(connection_handle, QUIC_PARAM_CONN_STATISTICS_V2, &statistics_size, &statistics));
            }
        }

        {
            std::lock_guard lock(metrics_mutex);
            if (statistics_available) {
                quic_metrics.tx_cwin_bytes.AddValue(statistics.SendCongestionWindow);
                quic_metrics.rtt_us.AddValue(statistics.Rtt);
                quic_metrics.srtt_us.AddValue(statistics.Rtt);
                quic_metrics.tx_lost_pkts = statistics.SendSuspectedLostPackets;
                quic_metrics.tx_spurious_losses = statistics.SendSpuriousLostPackets;
                quic_metrics.tx_congested = statistics.SendCongestionCount;
            }

            delegate->OnConnectionMetricsSampled(sample_time, quic_metrics);
            quic_metrics.ResetPeriod();
        }

        for (const auto& data_context : GetDataContexts()) {
            std::lock_guard lock(data_context->metrics_mutex);
            delegate->OnDataMetricsStampled(sample_time, data_context, data_context->metrics);
            data_context->metrics.ResetPeriod();
        }
    }

    std::shared_ptr<MsQuicDataContext> MsQuicConnection::AddDataContext(const bool bidir, const bool reliable)
    {
        std::lock_guard lock(data_contexts_mutex_);
        const auto id = next_data_context_id_++;
        auto context = std::make_shared<MsQuicDataContext>(id, GetID(), bidir, reliable);
        data_contexts_.emplace(id, context);
        return context;
    }

    std::shared_ptr<MsQuicDataContext> MsQuicConnection::GetDataContext(const std::uint64_t data_context_id) const
    {
        std::lock_guard lock(data_contexts_mutex_);
        const auto it = data_contexts_.find(data_context_id);
        return it == data_contexts_.end() ? nullptr : it->second;
    }

    std::shared_ptr<MsQuicDataContext> MsQuicConnection::RemoveDataContext(const std::uint64_t data_context_id)
    {
        std::lock_guard lock(data_contexts_mutex_);
        const auto it = data_contexts_.find(data_context_id);
        if (it == data_contexts_.end()) {
            return nullptr;
        }
        auto context = std::move(it->second);
        data_contexts_.erase(it);
        return context;
    }

    std::vector<std::shared_ptr<MsQuicDataContext>> MsQuicConnection::GetDataContexts() const
    {
        std::lock_guard lock(data_contexts_mutex_);
        std::vector<std::shared_ptr<MsQuicDataContext>> contexts;
        contexts.reserve(data_contexts_.size());
        for (const auto& [_, context] : data_contexts_) {
            contexts.push_back(context);
        }
        return contexts;
    }

    void MsQuicConnection::AddStream(const std::shared_ptr<MsQuicStream>& stream)
    {
        std::lock_guard lock(streams_mutex_);
        streams_[stream->id] = stream;
    }

    void MsQuicConnection::RetainPendingStream(const std::shared_ptr<MsQuicStream>& stream)
    {
        std::lock_guard lock(streams_mutex_);
        pending_streams_[stream->native_handle] = stream;
    }

    bool MsQuicConnection::PromotePendingStream(const std::shared_ptr<MsQuicStream>& stream)
    {
        std::lock_guard lock(streams_mutex_);
        const auto pending = pending_streams_.find(stream->native_handle);
        if (pending == pending_streams_.end()) {
            return false;
        }
        streams_[stream->id] = stream;
        pending_streams_.erase(pending);
        return true;
    }

    void MsQuicConnection::ReleasePendingStream(const HQUIC stream_handle)
    {
        std::lock_guard lock(streams_mutex_);
        pending_streams_.erase(stream_handle);
    }

    HQUIC MsQuicConnection::TakeHandle()
    {
        std::lock_guard lock(handle_mutex);
        return handle.exchange(nullptr);
    }

    std::shared_ptr<MsQuicStream> MsQuicConnection::GetStream(const std::uint64_t stream_id) const
    {
        std::lock_guard lock(streams_mutex_);
        const auto it = streams_.find(stream_id);
        return it == streams_.end() ? nullptr : it->second;
    }

    void MsQuicConnection::RemoveStream(const std::uint64_t stream_id)
    {
        std::lock_guard lock(streams_mutex_);
        streams_.erase(stream_id);
    }

    std::vector<std::shared_ptr<MsQuicStream>> MsQuicConnection::GetStreams() const
    {
        std::lock_guard lock(streams_mutex_);
        std::vector<std::shared_ptr<MsQuicStream>> streams;
        streams.reserve(streams_.size());
        for (const auto& [_, stream] : streams_) {
            streams.push_back(stream);
        }
        return streams;
    }
}
