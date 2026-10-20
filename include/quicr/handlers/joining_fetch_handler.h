// SPDX-FileCopyrightText: Copyright (c) 2025 Cisco Systems
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

#include "quicr/handlers/subscribe_track_handler.h"
#include "quicr/messages/message_serialisation.h"

namespace quicr {
    /**
     * @brief Adapts a joining-fetch stream to an existing subscription.
     *
     * @details The handler retains the target subscription and forwards each decoded object to
     * `SubscribeTrackHandler::ObjectReceived`. It is an internal convenience adapter; the target
     * handler must remain safe to call on the session's receive-callback thread.
     */
    class JoiningFetchHandler : public SubscribeTrackHandler
    {
      public:
        /**
         * @brief Constructs an adapter for a subscription's joining fetch.
         * @param joining_subscribe Subscription that receives the fetched objects. Must not be null;
         *        ownership is shared for the lifetime of this adapter.
         * @param group_order Requested fetch delivery order.
         */
        explicit JoiningFetchHandler(std::shared_ptr<SubscribeTrackHandler> joining_subscribe,
                                     messages::GroupOrder group_order = messages::GroupOrder::kAscending)
          : SubscribeTrackHandler(joining_subscribe->GetFullTrackName(),
                                  joining_subscribe->GetSubscriberPriority(),
                                  group_order,
                                  joining_subscribe->GetFilter())
          , joining_subscribe_(std::move(joining_subscribe))
        {
        }

        /**
         * @brief Forwards a fetched object to the joined subscription.
         * @param object_headers Decoded object metadata; borrowed for this call.
         * @param data Borrowed payload view, valid only for the duration of the callback.
         * @param stream_mode Stream framing properties when this is the first object on its subgroup stream.
         *
         * @note Called synchronously on the session receive path. The target callback must copy
         * `data` before returning if it needs to retain the payload.
         */
        void ObjectReceived(const ObjectHeaders& object_headers,
                            BytesSpan data,
                            std::optional<messages::StreamHeaderProperties> stream_mode = std::nullopt) override;

      private:
        std::shared_ptr<SubscribeTrackHandler> joining_subscribe_;
    };

} // namespace moq
