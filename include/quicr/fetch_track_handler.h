// SPDX-FileCopyrightText: Copyright (c) 2024 Cisco Systems
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

#include <quicr/detail/messages.h>
#include <quicr/metrics.h>
#include <quicr/subscribe_track_handler.h>

namespace quicr {
    class FetchTrackHandler : public SubscribeTrackHandler
    {
      protected:
        /**
         * @brief Fetch track handler constructor
         *
         * @param full_track_name Full track name struct.
         * @param priority The priority of the track.
         * @param group_order The group order to use.
         * @param start_group The starting group of the range.
         * @param end_group The final group in the range.
         * @param start_object The starting object in a group.
         * @param end_object The final object in a group.
         */
        FetchTrackHandler(const FullTrackName& full_track_name,
                          quicr::messages::SubscriberPriority priority,
                          quicr::messages::GroupOrder group_order,
                          quicr::messages::GroupId start_group,
                          quicr::messages::GroupId end_group,
                          quicr::messages::GroupId start_object,
                          quicr::messages::GroupId end_object)
          : SubscribeTrackHandler(full_track_name,
                                  priority,
                                  group_order,
                                  quicr::messages::FilterTypeEnum::kLatestGroup)
          , start_group_(start_group)
          , start_object_(start_object)
          , end_group_(end_group)
          , end_object_(end_object)
        {
        }

      public:
        /**
         * @brief Create shared Fetch track handler.
         *
         * @param full_track_name Full track name struct.
         * @param priority The priority of the track.
         * @param group_order The group order to use.
         * @param start_group The starting group of the range.
         * @param start_object The starting object in a group.
         * @param end_group The final group in the range.
         * @param end_object The final object in a group.
         *
         * @returns Shared pointer to a Fetch track handler.
         */
        static std::shared_ptr<FetchTrackHandler> Create(const FullTrackName& full_track_name,
                                                         quicr::messages::SubscriberPriority priority,
                                                         quicr::messages::GroupOrderEnum group_order,
                                                         quicr::messages::GroupId start_group,
                                                         quicr::messages::GroupId end_group,
                                                         quicr::messages::GroupId start_object,
                                                         quicr::messages::GroupId end_object)
        {
            return std::shared_ptr<FetchTrackHandler>(new FetchTrackHandler(
              full_track_name, priority, group_order, start_group, end_group, start_object, end_object));
        }

        /**
         * @brief Get the starting group id of the Fetch range.
         * @returns The starting group ID.
         */
        constexpr const quicr::messages::GroupId& GetStartGroup() const noexcept { return start_group_; }

        /**
         * @brief Get the id of the group one past the end of the Fetch range.
         * @returns The ending group ID.
         */
        constexpr const quicr::messages::GroupId& GetEndGroup() const noexcept { return end_group_; }

        /**
         * @brief Get the starting object id of the Group range.
         * @returns The starting object ID.
         */
        constexpr const quicr::messages::GroupId& GetStartObject() const noexcept { return start_object_; }

        /**
         * @brief Get the id of the object one past the end of the group range.
         * @returns The ending object ID.
         */
        constexpr const quicr::messages::GroupId& GetEndObject() const noexcept { return end_object_; }

        void StreamDataRecv(bool is_start,
                            uint64_t stream_id,
                            std::shared_ptr<const std::vector<uint8_t>> data) override;

      private:
        quicr::messages::GroupId start_group_;
        quicr::messages::GroupId start_object_;
        quicr::messages::GroupId end_group_;
        quicr::messages::GroupId end_object_;

        friend class Transport;
        friend class Client;
        friend class Server;
    };

} // namespace moq
