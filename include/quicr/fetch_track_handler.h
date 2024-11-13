// SPDX-FileCopyrightText: Copyright (c) 2024 Cisco Systems
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

#include <quicr/detail/base_track_handler.h>
#include <quicr/detail/messages.h>
#include <quicr/metrics.h>

namespace quicr {
    /**
     * @brief
     */
    class FetchTrackHandler : public BaseTrackHandler
    {
      public:
        /**
         * @brief Receive status codes
         */
        enum class Error : uint8_t
        {
            kOk = 0,
            kNotAuthorized,
            kObjectNotAvailable,
        };

        /**
         * @brief  Status codes for the track
         */
        enum class Status : uint8_t
        {
            kOk = 0,
            kNotConnected,
            kError,
            kNotAuthorized,
            kObjectNotAvailable,
            kPendingResponse,
        };

      protected:
        /**
         * @brief Fetch track handler constructor
         *
         * @param full_track_name Full track name struct.
         * @param priority The priority of the track.
         * @param group_order The group order to use.
         * @param start_group The starting group of the range.
         * @param start_object THe starting object in a group.
         * @param end_group The final group in the range.
         * @param end_object THe final object in a group.
         */
        FetchTrackHandler(const FullTrackName& full_track_name,
                          messages::ObjectPriority priority,
                          messages::GroupOrder group_order,
                          messages::GroupId start_group,
                          messages::GroupId start_object,
                          messages::GroupId end_group,
                          messages::GroupId end_object)
          : BaseTrackHandler(full_track_name)
          , priority_(priority)
          , group_order_(group_order)
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
         * @param start_object THe starting object in a group.
         * @param end_group The final group in the range.
         * @param end_object THe final object in a group.
         *
         * @returns Shared pointer to a Fetch track handler.
         */
        static std::shared_ptr<FetchTrackHandler> Create(const FullTrackName& full_track_name,
                                                         messages::ObjectPriority priority,
                                                         messages::GroupOrder group_order,
                                                         messages::GroupId start_group,
                                                         messages::GroupId start_object,
                                                         messages::GroupId end_group,
                                                         messages::GroupId end_object)
        {
            return std::shared_ptr<FetchTrackHandler>(new FetchTrackHandler(
              full_track_name, priority, group_order, start_group, start_object, end_group, end_object));
        }

        /**
         * @brief Get the status
         *
         * @returns The status of the fetch
         */
        constexpr Status GetStatus() const noexcept { return status_; }

        /**
         * @brief Notification of received [full] data object
         *
         * @details Event notification to provide the caller the received full data object
         *
         * @warning This data will be invalided after return of this method
         *
         * @param object_headers    Object headers, must include group and object Ids
         * @param data              Object payload data received, **MUST** match ObjectHeaders::payload_length.
         */
        virtual void ObjectReceived([[maybe_unused]] const ObjectHeaders& object_headers,
                                    [[maybe_unused]] BytesSpan data)
        {
        }

        /**
         * @brief Notification of a partial object received data object
         *
         * @details Event notification to provide the caller the received data object
         *
         * @warning This data will be invalided after return of this method
         *
         * @param object_headers    Object headers, must include group and object Ids
         * @param data              Object payload data received, can be <= ObjectHeaders::payload_length
         */
        virtual void PartialObjectReceived([[maybe_unused]] const ObjectHeaders& object_headers,
                                           [[maybe_unused]] BytesSpan data)
        {
        }

        /**
         * @brief Notification of subscribe status
         *
         * @details Notification of the subscribe status
         *
         * @param status Indicates status of the subscribe
         */
        virtual void StatusChanged([[maybe_unused]] Status status) {}

        /**
         * @brief Get priority
         *
         * @return Priority value
         */
        constexpr messages::ObjectPriority GetPriority() const noexcept { return priority_; }

        /**
         * @brief Get group order
         *
         * @return GroupOrder value
         */
        constexpr messages::GroupOrder GetGroupOrder() const noexcept { return group_order_; }

        constexpr messages::GroupId GetStartGroup() const noexcept { return start_group_; }
        constexpr messages::GroupId GetStartObject() const noexcept { return start_object_; }
        constexpr messages::GroupId GetEndGroup() const noexcept { return end_group_; }
        constexpr messages::GroupId GetEndObject() const noexcept { return end_object_; }

      private:
        /**
         * @brief Set the status
         * @param status Status of the subscribe
         */
        void SetStatus(Status status)
        {
            status_ = status;
            StatusChanged(status);
        }

      private:
        Status status_{ Status::kPendingResponse };
        messages::ObjectPriority priority_;
        messages::GroupOrder group_order_;
        messages::GroupId start_group_;
        messages::GroupId start_object_;
        messages::GroupId end_group_;
        messages::GroupId end_object_;

        friend class Transport;
        friend class Client;
        friend class Server;
    };

} // namespace moq
