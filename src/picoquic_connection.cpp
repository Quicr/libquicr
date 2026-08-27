// SPDX-FileCopyrightText: Copyright (c) 2026 Cisco Systems
// SPDX-License-Identifier: BSD-2-Clause

#include "picoquic_connection.h"

#include "quicr/metrics.h"
#include "quicr/session.h"

void
quicr::PicoQuicConnection::SampleMetrics(const MetricsTimeStamp& sample_time)
{
    auto delegate = delegate_.lock();
    if (!delegate) {
        return;
    }

    // Report the streams first: the connection sample is what tells the delegate the period is
    // complete, so anything a track accumulates from its streams has to be in by then. Snapshot the
    // container so the delegate is not called while holding the stream lock.
    for (const auto& stream : GetStreams()) {
        delegate->OnStreamMetricsStampled(sample_time, stream->GetStreamId(), stream->metrics);
        stream->metrics.ResetPeriod();
    }

    delegate->OnConnectionMetricsSampled(sample_time, metrics);

    dgram_metrics.ResetPeriod();
    metrics.ResetPeriod();
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
