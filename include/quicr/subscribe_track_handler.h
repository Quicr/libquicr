// SPDX-FileCopyrightText: Copyright (c) 2024 Cisco Systems
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

#include <quicr/detail/base_track_handler.h>
#include <quicr/detail/messages.h>
#include <quicr/detail/stream_buffer.h>
#include <quicr/detail/subscription_filters.h>
#include <quicr/metrics.h>
#include <quicr/object.h>

namespace quicr {

    // Verify that FilterExtensions and Extensions are the same type for safe pointer conversion
    static_assert(std::is_same_v<filters::FilterExtensions, Extensions>,
                  "FilterExtensions must match quicr::Extensions for safe pointer conversion");

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
            kError,
            kNotAuthorized,
            kNotSubscribed,
            kPendingResponse,
            kSendingUnsubscribe, ///< In this state, callbacks will not be called,
            kPaused,
            kNewGroupRequested,
            kCancelled,
            kDoneByFin,
            kDoneByReset,
        };

        /**
         * @brief Attributes to use when subscribing with a Joining Fetch.
         */
        struct JoiningFetch
        {
            const messages::SubscriberPriority priority;
            const messages::GroupOrder group_order;
            const messages::Parameters parameters;
            const messages::GroupId joining_start;
            const bool absolute;
        };

      protected:
        /**
         * @brief Subscribe track handler constructor
         *
         * @param full_track_name           Full track name struct
         * @param joining_fetch             If set, subscribe with a joining fetch using these attributes.
         * @param publisher_initiated       True if publisher initiated the subscribe, otherwise False
         */
        SubscribeTrackHandler(const FullTrackName& full_track_name,
                              messages::SubscriberPriority priority,
                              messages::GroupOrder group_order,
                              messages::FilterType filter_type,
                              const std::optional<JoiningFetch>& joining_fetch = std::nullopt,
                              bool publisher_initiated = false)
          : BaseTrackHandler(full_track_name)
          , priority_(priority)
          , group_order_(group_order)
          , filter_type_(filter_type)
          , joining_fetch_(publisher_initiated ? std::nullopt : joining_fetch)
          , publisher_initiated_(publisher_initiated)
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
          messages::SubscriberPriority priority,
          messages::GroupOrder group_order = messages::GroupOrder::kAscending,
          messages::FilterType filter_type = messages::FilterType::kLargestObject)
        {
            return std::shared_ptr<SubscribeTrackHandler>(
              new SubscribeTrackHandler(full_track_name, priority, group_order, filter_type));
        }

        /**
         * @brief Get the status of the subscribe
         *
         * @return Status of the subscribe
         */
        constexpr Status GetStatus() const noexcept { return status_; }

        /**
         * @brief Set the priority of received data
         *
         * @param priority      Priority value of received data
         */
        void SetPriority(uint8_t priority) noexcept { priority_ = priority; }

        /**
         * @brief Get subscription priority
         *
         * @return Priority value
         */
        constexpr messages::SubscriberPriority GetPriority() const noexcept { return priority_; }

        /**
         * @brief Get subscription group order
         *
         * @return GroupOrder value
         */

        constexpr messages::GroupOrder GetGroupOrder() const noexcept { return group_order_; }

        /**
         * @brief Get subscription filter type
         *
         * @return FilterType value
         */

        constexpr messages::FilterType GetFilterType() const noexcept { return filter_type_; }

        /**
         * @brief Get the subscription filter
         *
         * @return Reference to the subscription filter
         */
        const filters::SubscriptionFilter& GetSubscriptionFilter() const noexcept { return subscription_filter_; }

        /**
         * @brief Get mutable subscription filter for configuration
         *
         * @return Reference to the subscription filter
         */
        filters::SubscriptionFilter& GetSubscriptionFilter() noexcept { return subscription_filter_; }

        /**
         * @brief Set the subscription filter
         *
         * @param filter The subscription filter to use
         */
        void SetSubscriptionFilter(filters::SubscriptionFilter filter) noexcept
        {
            subscription_filter_ = std::move(filter);
        }

        /**
         * @brief Check if an object passes the subscription filter
         *
         * @param headers Object headers to check
         * @return true if object should be delivered, false if filtered out
         */
        [[nodiscard]] bool ShouldDeliverObject(const ObjectHeaders& headers) const noexcept
        {
            if (subscription_filter_.IsEmpty()) {
                return true;
            }

            // Convert headers to filter context
            // FilterExtensions is an alias to quicr::Extensions, so direct pointer use is safe
            filters::ObjectContext ctx(headers.group_id,
                                        headers.subgroup_id,
                                        headers.object_id,
                                        headers.priority.value_or(0),
                                        &headers.extensions,
                                        &headers.immutable_extensions);
            return subscription_filter_.Matches(ctx);
        }

        constexpr std::optional<messages::Location> GetLatestLocation() const noexcept { return latest_location_; }

        constexpr void SetLatestLocation(messages::Location new_location) noexcept { latest_location_ = new_location; }

        /**
         * @brief Get joining fetch info, if any.
         */
        std::optional<JoiningFetch> GetJoiningFetch() const noexcept { return joining_fetch_; }

        /**
         * @brief Set the track alias
         *
         * @param track_alias       MoQ track alias for track namespace+name that
         *                          is relative to the QUIC connection session
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
         * @brief Set the received track alias
         *
         * @param track_alias       MoQ track alias for track namespace+name that
         *                          is relative to the QUIC connection session
         */
        void SetReceivedTrackAlias(uint64_t track_alias) { received_track_alias_ = track_alias; }

        /**
         * @brief Set the new group request Id
         *
         * @param group_id              Group ID to request via new group request
         */
        void SetNewGroupRequestId(uint64_t group_id)
        {
            if (!pending_new_group_request_id_.has_value() || *pending_new_group_request_id_ < group_id) {
                pending_new_group_request_id_ = group_id;
            } else {
                pending_new_group_request_id_ = 0;
            }
        }

        /**
         * @brief Get the received track alias
         *
         * @details If the track alias is set, it will be returned, otherwise std::nullopt.
         *
         * @return Track alias if set, otherwise std::nullopt.
         */
        std::optional<uint64_t> GetReceivedTrackAlias() const noexcept { return received_track_alias_; }

        /**
         * @brief Pause receiving data
         * @details Pause will send a MoQT SUBSCRIBE_UPDATE to change the forwarding state to be stopped         *
         */
        void Pause() noexcept;

        /**
         * @brief Resume receiving data
         * @details Rresume will send a MoQT SUBSCRIBE_UPDATE to change the forwarding state to send
         */
        void Resume() noexcept;

        /**
         * @brief Generate a new group request for this subscription
         *
         * @param group_id      Value of group requested or zero if unknown
         */
        void RequestNewGroup(uint64_t group_id = 0) noexcept;

        /**
         * @brief Indicate if subscribe handler should send new group requests or not
         *
         * @param is_supported      True to send new group requests, False to disable sending
         */
        void SupportNewGroupRequest(bool is_supported) noexcept;

        /**
         * @brief Indicate if subscribe handler can send new group requests or not
         * @return True if new group requests are supported.
         */
        bool IsNewGroupRequestSupported() const noexcept { return support_new_group_request_; }

        std::chrono::milliseconds GetDeliveryTimeout() const noexcept { return delivery_timeout_; }

        void SetDeliveryTimeout(std::chrono::milliseconds timeout) noexcept { delivery_timeout_ = timeout; }

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
                                    [[maybe_unused]] BytesSpan data);

        /**
         * @brief Notification of received object status
         *
         * @details Event notification when an ObjectDatagramStatus message is received.
         *
         * @param group_id              Group ID of the status
         * @param object_id             Object ID of the status
         * @param status                Status
         * @param extensions            Mutable extensions, if any
         * @param immutable_extensions  Immutable extensions, if any
         */
        virtual void ObjectStatusReceived([[maybe_unused]] uint64_t group_id,
                                          [[maybe_unused]] uint64_t object_id,
                                          [[maybe_unused]] ObjectStatus status,
                                          [[maybe_unused]] std::optional<Extensions> extensions,
                                          [[maybe_unused]] std::optional<Extensions> immutable_extensions)
        {
        }

        /**
         * @brief Notification of received stream data slice
         *
         * @details Event notification to provide the caller the raw data received on a stream
         *
         * @param is_start    True to indicate if this data is the start of a new stream
         * @param stream_id   Stream ID data was received on
         * @param data        Shared pointer to the data received
         */
        virtual void StreamDataRecv(bool is_start,
                                    uint64_t stream_id,
                                    std::shared_ptr<const std::vector<uint8_t>> data);

        /**
         * @brief Notification of received datagram data
         *
         * @details Event notification to provide the caller the raw data received as a datagram
         *
         * @param data        Shared pointer to the data received
         */
        virtual void DgramDataRecv(std::shared_ptr<const std::vector<uint8_t>> data);

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

        /**
         * @brief Check if the subscribe is publisher initiated or not
         * @return True if publisher initiated, false if initiated by the relay/server
         */
        bool IsPublisherInitiated() const noexcept { return publisher_initiated_; }

        /**
         * @brief Subscribe metrics for the track
         *
         * @details Subscribe metrics are updated real-time and transport quic metrics on metrics_sample_ms
         *     period.
         */
        SubscribeTrackMetrics subscribe_track_metrics_;

      protected:
        /**
         * @brief Set the subscribe status
         * @param status                Status of the subscribe
         */
        void SetStatus(Status status) noexcept
        {
            status_ = status;
            StatusChanged(status);
        }

        StreamBuffer<uint8_t> stream_buffer_;

        std::optional<uint64_t> next_object_id_;
        uint64_t current_group_id_{ 0 };
        uint64_t current_subgroup_id_{ 0 };
        std::optional<uint64_t> pending_new_group_request_id_;
        bool is_fetch_handler_{ false };

      private:
        Status status_{ Status::kNotSubscribed };
        messages::SubscriberPriority priority_;
        messages::GroupOrder group_order_;
        messages::FilterType filter_type_;
        filters::SubscriptionFilter subscription_filter_; ///< Subscription filter for object filtering
        uint64_t current_stream_id_{ 0 };
        std::optional<messages::Location> latest_location_;
        std::optional<JoiningFetch> joining_fetch_;
        std::optional<uint64_t> track_alias_;
        std::optional<uint64_t> received_track_alias_; ///< Received track alias from publisher client or relay
        std::chrono::milliseconds delivery_timeout_{ 0 };

        bool publisher_initiated_{ false };
        bool support_new_group_request_{ false };

        friend class Transport;
        friend class Client;
        friend class Server;
    };

} // namespace moq
