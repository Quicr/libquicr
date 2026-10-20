// SPDX-FileCopyrightText: Copyright (c) 2024 Cisco Systems
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

#include "quicr/handlers/subscribe_track_handler.h"
#include "quicr/messages/message_serialisation.h"
#include "quicr/metrics.h"

namespace quicr {
    /**
     * @brief Handler for receiving a bounded FETCH response.
     *
     * @details A fetch uses the subscription object callbacks but requests an inclusive range.
     * Instances are heap-owned through `Create()` and are retained by the session while active.
     */
    class FetchTrackHandler : public SubscribeTrackHandler
    {
      protected:
        /**
         * @brief Fetch track handler constructor
         *
         * @param full_track_name Full track name struct.
         * @param priority The priority of the track.
         * @param start_location The starting location of the fetch's range (inclusive).
         * @param end_location The ending location of the fetch's range (inclusive).
         * @param group_order The requested object delivery order.
         */
        FetchTrackHandler(const FullTrackName& full_track_name,
                          const std::uint8_t priority,
                          const messages::Location& start_location,
                          const messages::FetchEndLocation& end_location,
                          const messages::GroupOrder group_order = messages::GroupOrder::kAscending)
          : SubscribeTrackHandler(full_track_name,
                                  priority,
                                  group_order,
                                  messages::LocationFilter{
                                    start_location.group,
                                    end_location.group,
                                  })
          , start_location_(start_location)
          , end_location_(end_location)
        {
        }

      public:
        /**
         * @brief Create shared Fetch track handler.
         *
         * @param full_track_name Full track name struct.
         * @param priority The priority of the track.
         * @param start_location The starting location of the fetch's range (inclusive).
         * @param end_location The ending location of the fetch's range (inclusive).
         * @param group_order The requested object delivery order.
         *
         * @return Shared ownership of the new handler.
         */
        static std::shared_ptr<FetchTrackHandler> Create(
          const FullTrackName& full_track_name,
          const std::uint8_t priority,
          const messages::Location& start_location,
          const messages::FetchEndLocation& end_location,
          const messages::GroupOrder group_order = messages::GroupOrder::kAscending)
        {
            return std::shared_ptr<FetchTrackHandler>(
              new FetchTrackHandler(full_track_name, priority, start_location, end_location, group_order));
        }

        /**
         * @brief Get the start location of the Fetch.
         * @return Borrowed reference to the immutable inclusive start location. The reference
         * remains valid for the handler's lifetime.
         */
        constexpr const messages::Location& GetStartLocation() const noexcept { return start_location_; }

        /**
         * @brief Get the end location of the Fetch.
         * @return Borrowed reference to the immutable inclusive end location. The reference
         * remains valid for the handler's lifetime.
         */
        constexpr const messages::FetchEndLocation& GetEndLocation() const noexcept { return end_location_; }

      private:
        messages::Location start_location_;
        messages::FetchEndLocation end_location_;

        friend class Session;
    };

} // namespace moq
