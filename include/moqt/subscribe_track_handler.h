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
            kSendingUnsubscribe                 ///< In this state, callbacks will not be called
        };

        SubscribeTrackHandler() = delete;

        // --------------------------------------------------------------------------
        // Public API methods
        // --------------------------------------------------------------------------

        /**
         * @brief Subscribe track handler constructor
         *
         * @param full_track_name           Full track name struct
         * @param only_complete_objects     If **false**, the ObjectReceived() can be called back with incomplete data.
         *                                  In the case of incomplete data, ContinuationDataReceived() will be called
         *                                  with the remaining data. If **true** ObjectReceived() will only be called
         *                                  with complete data.
         */
        SubscribeTrackHandler(const FullTrackName& full_track_name, bool only_complete_objects)
          : BaseTrackHandler(full_track_name)
          , only_complete_objects_(only_complete_objects)
        {
        }

        /**
         * @brief Get the status of the subscribe
         *
         * @return Status of the subscribe
         */
        Status GetStatus() { return status_; }

        // --------------------------------------------------------------------------
        // Public Virtual API callback event methods
        // --------------------------------------------------------------------------

        /**
         * @brief Notification of received data object
         *
         * @details Event notification to provide the caller the received data object
         *
         * @warning This data will be invalided after return of this method
         *
         * @param object_headers    Object headers, must include group and object Ids
         * @param data              Object payload data received. If only_complete_objects_ is false, data may be
         *                          less than payload length. The caller **MUST** implement ContinuationDataReceived()
         *                          to receive the remaining bytes of the payload.
         * @param track_mode        Track mode the object was received
         */
        virtual void ObjectReceived(const ObjectHeaders& object_headers,
                                    Span<uint8_t> data,
                                    TrackMode track_mode) {}

        /**
         * @brief Notification of a partial object received data object
         *
         * @details Event notification to provide the caller the received data object
         *
         * @warning This data will be invalided after return of this method
         *
         * @param object_headers    Object headers, must include group and object Ids
         * @param data              Object payload data received. If only_complete_objects_ is false, data may be
         *                          less than payload length. The caller **MUST** implement ContinuationDataReceived()
         *                          to receive the remaining bytes of the payload.
         * @param track_mode        Track mode the object was received
         */
        virtual void PartialObjectReceived(const ObjectHeaders& object_headers,
                                           Span<uint8_t> data,
                                           TrackMode track_mode) {}

        /**
         * @brief Notification of subscribe status
         *
         * @details Notification of the subscribe status
         *
         * @param status        Indicates status of the subscribe
         */
        virtual void StatusChanged(Status status) {}

        /**
         * @brief Notification callback to provide sampled metrics
         *
         * @details Callback will be triggered on Config::metrics_sample_ms to provide the sampled data based
         *      on the sample period.  After this callback, the period/sample based metrics will reset and start over
         *      for the new period.
         *
         * @param metrics           Copy of the subscribed metrics for the sample period
         */
        virtual void MetricsSampled(const SubscribeTrackMetrics&& metrics) {}

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
        bool only_complete_objects_;
    };

} // namespace moq::transport
