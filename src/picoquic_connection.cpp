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

    delegate->OnConnectionMetricsSampled(sample_time, metrics);

    // Snapshot the contexts so the delegate is not called while holding the data context lock.
    for (const auto& data_ctx : GetDataContexts()) {
        delegate->OnDataMetricsStampled(sample_time, data_ctx->data_ctx_id, data_ctx->metrics);
        data_ctx->metrics.ResetPeriod();
    }

    metrics.ResetPeriod();
}

std::shared_ptr<quicr::DataContext>
quicr::PicoQuicConnection::GetDataContext(const std::uint64_t data_ctx_id) const
{
    std::lock_guard _(data_ctx_mutex_);

    const auto it = active_data_contexts_.find(data_ctx_id);
    if (it == active_data_contexts_.end()) {
        return nullptr;
    }

    return it->second;
}

std::shared_ptr<quicr::DataContext>
quicr::PicoQuicConnection::AddDataContext(const bool bidir, const bool uses_reset_wait)
{
    auto data_ctx = std::make_shared<DataContext>();
    data_ctx->conn_id = GetID();
    data_ctx->is_bidir = bidir;
    data_ctx->uses_reset_wait = uses_reset_wait;

    std::lock_guard _(data_ctx_mutex_);

    data_ctx->data_ctx_id = next_data_ctx_id_++;
    active_data_contexts_.emplace(data_ctx->data_ctx_id, data_ctx);

    return data_ctx;
}

std::shared_ptr<quicr::DataContext>
quicr::PicoQuicConnection::RemoveDataContext(const std::uint64_t data_ctx_id)
{
    std::lock_guard _(data_ctx_mutex_);

    const auto it = active_data_contexts_.find(data_ctx_id);
    if (it == active_data_contexts_.end()) {
        return nullptr;
    }

    auto data_ctx = std::move(it->second);
    active_data_contexts_.erase(it);

    return data_ctx;
}

std::vector<std::shared_ptr<quicr::DataContext>>
quicr::PicoQuicConnection::GetDataContexts() const
{
    std::lock_guard _(data_ctx_mutex_);

    std::vector<std::shared_ptr<DataContext>> data_ctxs;
    data_ctxs.reserve(active_data_contexts_.size());

    for (const auto& [_, data_ctx] : active_data_contexts_) {
        data_ctxs.push_back(data_ctx);
    }

    return data_ctxs;
}
