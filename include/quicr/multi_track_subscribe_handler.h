// SPDX-FileCopyrightText: Copyright (c) 2025 Cisco Systems
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

#include <quicr/detail/base_track_handler.h>
#include <quicr/detail/messages.h>
#include <quicr/detail/stream_buffer.h>
#include <quicr/metrics.h>

#include <map>
#include <optional>
#include <vector>

namespace quicr {

    /**
     * @brief MOQ track handler for handling multiple track subscriptions via prefix matching
     *
     * @details MOQ multi-track subscribe handler manages multiple tracks that match
     *  a subscribed namespace prefix (via SUBSCRIBE_NAMESPACE).
     *
     *  This handler is used when a subscriber wants to receive objects from multiple
     *  tracks that share a common namespace prefix. As new tracks are published that
     *  match the prefix, this handler will be notified.
     */
    class MultiTrackSubscribeHandler : public BaseTrackHandler
    {
      public:
        /**
         * @brief Receive status codes per track
         */
        enum class Error : uint8_t
        {
            kOk = 0,
            kNotAuthorized,
            kNotSubscribed,
            kNoData
        };

        /**
         * @brief Status codes for individual tracks
         */
        enum class Status : uint8_t
        {
            kOk = 0,
            kNotConnected,
            kError,
            kNotAuthorized,
            kNotSubscribed,
            kPendingResponse,
            kSendingUnsubscribe,
            kPaused,
            kNewGroupRequested,
        };

        /**
         * @brief Response when accepting or rejecting a new track
         */
        struct TrackAddedResponse
        {
            /**
             * @details Whether to accept or reject the track
             */
            PublishResponse::ReasonCode reason_code{ PublishResponse::ReasonCode::kOk };

            /**
             * @details Optional error reason if rejecting
             */
            std::optional<std::string> error_reason{ std::nullopt };

            /**
             * @details Subscription priority for this track (used if accepted)
             */
            messages::SubscriberPriority priority{ 0 };

            /**
             * @details Group order for this track (used if accepted)
             */
            messages::GroupOrder group_order{ messages::GroupOrder::kAscending };

            /**
             * @details Filter type for this track (used if accepted)
             */
            messages::FilterType filter_type{ messages::FilterType::kLargestObject };
        };

      protected:
        /**
         * @brief Multi-track subscribe handler constructor
         *
         * @param prefix_namespace      Namespace prefix to subscribe to
         */
        explicit MultiTrackSubscribeHandler(const TrackNamespace& prefix_namespace)
          : BaseTrackHandler(FullTrackName{ prefix_namespace, {} })
          , prefix_namespace_(prefix_namespace)
        {
        }

      public:
        /**
         * @brief Create shared multi-track subscribe handler
         *
         * @param prefix_namespace      Namespace prefix to subscribe to
         *
         * @return Shared pointer to the handler
         */
        static std::shared_ptr<MultiTrackSubscribeHandler> Create(const TrackNamespace& prefix_namespace)
        {
            return std::shared_ptr<MultiTrackSubscribeHandler>(new MultiTrackSubscribeHandler(prefix_namespace));
        }

        // --------------------------------------------------------------------------
        // Public getter methods
        // --------------------------------------------------------------------------

        /**
         * @brief Get the prefix namespace this handler is subscribed to
         *
         * @return Prefix namespace
         */
        constexpr const TrackNamespace& GetPrefixNamespace() const noexcept { return prefix_namespace_; }

        // --------------------------------------------------------------------------
        // Track query methods
        // --------------------------------------------------------------------------

        /**
         * @brief Get all currently active tracks
         *
         * @return Vector of full track names for all active tracks
         */
        std::vector<FullTrackName> GetActiveTracks() const;

        /**
         * @brief Get the status of a specific track
         *
         * @param track     Full track name to query
         *
         * @return Status of the track, or std::nullopt if track is not found
         *
         */
        // TODO: Throw or nullopt?
        std::optional<Status> GetTrackStatus(const FullTrackName& track) const;

        /**
         * @brief Get the track alias for a specific track
         *
         * @param track     Full track name to query
         *
         * @return Track alias if found, otherwise std::nullopt
         */
        // TODO: Throw or nullopt?
        std::optional<uint64_t> GetTrackAlias(const FullTrackName& track) const;

        /**
         * @brief Get metrics for a specific track
         *
         * @param track     Full track name to query
         *
         * @return Track metrics if found, otherwise std::nullopt
         */
        // TODO: Throw or nullopt?
        std::optional<SubscribeTrackMetrics> GetTrackMetrics(const FullTrackName& track) const;

        /**
         * @brief Get subscription priority for a specific track
         *
         * @param track     Full track name to query
         *
         * @return Priority if track found, otherwise std::nullopt
         */
        // TODO: Throw or nullopt?
        std::optional<messages::SubscriberPriority> GetTrackPriority(const FullTrackName& track) const;

