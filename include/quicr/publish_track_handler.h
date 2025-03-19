// SPDX-FileCopyrightText: Copyright (c) 2024 Cisco Systems
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

#include <functional>
#include <quicr/detail/base_track_handler.h>
#include <quicr/metrics.h>
#include <quicr/object.h>

namespace quicr {

    /**
     * @brief MOQ track handler for published track
     *
     * @details MOQ publish track handler defines all track related callbacks and
     *  functions for publish. Track handler operates on a single track (namespace + name).
     *
     *  This extends the base track handler to add publish (aka send) handling
     */
    class PublishTrackHandler : public BaseTrackHandler
    {
      public:
        /**
         * @brief Publish status codes
         */
        enum class PublishObjectStatus : uint8_t
        {
            kOk = 0,
            kInternalError,
            kNotAuthorized,
            kNotAnnounced,
            kNoSubscribers,
            kObjectPayloadLengthExceeded,
            kPreviousObjectTruncated,

            kNoPreviousObject,
            kObjectDataComplete,
            kObjectContinuationDataNeeded,
            kObjectDataIncomplete, ///< PublishObject() was called when continuation data remains

            /// Indicates that the published object data is too large based on the object header payload size plus
            /// any/all data that was sent already.
            kObjectDataTooLarge,

            /// Previous object payload has not been completed and new object cannot start in per-group track mode
            /// unless new group is used.
            kPreviousObjectNotCompleteMustStartNewGroup,

            /// Previous object payload has not been completed and new object cannot start in per-track track mode
            /// without creating a new track. This requires to unpublish and to publish track again.
            kPreviousObjectNotCompleteMustStartNewTrack,
        };

        /**
         * @brief  Status codes for the publish track
         */
        enum class Status : uint8_t
        {
            kOk = 0,
            kNotConnected,
            kNotAnnounced,
            kPendingAnnounceResponse,
            kAnnounceNotAuthorized,
            kNoSubscribers,
            kSendingUnannounce, ///< In this state, callbacks will not be called
            kSubscriptionUpdated,
            kNewGroupRequested,
        };

      protected:
        /**
         * @brief Publish track handler constructor
         *
         * @param full_track_name       Full track name
         * @param track_mode            The track mode to operate using
         * @param default_priority      Default priority for objects if not specified in ObjectHeaderss
         * @param default_ttl           Default TTL for objects if not specified in ObjectHeaderss
         */
        PublishTrackHandler(const FullTrackName& full_track_name,
                            TrackMode track_mode,
                            uint8_t default_priority,
                            uint32_t default_ttl)
          : BaseTrackHandler(full_track_name)
          , default_track_mode_(track_mode)
          , default_priority_(default_priority)
          , default_ttl_(default_ttl)
        {
        }

      public:
        /**
         * @brief Create a shared Publish track handler
         *
         * @param full_track_name       Full track name
         * @param track_mode            The track mode to operate using
         * @param default_priority      Default priority for objects if not specified in ObjectHeaderss
         * @param default_ttl           Default TTL for objects if not specified in ObjectHeaderss
         */
        static std::shared_ptr<PublishTrackHandler> Create(const FullTrackName& full_track_name,
                                                           TrackMode track_mode,
                                                           uint8_t default_priority,
                                                           uint32_t default_ttl)
        {
            return std::shared_ptr<PublishTrackHandler>(
              new PublishTrackHandler(full_track_name, track_mode, default_priority, default_ttl));
        }

        // --------------------------------------------------------------------------
        // Public Virtual API callback event methods
        // --------------------------------------------------------------------------
        /** @name Callbacks
         */
        ///@{

        /**
         * @brief Notification of publish track status change
         * @details Notification of a change to publish track status, such as
         *      when it's ready to publish or not ready to publish
         *
         * @param status        Indicates the status of being able to publish
         */
        virtual void StatusChanged(Status status);

        /**
         * @brief Notification callback to provide sampled metrics
         *
         * @details Callback will be triggered on Config::metrics_sample_ms to provide the sampled data based
         *      on the sample period.  After this callback, the period/sample based metrics will reset and start over
         *      for the new period.
         *
         * @param metrics           Copy of the published metrics for the sample period
         */
        virtual void MetricsSampled(const PublishTrackMetrics& metrics);

