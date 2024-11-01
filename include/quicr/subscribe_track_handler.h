// SPDX-FileCopyrightText: Copyright (c) 2024 Cisco Systems
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

#include <quicr/detail/base_track_handler.h>
#include <quicr/metrics.h>
#include <quicr/publish_track_handler.h>

namespace quicr {

    /**
     * @brief MOQ track handler for subscribed track
     *
     * @details MOQ subscribe track handler defines all track related callbacks and
     *  functions for subscribe. Track handler operates on a single track (namespace + name).
     *
     *  This extends the base track handler to add subscribe handling
     */
    class SubscribeTrackHandler : public BaseTrackHandler
    {
      public:
        /**
         * @brief Receive status codes
         */
        enum class Error : uint8_t
        {
            kOk = 0,
            kNotAuthorized,
            kNotSubscribed,
            kNoData
        };

        /**
         * @brief  Status codes for the subscribe track
         */
        enum class Status : uint8_t
        {
            kOk = 0,
            kNotConnected,
            kSubscribeError,
            kNotAuthorized,
            kNotSubscribed,
            kPendingSubscribeResponse,
            kSendingUnsubscribe ///< In this state, callbacks will not be called
        };

      protected:
        /**
         * @brief Subscribe track handler constructor
         *
         * @param full_track_name           Full track name struct
         */
        SubscribeTrackHandler(const FullTrackName& full_track_name,
                              messages::ObjectPriority priority,
                              messages::GroupOrder group_order)
          : BaseTrackHandler(full_track_name)
          , subscription_priority_(priority)
          , subscription_group_order_(group_order)
        {
        }

      public:
        /**
         * @brief Create shared Subscribe track handler
         *
         * @param full_track_name           Full track name struct
         * @param priority                  Subscription priority, if omitted, publisher priority
         *                                  is considered
         * @param group_order               Order for group delivery
         */
        static std::shared_ptr<SubscribeTrackHandler> Create(
          const FullTrackName& full_track_name,
          messages::ObjectPriority priority,
          messages::GroupOrder group_order = messages::GroupOrder::kAscending)
        {
            return std::shared_ptr<SubscribeTrackHandler>(
              new SubscribeTrackHandler(full_track_name, priority, group_order));
        }

        /**
         * @brief Get the status of the subscribe
         *
         * @return Status of the subscribe
         */
        constexpr Status GetStatus() const noexcept { return status_; }

        /**
         * @brief Get subscription priority
         *
         * @return Priority value
         */
        constexpr messages::ObjectPriority GetPriority() const noexcept { return subscription_priority_; }

        /**
         * @brief Get subscription group order
         *
         * @return GroupOrder value
         */

        constexpr messages::GroupOrder GetGroupOrder() const noexcept { return subscription_group_order_; }

        // --------------------------------------------------------------------------
        // Public Virtual API callback event methods
        // --------------------------------------------------------------------------
        /** @name Callbacks
         */
        ///@{

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
         * @param status        Indicates status of the subscribe
         */
        virtual void StatusChanged([[maybe_unused]] Status status) {}

        /**
         * @brief Notification callback to provide sampled metrics
         *
         * @details Callback will be triggered on Config::metrics_sample_ms to provide the sampled data based
         *      on the sample period.  After this callback, the period/sample based metrics will reset and start over
         *      for the new period.
         *
         * @param metrics           Copy of the subscribed metrics for the sample period
         */
        virtual void MetricsSampled([[maybe_unused]] const SubscribeTrackMetrics& metrics) {}

        ///@}

        // --------------------------------------------------------------------------
        // Metrics
        // --------------------------------------------------------------------------

        /**
         * @brief Subscribe metrics for the track
         *
         * @details Subscribe metrics are updated real-time and transport quic metrics on metrics_sample_ms
         *     period.
         */
        SubscribeTrackMetrics subscribe_track_metrics_;

        // --------------------------------------------------------------------------
        // Internal
        // --------------------------------------------------------------------------
      private:
        /**
         * @brief Set the subscribe status
         * @param status                Status of the subscribe
         */
        void SetStatus(Status status) noexcept
        {
            status_ = status;
            StatusChanged(status);
        }

        // --------------------------------------------------------------------------
        // Member variables
        // --------------------------------------------------------------------------
        Status status_{ Status::kNotSubscribed };
        messages::ObjectPriority subscription_priority_;
        messages::GroupOrder subscription_group_order_;

        friend class Transport;
        friend class Client;
        friend class Server;
    };

} // namespace moq
