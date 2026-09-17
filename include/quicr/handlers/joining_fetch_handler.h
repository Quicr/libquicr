// SPDX-FileCopyrightText: Copyright (c) 2025 Cisco Systems
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

#include "quicr/handlers/subscribe_track_handler.h"
#include "quicr/messages/messages.h"

namespace quicr {
    /**
     * JoiningFetchHandler is used internally in order to forward JOINING FETCH
     * streams to their corresponding SUBSCRIBE track handler, for convenience.
     */
    class JoiningFetchHandler : public SubscribeTrackHandler
    {
      public:
        explicit JoiningFetchHandler(std::shared_ptr<SubscribeTrackHandler> joining_subscribe,
                                     messages::GroupOrder group_order = messages::GroupOrder::kAscending)
          : SubscribeTrackHandler(joining_subscribe->GetFullTrackName(),
                                  joining_subscribe->GetPriority(),
                                  group_order,
                                  joining_subscribe->GetFilter())
          , joining_subscribe_(std::move(joining_subscribe))
        {
        }

        void ObjectReceived(const ObjectHeaders& object_headers,
                            BytesSpan data,
                            std::optional<messages::StreamHeaderProperties> stream_mode = std::nullopt) override;

      private:
        std::shared_ptr<SubscribeTrackHandler> joining_subscribe_;
    };

} // namespace moq