        ///@}

        // --------------------------------------------------------------------------
        // Various getter/setters
        // --------------------------------------------------------------------------
        /**
         * @brief set/update the default priority for published objects
         */
        void SetDefaultPriority(const uint8_t priority) noexcept { default_priority_ = priority; }

        /**
         * @brief set/update the default TTL expiry for published objects
         */
        void SetDefaultTTL(const uint32_t ttl) noexcept { default_ttl_ = ttl; }

        /**
         * @brief set/update the default track mode for objects
         */
        void SetDefaultTrackMode(const TrackMode track_mode) noexcept { default_track_mode_ = track_mode; }

        /**
         * @brief Get the publish status
         *
         * @return Status of publish
         */
        constexpr Status GetStatus() const noexcept { return publish_status_; }

        // --------------------------------------------------------------------------
        // Methods that normally do not need to be overridden
        // --------------------------------------------------------------------------

        /**
         * @brief Publish [full] object
         *
         * @details Publish a full object. If not announced, it will be announced. Status will
         *   indicate if there are no subscribers. In this case, the object will
         *   not be sent.
         *
         * @note If data is less than ObjectHeaders::payload_length, the error kObjectDataIncomplete
         *   will be returned and the object will not be sent.
         *
         *   **Restrictions:**
         *   - This method cannot be called twice with the same object header group and object IDs.
         *   - In TrackMode::kStreamPerGroup, ObjectHeaders::group_id **MUST** be different than the previous
         *     when calling this method when the previous has not been completed yet using PublishContinuationData().
         *     If group id is not different, the PublishStatus::kPreviousObjectNotCompleteMustStartNewGroup will be
         *     returned and the object will not be sent. If new group ID is provided, then the previous object will
         *     terminate with stream closure, resulting in the previous being truncated.
         *   - In TrackMode::kStreamPerTrack, this method **CANNOT** be called until the previous object has been
         *     completed using PublishContinuationData(). Calling this method before completing the previous
         *     object remaining data will result in PublishStatus::kObjectDataIncomplete. No data would be sent and the
         *     stream would remain unchanged. It is expected that the caller would send the remaining continuation data.
         *
         * @param object_headers        Object headers, must include group and object Ids
         * @param data                  Full complete payload data for the object
         *
         * @returns Publish status of the publish
         */
        PublishObjectStatus PublishObject(const ObjectHeaders& object_headers, BytesSpan data);

        /**
         * @brief Forward received object data to subscriber/relay/remote client
         *
         * @details This method is similar to PublishObject except that the data forwarded is byte array
         *    level data, which should have already been encoded upon receive from the origin publisher. Relays
         *    implement this method to forward bytes received to subscriber connection.
         *
         * @param is_new_stream       Indicates if this data starts a new stream
         * @param data                MoQ data to send
         * @return Publish status on forwarding the data
         */
        PublishObjectStatus ForwardPublishedData(bool is_new_stream, std::shared_ptr<const std::vector<uint8_t>> data);

        /**
         * @brief Publish object to the announced track
         *
         * @details Publish a partial object. If not announced, it will be announced. Status will
         *   indicate if there are no subscribers. In this case, the object will
         *   not be sent.
         *
         *   **Restrictions:**
         *   - In TrackMode::kStreamPerGroup, /::group_id **MUST** be different than the previous
         *     when calling this method when the previous has not been completed yet using PublishContinuationData().
         *     If group id is not different, the PublishStatus::kPreviousObjectNotCompleteMustStartNewGroup will be
         *     returned and the object will not be sent. If new group ID is provided, then the previous object will
         *     terminate with stream closure, resulting in the previous being truncated.
         *   - In TrackMode::kStreamPerTrack, this method **CANNOT** be called until the previous object has been
         *     completed using PublishContinuationData(). Calling this method before completing the previous
         *     object remaining data will result in PublishStatus::kObjectDataIncomplete. No data would be sent and the
         *     stream would remain unchanged. It is expected that the caller would send the remaining continuation data.
         *
         * @note If data is less than ObjectHeaders::payload_length, then PublishObject()
         *   should be called to send the remaining data.
         *
         * @param object_headers        Object headers, must include group and object Ids
         * @param data                  Payload data for the object, must be <= object_headers.payload_length
         *
         * @returns Publish status of the publish
         *    * PublishStatus::kObjectContinuationDataNeeded if the object payload data is not completed but it was
         * sent,
         *    * PublishStatus::kObjectDataComplete if the object data was sent and the data is complete,
         *    * other PublishStatus
         */
        PublishObjectStatus PublishPartialObject(const ObjectHeaders& object_headers, BytesSpan data);

