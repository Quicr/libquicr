// SPDX-FileCopyrightText: Copyright (c) 2024 Cisco Systems
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

#include "quicr/handlers/base_track_handler.h"
#include "quicr/messages/messages.h"
#include "quicr/messages/object.h"
#include "quicr/metrics.h"

#include <atomic>
#include <functional>

namespace quicr {

    // TODO: Pick a name for your one "session peer". The endpoint you are connected to in the context of a Session.
    // TODO: It's perhaps important to note that this handler is scoped to that context. A relay fanout would need N
    // not 1.
    // TODO: It does actually seem that the MoQ draft does use the word "peer" for this case.

    /**
     * @brief Supports publishing a track and objects to that track.
     *
     * @details Applications can use this handler to announce the existence of the given track (Session::PublishTrack)
     * and subsequently publish objects using #PublishObject(). This handler is designed to be subclassed for full
     * visibility into state, although it can be used directly.
     *
     * @remarks It is safe to call any function from any application thread, although data plane calls
     * (#PublishObject(), #ForwardPublishedData(), #EndSubgroup()) should not run concurrently in a given handler.
     * Callback functions are fired from libquicr's single notification thread, are re-entrant safe, but should not
     * block.
     */
    class PublishTrackHandler : public BaseTrackHandler
    {
      public:
        /**
         * @brief Possible results of a #PublishObject() or #ForwardPublishedData() call.
         */
        // TODO: Some of these map 1:1 to a handler status. We could potentially simplify publish return codes (e.g
        // TODO: kCannotPublish) at the cost of the app having to lookup the handler status to determine more detail.
        enum class PublishObjectStatus : uint8_t
        {
            /// This object was successfully published.
            kOk = 0,
            /// Internal library error. // TODO: Understand what these could be.
            kInternalError,
            /// The publication of the track was refused.
            kNotAuthorized,
            /// Track has not yet been published. (TODO: Should we use the word "announce" or "publish"?)
            kNotAnnounced,
            /// No active subscribers, or the subscription has ended. TODO: This is where kUnsubscribed might make more
            /// sense as a distinct status. The former can flip, the latter cannot.
            kNoSubscribers,
            /// Cannot forward subgroup data until the first subgroup object has been fully published using
            /// #PublishObject. See #ForwardPublishedData(). TODO: Name?
            kObjectDataIncomplete,
            /// Publication is paused and not currently forwarding objects.
            kPaused,
            /// The publication of the track has not yet been accepted by the peer.
            kPendingPublishOk
            // TODO: What about the REQUEST_ERROR case, where the publication is rejected by the peer.
        };

        /**
         * @brief Possible states the handler can be in.
         *
         * @note kOk is not the only status that means it is okay to publish.
         *      CanPublish() method should be used to determine if the status is okay to still publish or not.
         *
         */
        enum class Status : uint8_t
        {
            /// Normal operation.
            kOk = 0,
            /// The connection is down.
            kNotConnected,
            /// This publication has not been announced via Session::PublishTrack().
            kNotAnnounced,
            /// This publication has been rejected due to auth.
            kNotAuthorized,
            /// There is no downstream subscriber, but one may still arrive.
            kNoSubscribers,
            /// The subscriber has terminated the request. This is a terminal state.
            kUnsubscribed,
            // TODO: A FIN only is allowed in the context of a REQUEST_ERROR / FIN rejecting a PUBLISH, not during
            // request lifetime.
            kDoneByFin,
            /// The subscriber has updated their subscription request. Updated state is visible at entry of this state.
            /// This state will be "consumed" on next publish, or any other state change.
            kSubscriptionUpdated,
            /// The subscriber has requested that a new group be produced.
            kNewGroupRequested,
            /// The publication is yet to be accepted or rejected.
            kPendingPublishOk,
            /// The subscriber has requested objects not be forwarded to it.
            kPaused,
        };

