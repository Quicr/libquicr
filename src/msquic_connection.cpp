// SPDX-FileCopyrightText: Copyright (c) 2026 Cisco Systems
// SPDX-License-Identifier: BSD-2-Clause

#include "msquic_connection.h"

#include <algorithm>
#include <utility>

namespace quicr {

    namespace {
        constexpr std::size_t kMaxUnreportedStreams = 1024;
    }

    MsQuicConnection::MsQuicConnection(const QUIC_API_TABLE* api, HQUIC handle)
      : Connection(reinterpret_cast<std::uint64_t>(handle), API::kNativeQuic)
      , api(api)
      , handle(handle)
      , datagram_rx_queue(std::make_shared<SafeQueue<std::shared_ptr<const std::vector<std::uint8_t>>>>())
    {
    }

    QuicMetricsSample MsQuicConnection::TakeMetricsSample()
    {
        QuicMetricsSample sample;

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

            sample.connection = quic_metrics;
            quic_metrics.ResetPeriod();
            datagram_metrics.ResetPeriod();
        }

        {
            std::lock_guard lock(streams_mutex_);
            sample.streams = std::exchange(unreported_stream_metrics_, {});
        }

        for (const auto& stream : GetStreams()) {
            std::lock_guard lock(stream->metrics_mutex);
            sample.streams.push_back({ stream->GetStreamId(), std::exchange(stream->metrics, {}), false });
        }

        return sample;
    }

    void MsQuicConnection::ReportMetricsSample(const MetricsTimeStamp& sample_time, const QuicMetricsSample& sample)
    {
        auto delegate = GetDelegate();
        if (!delegate) {
            return;
        }

        for (const auto& stream : sample.streams) {
            delegate->OnStreamMetricsStampled(sample_time, stream.stream_id, stream.metrics, stream.is_final);
        }
        delegate->OnConnectionMetricsSampled(sample_time, sample.connection);
    }

    void MsQuicConnection::AddStream(const std::shared_ptr<MsQuicStream>& stream)
    {
        std::lock_guard lock(streams_mutex_);
        streams_[stream->GetStreamId()] = stream;
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
        streams_[stream->GetStreamId()] = stream;
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

    std::shared_ptr<MsQuicStream> MsQuicConnection::RemoveStream(const std::uint64_t stream_id)
    {
        std::shared_ptr<MsQuicStream> stream;
        {
            std::lock_guard lock(streams_mutex_);
            const auto it = streams_.find(stream_id);
            if (it == streams_.end()) {
                return nullptr;
            }
            stream = std::move(it->second);
            streams_.erase(it);

            if (unreported_stream_metrics_.size() < kMaxUnreportedStreams) {
                std::lock_guard metrics_lock(stream->metrics_mutex);
                unreported_stream_metrics_.push_back({ stream_id, std::exchange(stream->metrics, {}), true });
            }
        }
        stream->MarkClosed();
        return stream;
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
