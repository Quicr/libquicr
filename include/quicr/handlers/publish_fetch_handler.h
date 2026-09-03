// SPDX-FileCopyrightText: Copyright (c) 2025 Cisco Systems
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

#include "quicr/handlers/publish_track_handler.h"
#include "quicr/messages/messages.h"

namespace quicr {
    class PublishFetchHandler : public PublishTrackHandler
    {
      protected:
        PublishFetchHandler(const FullTrackName& full_track_name,
                            uint8_t priority,
                            uint64_t subscribe_id,
                            messages::GroupOrder group_order,
                            uint32_t ttl)
          : PublishTrackHandler(full_track_name, TrackMode::kStream, priority, ttl)
          , group_order_(group_order)
          , serialization_state_(group_order)
        {
            SetRequestId(subscribe_id);
        }

      public:
        static std::shared_ptr<PublishFetchHandler> Create(const FullTrackName& full_track_name,
                                                           uint8_t priority,
                                                           uint64_t subscribe_id,
                                                           messages::GroupOrder group_order,
                                                           uint32_t ttl)
        {
            return std::shared_ptr<PublishFetchHandler>(
              new PublishFetchHandler(full_track_name, priority, subscribe_id, group_order, ttl));
        }
        PublishObjectStatus PublishObject(
          const ObjectHeaders& object_headers,
          BytesSpan data,
          std::optional<messages::StreamHeaderProperties> stream_mode = std::nullopt) override;
        constexpr messages::GroupOrder GetGroupOrder() const noexcept { return group_order_; }

      private:
        /**
         * @brief Finish the fetch: let whatever is queued drain, then close the stream.
         *
         * @details The empty object carries no payload; it exists to mark the stream close-on-empty
         *      once the transport reaches it, so objects already queued still go out. Does nothing if
         *      the fetch never sent an object, since there is no stream to close.
         */
        void EndFetch();

        messages::GroupOrder group_order_;
        messages::FetchObjectSerializationState serialization_state_;
        bool sent_first_header_{ false };
        std::shared_ptr<Stream> stream_; /// Stream for the fetch, created on sent_first_header

        friend class Session;
    };

} // namespace moq
