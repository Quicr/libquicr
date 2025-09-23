// SPDX-FileCopyrightText: Copyright (c) 2025 Cisco Systems
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

#include "subscribe_track_handler.h"

namespace quicr {
    /**
     * JoiningFetchHandler is used internally in order to forward JOINING FETCH
     * streams to their corresponding SUBSCRIBE track handler, for convenience.
     */
    class JoiningFetchHandler : public SubscribeTrackHandler
    {
      public:
        explicit JoiningFetchHandler(std::shared_ptr<SubscribeTrackHandler> joining_subscribe_handler);

        void StreamDataRecv(bool is_start,
                            uint64_t stream_id,
                            std::shared_ptr<const std::vector<uint8_t>> data) override;

      private:
        std::shared_ptr<SubscribeTrackHandler> joining_subscribe_handler_;
    };

} // namespace moq
