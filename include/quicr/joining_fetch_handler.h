// SPDX-FileCopyrightText: Copyright (c) 2025 Cisco Systems
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

#include <quicr/messages.h>
#include <quicr/subscribe_track_handler.h>

namespace quicr {
    /**
     * JoiningFetchHandler is used internally in order to forward JOINING FETCH
     * streams to their corresponding SUBSCRIBE track handler, for convenience.
     */
    class JoiningFetchHandler : public SubscribeTrackHandler
    {
      public:
        explicit JoiningFetchHandler(std::shared_ptr<SubscribeTrackHandler> joining_subscribe)
          : SubscribeTrackHandler(joining_subscribe->GetFullTrackName(),
                                  joining_subscribe->GetPriority(),
                                  joining_subscribe->GetGroupOrder(),
                                  joining_subscribe->GetFilterType())
          , joining_subscribe_(std::move(joining_subscribe))
        {
        }
        void StreamDataRecv(bool is_start,
                            uint64_t stream_id,
                            std::shared_ptr<const std::vector<uint8_t>> data) override;

      private:
        std::shared_ptr<SubscribeTrackHandler> joining_subscribe_;
    };

} // namespace moq
