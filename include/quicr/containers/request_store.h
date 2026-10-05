// SPDX-FileCopyrightText: Copyright (c) 2026 Cisco Systems
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

#include "quicr/utilities/thread_safety.h"

#include <map>
#include <memory>
#include <mutex>
#include <optional>

namespace quicr {

    class TrackHandler;

    /// Stores MoQ request state for lookup.
    class RequestStore
    {
      public:
        struct RequestState
        {
            // MoQ ID for this request.
            const std::uint64_t request_id;
            // QUIC stream ID for the bidir request stream.
            const std::uint64_t stream_id;
            // The handler for this request, if any.
            const std::shared_ptr<TrackHandler> handler;
        };

        /**
         * Store a request.
         * @param request_id The MoQ request ID.
         * @param stream_id The QUIC stream ID for the request.
         * @param handler Handler, if any.
         * @throws std::invalid_argument Duplicate request.
         */
        void Register(std::uint64_t request_id, std::uint64_t stream_id, std::shared_ptr<TrackHandler> handler);

        /// Return the request for this stream.
        [[nodiscard]] std::optional<RequestState> FindByStream(std::uint64_t stream_id) const;

      private:
        mutable std::mutex mutex_;

        // Stream ID to request.
        std::map<std::uint64_t, RequestState> requests_by_stream_ QUICR_GUARDED_BY(mutex_);
        // Request ID to Stream ID.
        std::map<std::uint64_t, std::uint64_t> stream_by_request_id_ QUICR_GUARDED_BY(mutex_);
    };

} // namespace quicr
