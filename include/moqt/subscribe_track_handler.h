/*
 *  Copyright (C) 2024
 *  Cisco Systems, Inc.
 *  All Rights Reserved
 */
#pragma once

#include <moqt/core/base_track_handler.h>
#include <moqt/metrics.h>
#include <moqt/publish_track_handler.h>

namespace moq::transport {

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
        friend class Transport;

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
            kSendingUnsubscribe // Triggers callbacks to not be called in this state
        };

        // --------------------------------------------------------------------------
        // Public API methods that normally should not be overridden
        // --------------------------------------------------------------------------

        /**
         * @brief Subscribe track handler constructor
         *
         * @param full_track_name       Full track name struct
         */
        SubscribeTrackHandler(const FullTrackName& full_track_name)
          : BaseTrackHandler(full_track_name)
        {
        }

        /**
         * @brief Get the status of the subscribe
         *
         * @return Status of the subscribe
         */
        Status GetStatus() { return status_; }

        // --------------------------------------------------------------------------
        // Public Virtual API callback event methods to be overridden
        // --------------------------------------------------------------------------

        /**
         * @brief Notification of received data object
         *
         * @details Event notification to provide the caller the received data object
         *
         * @param group_id          Group ID of the object received
         * @param object_id         Object ID of the object received
         * @param priority          Priority of the object received
         * @param object            Data object received
         * @param track_mode        Track mode the object was received
         */
        virtual void ObjectReceived(uint64_t group_id,
                                    uint64_t object_id,
                                    uint8_t priority,
                                    std::vector<uint8_t>&& object,
                                    TrackMode track_mode) = 0;

        /**
         * @brief Notification of subscribe status
         *
         * @details Notification of the subscribe status
         *
         * @param status        Indicates status of the subscribe
         */
        virtual void StatusChanged(Status status) = 0;

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
        void SetStatus(Status status) { status_ = status; }

        // --------------------------------------------------------------------------
        // Member variables
        // --------------------------------------------------------------------------
        Status status_{ Status::kNotSubscribed };
    };

} // namespace moq::transport
