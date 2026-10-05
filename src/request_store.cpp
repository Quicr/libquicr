// SPDX-FileCopyrightText: Copyright (c) 2026 Cisco Systems
// SPDX-License-Identifier: BSD-2-Clause

#include "quicr/containers/request_store.h"

namespace quicr {

    void RequestStore::Register(std::uint64_t request_id,
                                std::uint64_t stream_id,
                                std::shared_ptr<TrackHandler> handler)
    {
        std::lock_guard _(mutex_);

        if (stream_by_request_id_.contains(request_id)) {
            throw std::invalid_argument("Request already exists");
        }

        const auto [request_it, inserted] =
          requests_by_stream_.try_emplace(stream_id, RequestState{ request_id, stream_id, std::move(handler) });
        if (!inserted) {
            throw std::invalid_argument("Request already exists");
        }

        try {
            stream_by_request_id_.emplace(request_id, stream_id);
        } catch (...) {
            requests_by_stream_.erase(request_it);
            throw;
        }
    }

    std::optional<RequestStore::RequestState> RequestStore::FindByStream(std::uint64_t stream_id) const
    {
        std::lock_guard _(mutex_);

        const auto request_it = requests_by_stream_.find(stream_id);
        if (request_it == requests_by_stream_.end()) {
            return std::nullopt;
        }

        return request_it->second;
    }

} // namespace quicr