        /**
         * @brief Get group order for a specific track
         *
         * @param track     Full track name to query
         *
         * @return Group order if track found, otherwise std::nullopt
         */
        // TODO: Throw or nullopt?
        std::optional<messages::GroupOrder> GetTrackGroupOrder(const FullTrackName& track) const;

        /**
         * @brief Get filter type for a specific track
         *
         * @param track     Full track name to query
         *
         * @return Filter type if track found, otherwise std::nullopt
         */
        // TODO: Throw or nullopt?
        std::optional<messages::FilterType> GetTrackFilterType(const FullTrackName& track) const;

        // --------------------------------------------------------------------------
        // Per-track control methods
        // --------------------------------------------------------------------------

        /**
         * @brief Pause receiving data for a track
         *
         * @details Pause will send a MoQT SUBSCRIBE_UPDATE to change the forwarding
         *  state to be stopped for this specific track.
         *
         * @param track Full track name to pause, or std::nullopt to pause all tracks.
         */
        void Pause(const std::optional<FullTrackName>& track) noexcept;

        /**
         * @brief Resume receiving data for a track
         *
         * @details Resume will send a MoQT SUBSCRIBE_UPDATE to change the forwarding
         *  state to send for this specific track.
         *
         * @param track Full track name to resume, or std::nullopt to resume all tracks.
         */
        void Resume(const std::optional<FullTrackName>& track) noexcept;

        /**
         * @brief Unsubscribe from a track
         *
         * @details Unsubscribe from this track while keeping other tracks active.
         *  This will send a MoQT UNSUBSCRIBE for the specific track.
         *
         * @param track Full track name to unsubscribe from
         */
        void Unsubscribe(const FullTrackName& track) noexcept;

        /**
         * @brief Generate a new group request for a track
         *
         * @param track Full track name to request new group for, or std::nullopt for all tracks.
         */
        void RequestNewGroup(const std::optional<FullTrackName>& track) noexcept;

        // --------------------------------------------------------------------------
        // Public Virtual API callback event methods
        // --------------------------------------------------------------------------
        /** @name Callbacks
         */
        ///@{

        /**
         * @brief Notification of received [full] data object
         *
         * @details Event notification to provide the caller the received full data object.
         *  The track parameter identifies which track this object belongs to.
         *
         * @warning This data will be invalidated after return of this method
         *
         * @param track             Full track name identifying the source track
         * @param object_headers    Object headers, must include group and object Ids
         * @param data              Object payload data received, **MUST** match ObjectHeaders::payload_length
         */
        virtual void ObjectReceived([[maybe_unused]] const FullTrackName& track,
                                    [[maybe_unused]] const ObjectHeaders& object_headers,
                                    [[maybe_unused]] BytesSpan data);

        /**
         * @brief Notification of received stream data slice
         *
         * @details Event notification to provide the caller the raw data received on a stream.
         *  The track parameter identifies which track this data belongs to.
         *
         * @param track       Full track name identifying the source track
         * @param is_start    True to indicate if this data is the start of a new stream
         * @param stream_id   Stream ID data was received on
         * @param data        Shared pointer to the data received
         */
        virtual void StreamDataRecv(const FullTrackName& track,
                                    bool is_start,
                                    uint64_t stream_id,
                                    std::shared_ptr<const std::vector<uint8_t>> data);

        /**
         * @brief Notification of received datagram data
         *
         * @details Event notification to provide the caller the raw data received as a datagram.
         *  The track parameter identifies which track this data belongs to.
         *
         * @param track       Full track name identifying the source track
         * @param data        Shared pointer to the data received
         */
        virtual void DgramDataRecv(const FullTrackName& track, std::shared_ptr<const std::vector<uint8_t>> data);

        /**
         * @brief Notification of a partial object received data object
         *
         * @details Event notification to provide the caller the received partial data object.
         *  The track parameter identifies which track this object belongs to.
         *
         * @warning This data will be invalidated after return of this method
         *
         * @param track             Full track name identifying the source track
         * @param object_headers    Object headers, must include group and object Ids
         * @param data              Object payload data received, can be <= ObjectHeaders::payload_length
         */
        virtual void PartialObjectReceived([[maybe_unused]] const FullTrackName& track,
                                           [[maybe_unused]] const ObjectHeaders& object_headers,
                                           [[maybe_unused]] BytesSpan data)
        {
        }

        /**
         * @brief Notification of track status change
         *
         * @details Notification when the status of a specific track changes.
         *
         * @param track     Full track name of the track whose status changed
         * @param status    New status of the track
         */
        virtual void StatusChanged([[maybe_unused]] const FullTrackName& track, [[maybe_unused]] Status status) {}

        /**
         * @brief Notification callback to provide sampled metrics for a track
         *
         * @details Callback will be triggered on Config::metrics_sample_ms to provide the
         *  sampled data based on the sample period for a specific track. After this callback,
         *  the period/sample based metrics will reset and start over for the new period.
         *
         * @param track     Full track name of the track
         * @param metrics   Copy of the subscribed metrics for the sample period
         */
        virtual void MetricsSampled([[maybe_unused]] const FullTrackName& track,
                                    [[maybe_unused]] const SubscribeTrackMetrics& metrics)
        {
        }