      protected:
        /**
         * @brief Publish track handler constructor
         *
         * @param full_track_name       Full track name
         * @param track_mode            The track mode to operate using
         * @param default_priority      Default priority for objects if not specified in ObjectHeaders
         * @param default_ttl           Default TTL for objects if not specified in ObjectHeaders
         * @param subgroup_properties   Default subgroup header framing to use when track mode is kStream. Must not
         *                              be set for kDatagram. Defaults to explicit subgroup IDs with extensions
         *                              enabled and per-subgroup priority
         * @param largest_location      Largest location to start the handler from
         */
        PublishTrackHandler(const FullTrackName& full_track_name,
                            TrackMode track_mode,
                            uint8_t default_priority,
                            uint32_t default_ttl,
                            std::optional<messages::StreamHeaderProperties> subgroup_properties = std::nullopt,
                            messages::Location largest_location = { 0, 0 });

      public:
        /**
         * @brief Create a shared Publish track handler
         *
         * @param full_track_name       Full track name
         * @param track_mode            The track mode to operate using
         * @param default_priority      Default priority for objects if not specified in ObjectHeaderss
         * @param default_ttl           Default TTL for objects if not specified in ObjectHeaderss
         * @param largest_location      Largest location to start handler at
         */
        static std::shared_ptr<PublishTrackHandler> Create(const FullTrackName& full_track_name,
                                                           TrackMode track_mode,
                                                           uint8_t default_priority,
                                                           uint32_t default_ttl,
                                                           messages::Location largest_location)
        {
            return std::shared_ptr<PublishTrackHandler>(new PublishTrackHandler(
              full_track_name, track_mode, default_priority, default_ttl, std::nullopt, largest_location));
        }

        // --------------------------------------------------------------------------
        // Public Virtual API callback event methods
        // --------------------------------------------------------------------------
        /** @name Callbacks
         */
        ///@{

        /**
         * @brief The status of the publication has changed.
         * @details Notification of a change to the handler's status, such as
         *      when it's ready to publish or not ready to publish.
         * @remarks This is fired on libquicr's callback thread. // TODO: Double check this is always true.
         *
         * @param status The new status.
         */
        virtual void StatusChanged(Status status);

        /**
         * @brief Notification callback to provide sampled metrics
         *
         * @details Callback will be triggered on Config::metrics_sample_ms to provide the sampled data based
         *      on the sample period.  After this callback, the period/sample based metrics will reset and start over
         *      for the new period.
         *
         * @remarks This is fired on libquicr's callback thread.
         *
         * @param metrics           Copy of the published metrics for the sample period
         */
        virtual void MetricsSampled(const PublishTrackMetrics& metrics);

        // TODO: Does this callback need to be public? The reasoning it might not need to be is that parameters MUST
        // be understood by libquicr in order to even parse them, and so we can resolve them all to their respective
        // members instead, and then make a status callback (updated/ok) instead.
        void RequestUpdateReceived(const messages::Parameters& params) override;

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
         * @brief Get the default TTL/expiry for published objects
         */
        uint32_t GetDefaultTTL() const noexcept { return default_ttl_; }

        /**
         * @brief set/update the default track mode for objects
         */
        void SetDefaultTrackMode(const TrackMode track_mode) noexcept { default_track_mode_ = track_mode; }

        /**
         * @brief Get the default subgroup header framing used for kStream tracks.
         * @return The default subgroup properties, or std::nullopt for a kDatagram track.
         */
        constexpr std::optional<messages::StreamHeaderProperties> GetSubgroupProperties() const noexcept
        {
            return subgroup_properties_;
        }

        /**
         * @brief Get the current status of the handler. Apps should respond to status changes via #StatusChanged() over
         * observing #GetStatus() to avoid missing them.
         *
         * @return Current status of the handler.
         */
        Status GetStatus() const noexcept { return publish_status_.load(std::memory_order_acquire); }

