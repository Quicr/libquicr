// SPDX-FileCopyrightText: Copyright (c) 2024 Cisco Systems
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

#include <functional>
#include <quicr/detail/base_track_handler.h>
#include <quicr/detail/messages.h>
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

            kPaused,
            kPendingPublishOk
        };

        /**
         * @brief  Status codes for the publish track
         *
         * @note kOk is not the only status that means it is okay to publish.
         *      CanPublish() method should be used to determine if the status is okay to still publish or not.
         *
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
            kPendingPublishOk,
            kPaused,
        };

      protected:
        /**
         * @brief Publish track handler constructor
         *
         * @param full_track_name       Full track name
         * @param track_mode            The track mode to operate using
         * @param default_priority      Default priority for objects if not specified in ObjectHeaderss
         * @param default_ttl           Default TTL for objects if not specified in ObjectHeaderss
         * @param stream_mode           Stream to use when track mode is kStream.
         */
        PublishTrackHandler(const FullTrackName& full_track_name,
                            TrackMode track_mode,
                            uint8_t default_priority,
                            uint32_t default_ttl,
                            std::optional<messages::StreamHeaderProperties> stream_mode = std::nullopt)
          : BaseTrackHandler(full_track_name)
          , default_track_mode_(track_mode)
          , default_priority_(default_priority)
          , default_ttl_(default_ttl)
        {
            switch (track_mode) {
                case TrackMode::kDatagram:
                    if (stream_mode.has_value()) {
                        throw std::invalid_argument("Datagram track mode should not specify a stream mode");
                    }
                    break;
                case TrackMode::kStream:
                    if (stream_mode.has_value()) {
                        stream_mode_.emplace(*stream_mode);
                    } else {
                        stream_mode_.emplace(true, messages::SubgroupIdType::kExplicit, false, false);
                    }
                    break;
            }
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

        // TODO: Is this all the info needed for an alias calculation?

        // --------------------------------------------------------------------------
        // Public Virtual API callback event methods
        // --------------------------------------------------------------------------
        /** @name Callbacks
         */
        ///@{

        /**
         * @brief Notification of publish track status change
         * @details Notification of a change to  publish track status, such as
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

        void RequestUpdate(uint64_t request_id, const messages::Parameters& params) override;
        void RequestOk(uint64_t request_id, const messages::Parameters& params) override;

        ///@}

        // --------------------------------------------------------------------------
        // Various getter/setters
        // --------------------------------------------------------------------------
        /**
         * @brief set/update the default priority for published objects
         */
        void SetDefaultPriority(const uint8_t priority) noexcept { default_priority_ = priority; }

        /**
         * @brief Get the default priority for published objects.
         * @return The default priority.
         */
        constexpr uint8_t GetDefaultPriority() const noexcept { return default_priority_; }

        /**
         * @brief set/update the default TTL expiry for published objects
         */
        void SetDefaultTTL(const uint32_t ttl) noexcept { default_ttl_ = ttl; }

        /**
         * @brief set/update the default track mode for objects
         */
        void SetDefaultTrackMode(const TrackMode track_mode) noexcept { default_track_mode_ = track_mode; }

        /**
         * @brief Get the current stream mode.
         * @return The current stream mode.
         */
        constexpr std::optional<messages::StreamHeaderProperties> GetStreamMode() const noexcept
        {
            return stream_mode_;
        }

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
         * @brief Check if the state allows publishing or not
         *
         * @return true to indicate that the publisher can publish, false if the publisher cannot
         */
        constexpr bool CanPublish() const noexcept
        {
            switch (publish_status_) {
                case Status::kOk:
                case Status::kNewGroupRequested:
                case Status::kSubscriptionUpdated:
                    return true;
                default:
                    return false;
            }
        }

        /**
         * @brief Set the track alias
         *
         * @param track_alias       MoQ track alias for track namespace+name
         */
        void SetTrackAlias(uint64_t track_alias) { track_alias_ = track_alias; }

        /**
         * @brief Get the track alias
         *
         * @details If the track alias is set, it will be returned, otherwise std::nullopt.
         *
         * @return Track alias if set, otherwise std::nullopt.
         */
        std::optional<uint64_t> GetTrackAlias() const noexcept { return track_alias_; }

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
         * @param group_id            Group ID for stream
         * @param subgroup_id         Subgroup ID for stream
         * @param data                MoQ data to send
         *
         * @return Publish status on forwarding the data
         */
        PublishObjectStatus ForwardPublishedData(bool is_new_stream,
                                                 uint64_t group_id,
                                                 uint64_t subgroup_id,
                                                 std::shared_ptr<const std::vector<uint8_t>> data);

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

        /**
         * @brief Ends the subgroup as completed or not.
         *
         * @details APP MUST call this to end subgroups, otherwise they will linger. If
         *      completed is true, the subgroups will be closed after last message has
         *      been delivered.
         *
         * @param group_id
         * @param subgroup_id
         * @param completed
         */
        void EndSubgroup(uint64_t group_id, uint64_t subgroup_id, bool completed = true);

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
      protected:
        /**
         * @brief Set the publish status
         * @param status                Status of publishing (aka publish objects)
         */
        void SetStatus(Status status) noexcept
        {
            if (publish_status_ == status) {
                return;
            }

            publish_status_ = status;
            StatusChanged(status);
        }

        // --------------------------------------------------------------------------
        // Member variables
        // --------------------------------------------------------------------------
        Status publish_status_{ Status::kNotAnnounced };
        TrackMode default_track_mode_;
        std::optional<messages::StreamHeaderProperties> stream_mode_;
        uint8_t default_priority_; // Set by caller and is used when priority is not specified
        uint32_t default_ttl_;     // Set by caller and is used when TTL is not specified

        uint64_t publish_data_ctx_id_; // set by the transport; publishing data context ID

        struct StreamInfo
        {
            uint64_t stream_id{ 0 };
            uint64_t last_group_id{ 0 };
            uint64_t last_subgroup_id{ 0 };
            std::optional<uint64_t> last_object_id;
        };

        std::map<std::uint64_t, std::map<std::uint64_t, StreamInfo>> stream_info_by_group_;

        std::optional<uint64_t> track_alias_;

        messages::Location largest_location_{ 0, 0 };

        Bytes object_msg_buffer_; // TODO(tievens): Review shrink/resize

        bool support_new_group_request_{ true };
        std::optional<uint64_t> pending_new_group_request_id_;

        friend class Transport;
        friend class Client;
        friend class Server;
    };

} // namespace moq
