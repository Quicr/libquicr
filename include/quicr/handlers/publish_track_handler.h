// SPDX-FileCopyrightText: Copyright (c) 2024 Cisco Systems
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

#include "quicr/handlers/track_handler.h"
#include "quicr/messages/message_serialisation.h"
#include "quicr/messages/object.h"
#include "quicr/metrics.h"

#include <atomic>
#include <functional>

namespace quicr {

    /**
     * @brief MOQ track handler for published track
     *
     * @details Publishes one track using unreliable object datagrams (`TrackMode::kDatagram`) or
     * reliable streams with one stream per subgroup (`TrackMode::kStream`). Despite older
     * terminology, there are no stream-per-group or stream-per-track modes. Publishing methods and
     * mutable configuration are not internally serialized; callers must not invoke them concurrently.
     */
    class PublishTrackHandler : public TrackHandler
    {
      public:
        /**
         * @brief Publish status codes
         */
        enum class PublishObjectStatus : uint8_t
        {
            kOk = 0,                      ///< Object or bytes were queued successfully.
            kInternalError,               ///< Required session/stream state was unavailable or invalid.
            kNotAuthorized,               ///< Peer denied publication.
            kNotAnnounced,                ///< Track is not ready/connected/announced.
            kNoSubscribers,               ///< No active subscriber/request ID exists.
            kObjectPayloadLengthExceeded, ///< Legacy continuation status; not returned by current implementation.
            kPreviousObjectTruncated,     ///< Legacy continuation status; not returned by current implementation.

            kNoPreviousObject,             ///< Legacy continuation status; not returned by current implementation.
            kObjectDataComplete,           ///< Legacy continuation status; not returned by current implementation.
            kObjectContinuationDataNeeded, ///< Legacy continuation status; not returned by current implementation.
            kObjectDataIncomplete,         ///< PublishObject() was called when continuation data remains

            /// Indicates that the published object data is too large based on the object header payload size plus
            /// any/all data that was sent already. Legacy continuation status; not returned by current implementation.
            kObjectDataTooLarge,

            /// Previous object payload has not been completed and new object cannot start in per-group track mode
            /// unless new group is used. Legacy status; current subgroup streams are independent.
            kPreviousObjectNotCompleteMustStartNewGroup,

            /// Previous object payload has not been completed and new object cannot start in per-track track mode
            /// without creating a new track. This requires to unpublish and to publish track again. Legacy status; not
            /// returned by current implementation.
            kPreviousObjectNotCompleteMustStartNewTrack,

            kPaused,          ///< Forwarding is paused by subscription state.
            kPendingPublishOk ///< A publisher-initiated PUBLISH awaits acknowledgement.
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
            kOk = 0,                  ///< Publication is ready to queue objects.
            kNotConnected,            ///< Transport is disconnected.
            kNotAnnounced,            ///< Track is not announced/active.
            kPendingAnnounceResponse, ///< Waiting for announce acceptance.
            kAnnounceNotAuthorized,   ///< Peer denied the announcement.
            kNoSubscribers,           ///< No subscriber is currently bound.
            kUnsubscribed,            ///< Subscriber ended the subscription.
            kDoneByFin,               ///< Request stream ended normally.
            kSendingUnannounce,       ///< In this state, callbacks will not be called
            kSubscriptionUpdated,     ///< Subscription parameters changed; next publish normalizes to `kOk`.
            kNewGroupRequested,       ///< Subscriber requested a new group; next publish normalizes to `kOk`.
            kPendingPublishOk,        ///< Waiting for acknowledgement of publisher-initiated publish.
            kPaused,                  ///< Subscriber disabled forwarding.
        };

      protected:
        /**
         * @brief Publish track handler constructor
         *
         * @param full_track_name Full track name
         * @param track_mode The track mode to operate using
         * @param publisher_priority Publisher priority of origin publisher
         * @param publish_object_priority Publish object priority for objects being sent (set by Subscriber priority)
         * @param default_ttl Default transport queue lifetime when object headers omit TTL.
         * @param stream_mode Stream to use when track mode is kStream.
         * @param largest_location Largest location to start the handler
         * @throws std::invalid_argument if datagram mode is combined with stream properties.
         * @details Stream mode defaults to explicit subgroup IDs with the library's standard
         * subgroup framing properties when `stream_mode` is omitted.
         */
        PublishTrackHandler(const FullTrackName& full_track_name,
                            TrackMode track_mode,
                            uint8_t publisher_priority,
                            uint8_t publish_object_priority,
                            uint32_t default_ttl,
                            std::optional<messages::StreamHeaderProperties> stream_mode = std::nullopt,
                            messages::Location largest_location = { 0, 0 });