        /**
         * Get the largest location that been published on this track.
         *
         * // TODO: Should this be largest published or attempted to be published.
         *
         * @return the largest/current location
         */
        constexpr messages::Location GetLargestLocation() const noexcept { return largest_location_; }

        // TODO: Likewise should this be public or we can just handle internally?
        /**
         * @brief Notification that a stream has been closed.
         * @param stream_id The ID of the stream being closed.
         * @param reset     True if stream closed by reset, false if closed by FIN.
         *
         */
        virtual void StreamClosed(std::uint64_t stream_id, bool reset = false);

        // --------------------------------------------------------------------------
        // Methods that normally do not need to be overridden
        // --------------------------------------------------------------------------

        /**
         * @brief Check if the handler is currently allowing publishing of objects.
         *
         * @return true to indicate that the publisher can publish, false if the publisher cannot
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

        // TODO: Nobody ever updates this. It could be set at construction time, and internally set by libquicr when
        // unset at the point of PublishTrack.
        // TODO: This would make it "publically" immutable and remove this API surface.

        /**
         * @brief Optionally, set the track alias. If left unset, libquicr will generate one for this track.
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
         * @brief Publish a complete object on this track.
         *
         * @details Serializes and enqueues a single object for delivery. The wire framing depends on the
         *   handler's track mode:
         *   - **kDatagram**: each object is sent as an independent ObjectDatagram. Its header framing is derived
         *     per-object from the object itself (extensions, object ID, priority), so @p subgroup_properties does
         *     not apply.
         *   - **kStream**: objects are grouped into subgroup streams keyed by (group ID, subgroup ID). The first
         *     object seen for a given subgroup opens a new stream and emits a StreamHeaderSubGroup; subsequent
         *     objects for that subgroup are appended to it.
         *
         * @param object_headers        Object headers; must include group and object IDs.
         * @param data                  Complete payload for the object.
         * @param subgroup_properties   Optional override of the subgroup header framing. Only valid in kStream mode,
         *                              and only on the first object of a subgroup. When omitted, the handler's
         *                              default is used.
         *
         * @returns Status reflecting whether the object was published.
         */
        virtual PublishObjectStatus PublishObject(
          const ObjectHeaders& object_headers,
          BytesSpan data,
          std::optional<messages::StreamHeaderProperties> subgroup_properties = std::nullopt);

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

        // TODO: Why is this virtual?

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
        virtual void EndSubgroup(uint64_t group_id, uint64_t subgroup_id, bool completed = true);

        // TODO: We need an EndGroup API here.

        // TODO: Internal? Laps does use for top-n pause though.
        /**
         * @brief Set the status of the handler and fire the #StatusChanged() callback.
         * @param status The new status of the handler.
         */
        void SetStatus(Status status) noexcept
        {
            const auto previous = publish_status_.exchange(status, std::memory_order_acq_rel);
            if (previous == status) {
                return;
            }
            StatusChanged(status);
        }

        // --------------------------------------------------------------------------
        // Metrics
        // --------------------------------------------------------------------------

        // TODO: Why is this public.

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
        void RequestOkReceived(const messages::Parameters& params) override;

        // --------------------------------------------------------------------------
        // Member variables
        // --------------------------------------------------------------------------
        std::atomic<Status> publish_status_{ Status::kNotAnnounced };
        TrackMode default_track_mode_;
        std::optional<messages::StreamHeaderProperties> subgroup_properties_;
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

        // Key is group and subgroup id
        std::map<std::uint64_t, std::map<std::uint64_t, StreamInfo>> stream_info_by_group_;

        std::optional<uint64_t> track_alias_;
        messages::Location largest_location_;

        Bytes object_msg_buffer_; // TODO(tievens): Review shrink/resize

        bool support_new_group_request_{ true }; /// TODO: For now, always support dynamic groups
        std::optional<uint64_t> pending_new_group_request_id_;

        // TODO: If we're continuing with friend, we may as well move internal things to friend-called over public.
        friend class Session;
    };

} // namespace moq
