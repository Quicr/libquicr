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

    for (auto& [data_ctx_id, data_ctx] : active_data_contexts) {
        delegate->OnDataMetricsStampled(sample_time, data_ctx_id, data_ctx.metrics);
        data_ctx.metrics.ResetPeriod();
    }

    metrics.ResetPeriod();
}
