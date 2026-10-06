// SPDX-FileCopyrightText: Copyright (c) 2026 Cisco Systems
// SPDX-License-Identifier: BSD-2-Clause

#include "quicr/containers/request_store.h"

#include <stdexcept>

namespace quicr {

    void RequestStore::Register(std::uint64_t request_id,
                                std::uint64_t stream_id,
                                std::shared_ptr<TrackHandler> handler)
    {
        if (request_id_by_stream_.contains(stream_id)) {
            throw std::invalid_argument("Stream already exists");
        }

        const auto [request_it, inserted] =
          requests_by_id_.try_emplace(request_id, StoredRequest{ { request_id, stream_id, std::move(handler) }, {} });
        if (!inserted) {
            throw std::invalid_argument("Request already exists");
        }

        try {
            request_id_by_stream_.emplace(stream_id, request_id);
        } catch (...) {
            requests_by_id_.erase(request_it);
            throw;
        }
    }

    void RequestStore::Unregister(std::uint64_t request_id)
    {
        const auto request_it = requests_by_id_.find(request_id);
        if (request_it == requests_by_id_.end()) {
            throw std::invalid_argument("Request not found");
        }
        for (const auto data_stream_id : request_it->second.data_stream_ids) {
            request_id_by_stream_.erase(data_stream_id);
        }
        request_id_by_stream_.erase(request_it->second.state.request_stream_id);
        requests_by_id_.erase(request_it);
    }

    void RequestStore::AddDataStream(std::uint64_t request_id, std::uint64_t stream_id)
    {
        const auto request_it = requests_by_id_.find(request_id);
        if (request_it == requests_by_id_.end()) {
            throw std::invalid_argument("Request does not exist");
        }

        const auto stream_it = request_id_by_stream_.find(stream_id);
        if (stream_it != request_id_by_stream_.end()) {
            if (request_it->second.data_stream_ids.contains(stream_id)) {
                return;
            }
            throw std::invalid_argument("Stream already exists");
        }

        auto& data_stream_ids = request_it->second.data_stream_ids;
        const auto data_stream_it = data_stream_ids.emplace(stream_id).first;
        try {
            request_id_by_stream_.emplace(stream_id, request_id);
        } catch (...) {
            data_stream_ids.erase(data_stream_it);
            throw;
        }
    }

    void RequestStore::RemoveDataStream(std::uint64_t request_id, std::uint64_t stream_id)
    {
        const auto request_it = requests_by_id_.find(request_id);
        if (request_it == requests_by_id_.end()) {
            throw std::invalid_argument("Request does not exist");
        }
        if (request_it->second.data_stream_ids.erase(stream_id) != 0) {
            request_id_by_stream_.erase(stream_id);
        }
    }

    std::optional<RequestStore::RequestState> RequestStore::FindByStream(std::uint64_t stream_id) const
    {
        const auto stream_it = request_id_by_stream_.find(stream_id);
        if (stream_it == request_id_by_stream_.end()) {
            return std::nullopt;
        }
        return requests_by_id_.at(stream_it->second).state;
    }

} // namespace quicr
