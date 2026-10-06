// SPDX-FileCopyrightText: Copyright (c) 2026 Cisco Systems
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

#include <map>
#include <memory>
#include <optional>
#include <set>

namespace quicr {

    class TrackHandler;

    /// Stores MoQ request state for lookup.
    class RequestStore
    {
      public:
        // TODO: Determine minimal info Session callers actually need returned.
        struct RequestState
        {
            // MoQ ID for this request.
            const std::uint64_t request_id;
            // QUIC stream ID for the bidir request stream.
            const std::uint64_t request_stream_id;
            // The handler for this request, if any.
            const std::shared_ptr<TrackHandler> handler;
        };

        /**
         * Store a request.
         * @param request_id The MoQ request ID.
         * @param stream_id The QUIC stream ID for the request.
         * @param handler Handler, if any.
         * @throws std::invalid_argument Duplicate request or stream.
         */
        void Register(std::uint64_t request_id, std::uint64_t stream_id, std::shared_ptr<TrackHandler> handler);

        /**
         * Remove a stored request.
         * @throws std::invalid_argument The given request does not exist.
         */
        void Unregister(std::uint64_t request_id);

        /**
         * Get the request for this stream, if any.
         * @param stream_id QUIC stream ID.
         * @return The request, if tracked.
         */
        [[nodiscard]] std::optional<RequestState> FindByStream(std::uint64_t stream_id) const;

        /**
         * Record a data stream ID for this request.
         * @param request_id Request ID.
         * @param stream_id QUIC stream ID.
         * @throws std::invalid_argument Missing request or stream ID reuse.
         */
        void AddDataStream(std::uint64_t request_id, std::uint64_t stream_id);

        /**
         * Remove a recorded data stream ID for this request.
         * @param request_id Request ID.
         * @param stream_id QUIC stream ID.
         * @throws std::invalid_argument Missing request.
         */
        void RemoveDataStream(std::uint64_t request_id, std::uint64_t stream_id);

      private:
        struct StoredRequest
        {
            RequestState state;
            std::set<std::uint64_t> data_stream_ids;
        };

        // Request ID to request.
        std::map<std::uint64_t, StoredRequest> requests_by_id_;
        // Request and data stream IDs to request ID.
        std::map<std::uint64_t, std::uint64_t> request_id_by_stream_;
    };

} // namespace quicr