        /**
         * @brief Notification when a new track is available
         *
         * @details Called when a PUBLISH message is received that matches the prefix
         *  namespace. The application can decide whether to accept or reject the track,
         *  and if accepted, specify the subscription parameters (priority, group order,
         *  filter type) for this specific track.
         *
         * @param track     Full track name of the new track
         *
         * @return TrackAddedResponse with acceptance decision and subscription parameters
         */
        virtual TrackAddedResponse TrackAdded([[maybe_unused]] const FullTrackName& track)
        {
            return TrackAddedResponse{};
        }

        /**
         * @brief Notification when a track is removed from this handler
         *
         * @details Called when a track ends (PUBLISH_DONE) or is unsubscribed.
         *
         * @param track     Full track name of the removed track
         */
        virtual void TrackRemoved([[maybe_unused]] const FullTrackName& track) {}

        ///@}

      protected:
        /**
         * @brief Per-track context information
         *
         * @details Stores all state needed for managing a single track within
         *  the multi-track handler.
         */
        struct TrackContext
        {
            FullTrackName full_track_name;                    ///< Full track name for callbacks
            uint64_t track_alias;                             ///< Track alias received from PUBLISH message
            Status status{ Status::kNotSubscribed };          ///< Current status of this track
            StreamBuffer<uint8_t> stream_buffer;              ///< Stream buffer for this track
            std::optional<uint64_t> request_id;               ///< Original PUBLISH request ID
            SubscribeTrackMetrics metrics;                    ///< Per-track metrics
            std::optional<uint64_t> next_object_id;           ///< Expected next object ID
            uint64_t current_group_id{ 0 };                   ///< Current group ID
            uint64_t current_subgroup_id{ 0 };                ///< Current subgroup ID
            uint64_t current_stream_id{ 0 };                  ///< Current stream ID
            std::chrono::milliseconds delivery_timeout_{ 0 }; ///< Delivery timeout for this track
            messages::SubscriberPriority priority{ 0 };       ///< Subscription priority for this track
            messages::GroupOrder group_order{ messages::GroupOrder::kAscending };     ///< Group order for this track
            messages::FilterType filter_type{ messages::FilterType::kLargestObject }; ///< Filter type for this track

            TrackContext(const FullTrackName& ftn,
                         uint64_t alias,
                         std::optional<uint64_t> req_id,
                         messages::SubscriberPriority prio,
                         messages::GroupOrder order,
                         messages::FilterType filter)
              : full_track_name(ftn)
              , track_alias(alias)
              , request_id(req_id)
              , priority(prio)
              , group_order(order)
              , filter_type(filter)
            {
            }
        };

        /**
         * @brief Set the status for a specific track and notify via callback
         *
         * @param track_alias   Track alias to update
         * @param status        New status
         */
        void SetTrackStatus(uint64_t track_alias, Status status);

        // --------------------------------------------------------------------------
        // Member variables
        // --------------------------------------------------------------------------

        /// Map from track alias to track context
        std::map<uint64_t, TrackContext> tracks_;

        /// Prefix namespace this handler is subscribed to
        TrackNamespace prefix_namespace_;

      private:
        friend class Transport;
        friend class Client;
        friend class Server;

        // --------------------------------------------------------------------------
        // Internal methods for Transport layer
        // --------------------------------------------------------------------------

        /**
         * @brief Add a new track to this handler
         *
         * @details Called by transport when a PUBLISH message is generated that matches
         *  the prefix namespace. Invokes the TrackAdded() callback to allow the application
         *  to accept/reject and specify subscription parameters. If accepted, creates track
         *  context with the specified parameters and adds to internal map.
         *
         * @param full_name     Full track name of the new track
         * @param track_alias   Track alias assigned by the publisher
         * @param request_id    Request ID from the PUBLISH message
         *
         * @return PublishResponse indicating acceptance or rejection
         */
        PublishResponse AddTrack(const FullTrackName& full_name, uint64_t track_alias, uint64_t request_id);

        /**
         * @brief Remove a track from this handler
         *
         * @details Called by transport when a track ends (PUBLISH_DONE) or is rejected.
         *  Removes track context from internal map and notifies via callback.
         *
         * @param track_alias   Track alias to remove
         */
        void RemoveTrack(uint64_t track_alias);

        /**
         * @brief Get track context by alias (for data routing)
         *
         * @details Used by transport to look up track context when data arrives.
         *
         * @param track_alias   Track alias from received data
         *
         * @return Pointer to track context if found, nullptr otherwise
         */
        TrackContext* GetTrackByAlias(uint64_t track_alias);

        /**
         * @brief Get track context by full track name
         *
         * @details Helper method for control operations that use full track name.
         *
         * @param track     Full track name to look up
         *
         * @return Pointer to track context if found, nullptr otherwise
         */
        TrackContext* GetTrackByName(const FullTrackName& track);
    };

} // namespace quicr
