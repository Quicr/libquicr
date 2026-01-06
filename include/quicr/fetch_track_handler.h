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
         * @param start_location The starting location of the fetch's range (inclusive).
         * @param end_location The ending location of the fetch's range (inclusive).
         * entire end_group.
         */
        FetchTrackHandler(const FullTrackName& full_track_name,
                          const messages::SubscriberPriority priority,
                          const messages::GroupOrder group_order,
                          const messages::Location& start_location,
                          const messages::FetchEndLocation& end_location)
          : SubscribeTrackHandler(full_track_name, priority, group_order, messages::FilterType::kNextGroupStart)
          , start_location_(start_location)
          , end_location_(end_location)
        {
            is_fetch_handler_ = true;
        }

      public:
        /**
         * @brief Create shared Fetch track handler.
         *
         * @param full_track_name Full track name struct.
         * @param priority The priority of the track.
         * @param group_order The group order to use.
         * @param start_location The starting location of the fetch's range (inclusive).
         * @param end_location The ending location of the fetch's range (inclusive).
         * entire end_group.
         *
         * @returns Shared pointer to a Fetch track handler.
         */
        static std::shared_ptr<FetchTrackHandler> Create(const FullTrackName& full_track_name,
                                                         const messages::SubscriberPriority priority,
                                                         const messages::GroupOrder group_order,
                                                         const messages::Location& start_location,
                                                         const messages::FetchEndLocation& end_location)
        {
            return std::shared_ptr<FetchTrackHandler>(
              new FetchTrackHandler(full_track_name, priority, group_order, start_location, end_location));
        }

        /**
         * @brief Get the start location of the Fetch.
         * @returns The starting location.
         */
        constexpr const messages::Location& GetStartLocation() const noexcept { return start_location_; }

        /**
         * @brief Get the end location of the Fetch.
         * @returns The ending location.
         */
        constexpr const messages::FetchEndLocation& GetEndLocation() const noexcept { return end_location_; }

        void StreamDataRecv(bool is_start,
                            uint64_t stream_id,
                            std::shared_ptr<const std::vector<uint8_t>> data) override;

      private:
        messages::Location start_location_;
        messages::FetchEndLocation end_location_;

        friend class Transport;
        friend class Client;
        friend class Server;
    };

} // namespace moq