        // --------------------------------------------------------------------------
        // Metrics
        // --------------------------------------------------------------------------

        /**
         * @brief Publish metrics for the track
         *
         * @details Publish metrics are updated real-time and transport quic metrics on metrics_sample_ms
         *     period.
         */
        PublishTrackMetrics publish_track_metrics_;

        // --------------------------------------------------------------------------
        // Internals
        // --------------------------------------------------------------------------
      private:
        /**
         * @brief Publish Object function via the MoQ instance
         *
         * @details This is set by the MoQInstance.
         *   Publish object function provides direct access to the MoQInstance that will publish
         *   the object based on the track mode.
         *
         * @param priority              Priority to use for object; set on next track change
         * @param ttl                   Expire time to live in milliseconds
         * @param stream_header_needed  Indicates if group or track header is needed before this data object
         * @param group_id              The ID of the group the message belongs to
         * @param object_id             The ID of the object this message represents
         * @param extensions            Extensions to send along with the message
         * @param data                  Raw data/object that should be transmitted - MoQInstance serializes the data
         */
        using PublishObjFunction = std::function<PublishObjectStatus(uint8_t priority,
                                                                     uint32_t ttl,
                                                                     bool stream_header_needed,
                                                                     uint64_t group_id,
                                                                     uint64_t subgroup_id,
                                                                     uint64_t object_id,
                                                                     std::optional<Extensions> extensions,
                                                                     BytesSpan data)>;

        /**
         * @brief Forward data function via the MoQ instance
         *
         * @details This is set by the MoQInstance.
         *   Forward data function provides direct access to the MoQInstance that will forward data
         *   based on the parameters passed.
         *
         * @param priority              Priority to use for object; set on next track change
         * @param ttl                   Expire time to live in milliseconds
         * @param stream_header_needed  Indicates if group or track header is needed before this data object
         * @param data                  MoQ Serialized data to write to the remote connection/stream
         */
        using ForwardDataFunction =
          std::function<PublishObjectStatus(uint8_t priority,
                                            uint32_t ttl,
                                            bool stream_header_needed,
                                            std::shared_ptr<const std::vector<uint8_t>> data)>;

        /**
         * @brief Set the Data context ID
         *
         * @details The MOQ Handler sets the data context ID
         */
        void SetDataContextId(uint64_t data_ctx_id) noexcept { publish_data_ctx_id_ = data_ctx_id; };

        /**
         * @brief Get the Data context ID
         */
        constexpr uint64_t GetDataContextId() const noexcept { return publish_data_ctx_id_; };

        /**
         * @brief Set the publish status
         * @param status                Status of publishing (aka publish objects)
         */
        void SetStatus(Status status) noexcept
        {
            publish_status_ = status;
            StatusChanged(status);
        }

        // --------------------------------------------------------------------------
        // Member variables
        // --------------------------------------------------------------------------
        Status publish_status_{ Status::kNotAnnounced };
        TrackMode default_track_mode_;
        uint8_t default_priority_; // Set by caller and is used when priority is not specified
        uint32_t default_ttl_;     // Set by caller and is used when TTL is not specified

        uint64_t publish_data_ctx_id_;                  // set byte the transport; publishing data context ID
        PublishObjFunction publish_object_func_;        // set by the transport
        ForwardDataFunction forward_publish_data_func_; // set by the transport

        uint64_t prev_object_group_id_{ 0 };
        uint64_t prev_sub_group_id_{ 0 };
        uint64_t prev_object_id_{ 0 };
        uint64_t object_payload_remaining_length_{ 0 };
        bool sent_first_header_{ false }; // Used to indicate if the first stream has sent the header or not

        Bytes object_msg_buffer_; // TODO(tievens): Review shrink/resize

        friend class Transport;
        friend class Client;
        friend class Server;
    };

} // namespace moq
