// SPDX-FileCopyrightText: Copyright (c) 2026 Cisco Systems
// SPDX-License-Identifier: BSD-2-Clause

#include "picoquic_connection.h"

#include "quicr/metrics.h"
#include "quicr/session.h"

#include <utility>

namespace {
    /// Ceiling on streams whose last metrics are held waiting for a sample that may never come
    constexpr std::size_t kMaxUnreportedStreams = 1024;
}

quicr::QuicMetricsSample
quicr::PicoQuicConnection::TakeMetricsSample()
{
    QuicMetricsSample sample;

    {
        std::lock_guard _(stream_mutex_);
        sample.streams = std::exchange(unreported_stream_metrics_, {});
    }

    // Snapshot the container so the streams' own lock is not held while their metrics are taken.
    for (const auto& stream : GetStreams()) {
        sample.streams.push_back({ stream->GetStreamId(), std::exchange(stream->metrics, {}), false });
    }

    // The connection's counters are reported as running totals rather than accumulated, so only
    // its period values are reset.
    sample.connection = metrics;
    metrics.ResetPeriod();
    dgram_metrics.ResetPeriod();

    return sample;
}

void
quicr::PicoQuicConnection::ReportMetricsSample(const MetricsTimeStamp& sample_time, const QuicMetricsSample& sample)
{
    auto delegate = GetDelegate();
    if (!delegate) {
        return;
    }

    // Report the streams first: the connection sample is what tells the delegate the period is
    // complete, so anything a track accumulates from its streams has to be in by then.
    for (const auto& stream : sample.streams) {
        delegate->OnStreamMetricsStampled(sample_time, stream.stream_id, stream.metrics, stream.is_final);
    }

    delegate->OnConnectionMetricsSampled(sample_time, sample.connection);
}

quicr::PicoQuicStream::PicoQuicStream(const std::uint64_t stream_id,
                                      const std::uint64_t conn_id,
                                      std::unique_ptr<SafeTimeQueue<ConnData>> tx_queue)
  : Stream(stream_id, conn_id)
  , tx_data(std::move(tx_queue))
{
}

std::shared_ptr<quicr::PicoQuicStream>
quicr::PicoQuicConnection::GetStream(const std::uint64_t stream_id) const
{
    std::lock_guard _(stream_mutex_);

    const auto it = streams_.find(stream_id);
    if (it == streams_.end()) {
        return nullptr;
    }

    return it->second;
}

std::shared_ptr<quicr::PicoQuicStream>
quicr::PicoQuicConnection::AddStream(const std::uint64_t stream_id, std::unique_ptr<SafeTimeQueue<ConnData>> tx_queue)
{
    auto stream = std::make_shared<PicoQuicStream>(stream_id, GetID(), std::move(tx_queue));

    std::lock_guard _(stream_mutex_);
    streams_[stream_id] = stream;

    return stream;
}

std::shared_ptr<quicr::PicoQuicStream>
quicr::PicoQuicConnection::GetOrAddStream(const std::uint64_t stream_id,
                                          std::unique_ptr<SafeTimeQueue<ConnData>> tx_queue)
{
    std::lock_guard _(stream_mutex_);

    const auto it = streams_.find(stream_id);
    if (it != streams_.end()) {
        return it->second;
    }

    auto stream = std::make_shared<PicoQuicStream>(stream_id, GetID(), std::move(tx_queue));
    streams_.emplace(stream_id, stream);

    return stream;
}

std::shared_ptr<quicr::PicoQuicStream>
quicr::PicoQuicConnection::RemoveStream(const std::uint64_t stream_id)
{
    std::shared_ptr<PicoQuicStream> stream;

    {
        std::lock_guard _(stream_mutex_);

        const auto it = streams_.find(stream_id);
        if (it == streams_.end()) {
            return nullptr;
        }

        stream = std::move(it->second);
        streams_.erase(it);

        // Bounded because nothing drains this if samples stop being taken, which happens when the
        // callback queue is under sustained backpressure.
        if (unreported_stream_metrics_.size() < kMaxUnreportedStreams) {
            unreported_stream_metrics_.push_back({ stream_id, stream->metrics, true });
        }
    }

    stream->MarkClosed();

    return stream;
}

std::vector<std::shared_ptr<quicr::PicoQuicStream>>
quicr::PicoQuicConnection::GetStreams() const
{
    std::lock_guard _(stream_mutex_);

    std::vector<std::shared_ptr<PicoQuicStream>> streams;
    streams.reserve(streams_.size());

    for (const auto& [_, stream] : streams_) {
        streams.push_back(stream);
    }

    return streams;
}
