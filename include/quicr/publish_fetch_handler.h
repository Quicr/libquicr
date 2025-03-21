// SPDX-FileCopyrightText: Copyright (c) 2025 Cisco Systems
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

#include <quicr/detail/ctrl_messages.h>
#include <quicr/detail/messages.h>
#include <quicr/publish_track_handler.h>

namespace quicr {
    class PublishFetchHandler : public PublishTrackHandler
    {
      protected:
        PublishFetchHandler(const FullTrackName& full_track_name,
                            uint8_t priority,
                            uint64_t subscribe_id,
                            quicr::ctrl_messages::GroupOrder group_order,
                            uint32_t ttl)
          : PublishTrackHandler(full_track_name, TrackMode::kStream, priority, ttl)
          , group_order_(group_order)
        {
            SetSubscribeId(subscribe_id);
        }

      public:
        static std::shared_ptr<PublishFetchHandler> Create(const FullTrackName& full_track_name,
                                                           uint8_t priority,
                                                           uint64_t subscribe_id,
                                                           quicr::ctrl_messages::GroupOrderEnum group_order,
                                                           uint32_t ttl)
        {
            return std::shared_ptr<PublishFetchHandler>(
              new PublishFetchHandler(full_track_name, priority, subscribe_id, group_order, ttl));
        }
        PublishObjectStatus PublishObject(const ObjectHeaders& object_headers, BytesSpan data) override;
        constexpr quicr::ctrl_messages::GroupOrder GetGroupOrder() const noexcept { return group_order_; }

      private:
        quicr::ctrl_messages::GroupOrder group_order_;
        bool sent_first_header_{ false };
    };

} // namespace moq
