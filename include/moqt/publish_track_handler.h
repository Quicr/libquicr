/*
 *  Copyright (C) 2024
 *  Cisco Systems, Inc.
 *  All Rights Reserved
 */
#pragma once

#include <functional>
#include <moqt/core/base_track_handler.h>

namespace moq::transport {

    /**
     * @brief MOQ track handler for published track
     *
     * @details MOQ publish track handler defines all track related callbacks and
     *  functions for publish. Track handler operates on a single track (namespace + name).
     *
     *  This extends the base track handler to add publish (aka send) handling
     */
    class PublishTrackHandler : protected BaseTrackHandler
    {
      public:
        friend class Transport;

        /**
         * @brief Publish status codes
         */
        enum class PublishStatus : uint8_t
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
            kObjectDataIncomplete,              ///< PublishObject() was called when continuation data remains

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
            kOK = 0,
            kNotConnected,
            kNotAnnounced,
            kPendingAnnounceResponse,
            kAnnounceNotAuthorized,
            kNoSubscribers,
            kSendingUnannounce,
        };


        // --------------------------------------------------------------------------
        // Public API methods that normally should not be overridden
        // --------------------------------------------------------------------------

        /**
         * @brief Publish track handler constructor
         *
         * @param full_track_name       Full track name struct
         * @param track_mode            The track mode to operate using
         * @param default_priority      Default priority for objects if not specified in ObjectHeaderss
         * @param default_ttl           Default TTL for objects if not specified in ObjectHeaderss
         */
        PublishTrackHandler(const FullTrackName& full_track_name,
                            TrackMode track_mode,
                            uint8_t default_priority,
                            uint32_t default_ttl)
          : BaseTrackHandler(full_track_name)
          , track_mode_(track_mode)
          , def_priority_(default_priority)
          , def_ttl_(default_ttl)
        {
        }

        // --------------------------------------------------------------------------
        // Public Virtual API callback event methods to be overridden
        // --------------------------------------------------------------------------

        /**
         * @brief Notification of publish status
         * @details Notification of publishing status, such as when it's ready to publish or not ready to publish
         *
         * @param status        Indicates the status of being able to publish
         */
        virtual void StatusChanged(PublishTrackHandler::Status status) = 0;

        // --------------------------------------------------------------------------
        // Various getter/setters
        // --------------------------------------------------------------------------
        /**
         * @brief set/update the default priority for published objects
         */
        void SetDefaultPriority(uint8_t priority) { def_priority_ = priority; }

        /**
         * @brief set/update the default TTL expiry for published objects
         */
        void SetDefaultTTL(uint32_t ttl) { def_ttl_ = ttl; }

        /**
         * @brief Get the publish status
         *
         * @return Status of publish
         */
        Status GetStatus() { return publish_status_; }

        // --------------------------------------------------------------------------
        // Methods that normally do not need to be overridden
        // --------------------------------------------------------------------------

        /**
         * @brief Publish object to the announced track
         *
         * @details Publish an object to announced track that was previously announced.
         *   This will have an error if the track wasn't announced yet. Status will
         *   indicate if there are no subscribers. In this case, the object will
         *   not be sent.
         *
         *   **Restrictions:**
         *   - This method cannot be called twice with the same object header group and object IDs.
         *   - In TrackMode::kStreamPerGroup, ObjectHeaders::group_id **MUST** be different than the previous
         *     when calling this method when the previous has not been completed yet using PublishContinuationData().
         *     If group id is not different, the Error::kPreviousObjectNotCompleteMustStartNewGroup will be returned
         *     and the object will not be sent. If new group ID is provided, then the previous object will terminate
         *     with stream closure, resulting in the previous being truncated.
         *   - In TrackMode::kStreamPerTrack, this method **CANNOT** be called until the previous object has been
         *     completed using PublishContinuationData(). Calling this method before completing the previous
         *     object remaining data will result in PublishStatus::kObjectDataIncomplete. No data would be sent and the
         *     stream would remain unchanged. It is expected that the caller would send the remaining continuation data.
         *
         * @note If data is less than object_header.payload_length, then PublishObject()
         *   should be called to send the remaining data.
         *
         * @param object_headers        Object headers, must include group and object Ids
         * @param data                  Payload data for the object, must be <= object_headers.payload_length
         *
         * @returns Publish status of the publish
         *    * PublishStatus::kObjectContinuationDataNeeded if the object payload data is not completed but it was sent,
         *    * PublishStatus::kObjectDataComplete if the object data was sent and the data is complete,
         *    * other track status
         */
        PublishStatus PublishObject(const ObjectHeaders& object_headers, BytesSpan data);

        /**
         * @brief Publish continuation data
         *
         * @details Publish continuation data for the previous PublishObject(). The data should be the
         *      remaining data of the object payload size.
         *
         * @param data                  Remaining payload data for the object being published. The length cannot exceed
         *                              the original object payload size.
         *
         * @returns
         *      * PublishStatus::kObjectDataComplete if the data fulfills the remaining data,
         *      * PublishStatus::kObjectContinuationDataNeeded if more data is still needed,
         *      * PublishStatus::kObjectDataTooLarge if the data provided would exceed the remaining amount of data,
         *      * PublishStatus::kNoPreviousObject if the previous object was completed or if there was no previous object,
         *      * other track status
         */
        PublishStatus PublishContinuationData(BytesSpan data);

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
         * @param data                  Raw data/object that should be transmitted - MoQInstance serializes the data
         */
        using PublishObjFunction = std::function<PublishStatus(uint8_t priority,
                                                               uint32_t ttl,
                                                               bool stream_header_needed,
                                                               uint64_t group_id,
                                                               uint64_t object_id,
                                                               BytesSpan data)>;
        /**
         * @brief Set the Data context ID
         *
         * @details The MOQ Handler sets the data context ID
         */
        void SetDataContextId(uint64_t data_ctx_id) { publish_data_ctx_id_ = data_ctx_id; };

        /**
         * @brief Get the Data context ID
         */
        uint64_t GetDataContextId() { return publish_data_ctx_id_; };

        void SetPublishObjectFunction(PublishObjFunction&& publish_func)
        {
            publish_object_func_ = std::move(publish_func);
        }

        /**
         * @brief Set the publish status
         * @param status                Status of publishing (aka publish objects)
         */
        void SetStatus(Status status) { publish_status_ = status; }

        // --------------------------------------------------------------------------
        // Member variables
        // --------------------------------------------------------------------------
        Status publish_status_{ Status::kNotAnnounced };
        TrackMode track_mode_;
        uint8_t def_priority_;              // Set by caller and is used when priority is not specified
        uint32_t def_ttl_;                  // Set by caller and is used when TTL is not specified

        uint64_t publish_data_ctx_id_;      // publishing data context ID
        PublishObjFunction publish_object_func_;

        uint64_t object_payload_remaining_length_ { 0 };
        bool sent_track_header_ { false };  // Used only in stream per track mode
    };

} // namespace moq::transport