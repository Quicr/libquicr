#pragma once

#include "quicr/detail/base_track_handler.h"
#include "quicr/track_name.h"

#include <span>
#include <vector>

namespace quicr {
    class PublishNamespaceHandler : public BaseTrackHandler
    {
      public:
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

            kPaused,
            kPendingPublishOk
        };

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
            kPendingPublishOk,
            kPaused,
        };

      private:
        PublishNamespaceHandler(const TrackNamespace& prefix)
          : BaseTrackHandler({ prefix, {} })
          , prefix_{ prefix }
        {
        }

      public:
        static std::shared_ptr<PublishNamespaceHandler> Create(const TrackNamespace& prefix)
        {
            return std::shared_ptr<PublishNamespaceHandler>(new PublishNamespaceHandler(prefix));
        }

        /**
         * @brief Get the track alias
         *
         * @details If the track alias is set, it will be returned, otherwise std::nullopt.
         *
         * @return Track alias if set, otherwise std::nullopt.
         */
        const TrackNamespace& GetPrefix() const noexcept { return prefix_; }

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
        virtual PublishObjectStatus PublishObject(const ObjectHeaders& object_headers, BytesSpan data);

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

      private:
        const TrackNamespace prefix_;
    };
}
