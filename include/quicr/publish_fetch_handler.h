// SPDX-FileCopyrightText: Copyright (c) 2025 Cisco Systems
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

#include <quicr/detail/messages.h>

namespace quicr {
    class PublishFetchHandler : BaseTrackHandler
    {
      protected:
        /**
         * @brief Fetch publish track handler constructor
         *
         * @param subscribe_id Subscribe ID.
         */
        explicit PublishFetchHandler(const std::uint64_t subscribe_id,
                                     const FullTrackName full_track_name,
                                     messages::ObjectPriority priority,
                                     messages::GroupOrder group_order)
          : BaseTrackHandler(full_track_name)
          , priority_(priority)
          , group_order_(group_order)
        {
            SetSubscribeId(subscribe_id);
        }

      public:
        /**
         * @brief Create shared Fetch publish handler.
         *
         * @param subscribe_id Subscribe ID.
         *
         * @returns Shared pointer to a Fetch publish handler.
         */
        static std::shared_ptr<PublishFetchHandler> Create(const std::uint64_t subscribe_id,
                                                           const FullTrackName full_track_name,
                                                           messages::ObjectPriority priority,
                                                           messages::GroupOrder group_order)
        {
            return std::shared_ptr<PublishFetchHandler>(
              new PublishFetchHandler(subscribe_id, full_track_name, priority, group_order));
        }

        bool PublishObject(const ObjectHeaders& object_headers, const BytesSpan data);
        constexpr messages::GroupOrder GetGroupOrder() const noexcept { return group_order_; }
        constexpr messages::ObjectPriority GetPriority() const noexcept { return priority_; }

      private:
        messages::ObjectPriority priority_;
        messages::GroupOrder group_order_;
        bool sent_first_header_{ false };
        using PublishObjFunction = std::function<bool(uint8_t priority,
                                                      uint32_t ttl,
                                                      bool stream_header_needed,
                                                      uint64_t group_id,
                                                      uint64_t subgroup_id,
                                                      uint64_t object_id,
                                                      std::optional<Extensions> extensions,
                                                      BytesSpan data)>;
        uint64_t publish_data_ctx_id_;                      // set by the transport; publishing data context ID
        PublishObjFunction publish_object_func_{ nullptr }; // set by the transport
        Bytes object_msg_buffer_;                           // TODO(tievens): Review shrink/resize

        friend class Transport;
        friend class Server;
    };

} // namespace moq