      public:
        /**
         * @brief Create a shared Publish track handler
         *
         * @param full_track_name Full track name
         * @param track_mode The track mode to operate using
         * @param publisher_priority Publisher priority of origin publisher
         * @param publish_object_priority Publish object priority for objects being sent
         * @param default_ttl Default TTL for objects if not specified in ObjectHeaderss
         * @param largest_location Largest location to start handler at
         * @return Shared ownership of the new handler.
         */
        static std::shared_ptr<PublishTrackHandler> Create(const FullTrackName& full_track_name,
                                                           TrackMode track_mode,
                                                           uint8_t publisher_priority,
                                                           uint8_t publish_object_priority,
                                                           uint32_t default_ttl,
                                                           messages::Location largest_location)
        {
            return std::shared_ptr<PublishTrackHandler>(new PublishTrackHandler(full_track_name,
                                                                                track_mode,
                                                                                publisher_priority,
                                                                                publish_object_priority,
                                                                                default_ttl,
                                                                                std::nullopt,
                                                                                largest_location));
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
         * @param status Indicates the status of being able to publish
         * @note Default implementation does nothing. `SetStatus()` suppresses this callback when
         * the atomic status value is unchanged.
         */
        virtual void StatusChanged(Status status);

        /**
         * @brief Notification callback to provide sampled metrics
         *
         * @details Called at the connection metrics cadence, including idle periods. Object, byte,
         * drop, and most QUIC counters are cumulative snapshots; only QUIC min/max/average
         * aggregates are cleared after the callback for the next period.
         *
         * @param metrics Copy of the published metrics for the sample period
         * @note The reference is borrowed for the callback. Copy it to retain a stable snapshot.
         */
        virtual void MetricsSampled(const PublishTrackMetrics& metrics);

        /**
         * @brief Applies subscriber forwarding and new-group update parameters.
         * @param params Borrowed update parameters.
         * @return Immediate success with empty acknowledgement parameters.
         * @details A present forwarding value changes status to `kOk` or `kPaused`; a present
         * new-group request then changes it to `kNewGroupRequested`. Other parameters are currently ignored.
         */
        Reply<messages::Parameters, ErrorCode> RequestUpdateReceived(const messages::Parameters& params) override;

        ///@}

        // --------------------------------------------------------------------------
        // Various getter/setters
        // --------------------------------------------------------------------------
        /**
         * @brief Sets the publisher priority used in future datagram and subgroup headers.
         * @param priority New protocol priority; already-open subgroup headers are unchanged.
         */
        void SetPublisherPriority(const uint8_t priority) noexcept { publisher_priority_ = priority; }

        /**
         * @brief set/update the publish object priority
         * @details Publish object priority is set by subscriber priority, but the client app can
         *      update this as needed at any time. The setting will take effect on next datagram
         *      object and on next start of group/subgroup
         * @param priority New transport scheduling priority.
         */
        void SetPublishObjectPriority(const uint8_t priority) noexcept { publish_object_priority_ = priority; }

        /**
         * @brief Get the publisher priority
         * @return Priority encoded as the publisher priority.
         */
        constexpr uint8_t GetPublisherPriority() const noexcept { return publisher_priority_; }

        /**
         * @brief Get the publish object priority
         * @return Priority used to schedule newly enqueued object data.
         */
        constexpr uint8_t GetPublishObjectPriority() const noexcept { return publish_object_priority_; }

        /**
         * @brief Sets the default transport queue lifetime for future objects.
         * @param ttl TTL used when `ObjectHeaders::ttl` is absent.
         */
        void SetDefaultTTL(const uint32_t ttl) noexcept { default_ttl_ = ttl; }

        /**
         * @brief Gets the default transport queue lifetime.
         * @return Configured default TTL.
         */
        uint32_t GetDefaultTTL() const noexcept { return default_ttl_; }

        /**
         * @brief Changes framing mode for subsequent objects.
         * @param track_mode Datagram or subgroup-stream framing.
         * @warning Changing modes while subgroup streams are open does not close or migrate them;
         * callers must coordinate mode transitions.
         */
        void SetDefaultTrackMode(const TrackMode track_mode) noexcept { default_track_mode_ = track_mode; }

        /**
         * @brief Get the current stream mode.
         * @return Default subgroup framing properties in stream mode, or `std::nullopt` in datagram mode.
         */
        constexpr std::optional<messages::StreamHeaderProperties> GetStreamMode() const noexcept
        {
            return stream_mode_;
        }

        /**
         * @brief Get the publish status
         *
         * @return Atomically loaded publication status. Other mutable handler state is not made
         * safe by this atomic accessor.
         */
        Status GetStatus() const noexcept { return publish_status_.load(std::memory_order_acquire); }

        /**
         * @brief Gets the largest/current location observed by publication.
         * @return Stored location. The group advances monotonically; within the current group the
         * object component is the most recently published object ID.
         */
        constexpr messages::Location GetLargestLocation() const noexcept { return largest_location_; }

        /**
         * @brief Notification that a stream has been closed.
         * @param stream_id The ID of the stream being closed.
         * @param reset True if stream closed by reset
         * @details Removes matching subgroup bookkeeping. The default implementation does not
         * change publication status and currently ignores `reset`.
         */
        virtual void StreamClosed(std::uint64_t stream_id, bool reset = false);

        // --------------------------------------------------------------------------
        // Methods that normally do not need to be overridden
        // --------------------------------------------------------------------------

        /**
         * @brief Check if the state allows publishing or not
         *
         * @return `true` only for `kOk`, `kNewGroupRequested`, or `kSubscriptionUpdated`.
         */
        bool CanPublish() const noexcept
        {
            switch (GetStatus()) {
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
         * @param track_alias MoQ track alias for this connection.
         * @note Must be set before successful publication; publication dereferences the alias.
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
         * @details Encodes and queues one complete object. Datagram mode emits one object datagram.
         * Stream mode creates/reuses the stream identified by `(group_id, subgroup_id)`, emits a
         * subgroup header on first use, and delta-encodes subsequent object IDs. The implementation
         * does not compare `data.size()` with `payload_length`; `data` is the payload that is encoded.
         * A per-object `track_mode` changes the default mode before encoding subsequent framing,
         * but mode changes while streams are open require application coordination.
         *
         * @param object_headers Object headers, must include group and object Ids
         * @param data Full complete payload data for the object
         * @param stream_mode Subgroup header properties
         *
         * @return Publication result; unsuccessful state checks increment
         * `publish_track_metrics_.objects_dropped_not_ok`, except a missing request ID.
         * @throws TransportException if enqueueing fails.
         * @throws std::bad_optional_access if required track alias or stream properties were not set.
         * @note `data` is borrowed and copied into an owned queue buffer before return. Calls must
         * be externally serialized.
         */
        virtual PublishObjectStatus PublishObject(
          const ObjectHeaders& object_headers,
          BytesSpan data,
          std::optional<messages::StreamHeaderProperties> stream_mode = std::nullopt);

        /**
         * @brief Forward received object data to subscriber/relay/remote client
         *
         * @details This method is similar to PublishObject except that the data forwarded is byte array
         *    level data, which should have already been encoded upon receive from the origin publisher. Relays
         *    implement this method to forward bytes received to subscriber connection.
         *
         * @param is_new_stream Indicates if this data starts a new stream
         * @param group_id Group ID for stream
         * @param subgroup_id Subgroup ID for stream
         * @param data MoQ data to send
         *
         * @return Publication result. In stream mode, `is_new_stream == true` currently returns
         * `kObjectDataIncomplete`; the caller must first establish stream bookkeeping through
         * `PublishObject`. Missing subgroup bookkeeping returns `kInternalError`.
         * @throws TransportException if enqueueing fails.
         * @details Shared ownership of `data` is passed to the transport without copying. In
         * datagram mode bytes are sent unreliably as already encoded; in stream mode they are
         * appended to the existing subgroup stream.
         */
        PublishObjectStatus ForwardPublishedData(bool is_new_stream,
                                                 uint64_t group_id,
                                                 uint64_t subgroup_id,
                                                 std::shared_ptr<const std::vector<uint8_t>> data);

        /**
         * @brief Ends the subgroup as completed or not.
         *
         * @details Applications must call this to release an open stream-mode subgroup. With
         * `completed == true`, an empty close marker is queued after existing data; otherwise the
         * stream is reset immediately. The subgroup is then removed from local bookkeeping.
         * Silently does nothing without a live session or matching open subgroup.
         *
         * @param group_id Group containing the subgroup.
         * @param subgroup_id Subgroup stream to end.
         * @param completed `true` for orderly drain-and-close, `false` to reset.
         */
        virtual void EndSubgroup(uint64_t group_id, uint64_t subgroup_id, bool completed = true);

        /**
         * @brief End every subgroup still open on this track.
         *
         * @details Called by the session when the track is unbound, so that subgroups the application
         *      never ended do not keep their streams open for the life of the connection. Each is
         *      ended as completed, matching what a well-behaved application would have done.
         * @note Silently leaves bookkeeping unchanged if no live session exists.
         */
        void EndAllSubgroups();

        /**
         * @brief Set the publish status
         * @param status Status of publishing (aka publish objects)
         * @details Atomically changes status and invokes `StatusChanged` only when the value differs.
         * The callback executes synchronously on the caller's thread.
         */
        void SetStatus(Status status) noexcept
        {
            if (UpdateStatus(status)) {
                StatusChanged(status);
            }
        }

        // --------------------------------------------------------------------------
        // Metrics
        // --------------------------------------------------------------------------

        /**
         * @brief Publish metrics for the track
         *
         * @details Public live counters updated by publication and session metrics processing.
         * Byte/object counters increase when an object passes state checks, before enqueue success;
         * forwarded bytes increase before the request-ID check. Rejected states increment
         * `objects_dropped_not_ok`; missing request IDs and later encoding/enqueue failures do not.
         * Counters are cumulative except the QUIC min/max/average aggregates cleared after sampling.
         * Unsynchronized application writes race with library updates.
         */
        PublishTrackMetrics publish_track_metrics_;

        // --------------------------------------------------------------------------
        // Internals
        // --------------------------------------------------------------------------
      protected:
        /**
         * @brief Handles PUBLISH_OK or REQUEST_UPDATE_OK parameters.
         * @param params Borrowed acknowledgement parameters.
         * @throws messages::ProtocolViolationException for unsupported PUBLISH_OK parameters or a
         * new-group request when dynamic groups are disabled.
         * @details During `kPendingPublishOk`, validates parameters and resolves forwarding to
         * `kOk`/`kPaused`; a new-group status may be transiently set before forwarding is applied.
         * For later update acknowledgements parameters are currently ignored.
         */
        void RequestOkReceived(const messages::Parameters& params) override;

        // --------------------------------------------------------------------------
        // Member variables
        // --------------------------------------------------------------------------
        /// Atomic publication state; does not synchronize the remaining fields.
        std::atomic<Status> publish_status_{ Status::kNotAnnounced };
        TrackMode default_track_mode_; ///< Framing mode used for the next object.

        /// Default subgroup framing properties; populated for stream mode.
        std::optional<messages::StreamHeaderProperties> stream_mode_;
        uint8_t publisher_priority_;      ///< Publisher priority encoded in object headers.
        uint8_t publish_object_priority_; ///< Subscriber-derived transport scheduling priority.
        uint32_t default_ttl_;            ///< Queue lifetime used when object TTL is absent.

        /// @brief State retained for one open subgroup stream.
        struct StreamInfo
        {
            /**
             * Stream the subgroup is written to.
             *
             * @details Held from stream creation until the subgroup ends, so publishing an object
             *      costs no lookup. The transport may close the stream underneath this handle, which
             *      it detects and rejects on send.
             */
            std::shared_ptr<Stream> stream;
            uint64_t last_group_id{ 0 };                            ///< Last group encoded on the stream.
            uint64_t last_subgroup_id{ 0 };                         ///< Last subgroup encoded on the stream.
            std::optional<uint64_t> last_object_id{ std::nullopt }; ///< Last object used for delta encoding.
        };

        std::map<std::uint64_t, std::map<std::uint64_t, StreamInfo>>
          stream_info_by_group_; ///< Open streams keyed first by group ID and then subgroup ID.

        /// Connection-scoped track alias required by object framing.
        std::optional<uint64_t> track_alias_;
        messages::Location largest_location_; ///< Largest/current location reported for the track.

        /// Reusable serialization scratch buffer; invalidated by the next publish/end operation.
        Bytes object_msg_buffer_; // TODO(tievens): Review shrink/resize

        bool support_new_group_request_{ true }; ///< Whether PUBLISH_OK may request dynamic groups.

        /// Pending requested group ID, when one has been recorded.
        std::optional<uint64_t> pending_new_group_request_id_;

      private:
        // Update status without firing the callback.
        std::optional<Status> UpdateStatus(Status status) noexcept;

        friend class Session;
    };

} // namespace moq
