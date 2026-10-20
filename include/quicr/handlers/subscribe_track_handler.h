// SPDX-FileCopyrightText: Copyright (c) 2024 Cisco Systems
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

#include "quicr/containers/stream_buffer.h"
#include "quicr/handlers/track_handler.h"
#include "quicr/messages/ctrl_message_types.h"
#include "quicr/messages/message_serialisation.h"
#include "quicr/metrics.h"

#include <stdexcept>

namespace quicr {

    /**
     * @brief MOQ track handler for subscribed track
     *
     * @details Receives one track's object callbacks and manages its subscription controls.
     * Payload callbacks are synchronous on session receive processing; borrowed spans become
     * invalid on return. State and most attributes are not synchronized, so applications should
     * serialize calls with callbacks unless a method explicitly says otherwise.
     */
    class SubscribeTrackHandler : public TrackHandler
    {
      public:
        /**
         * @brief Receive status codes
         */
        enum class Error : uint8_t
        {
            kOk = 0,        ///< Receive operation succeeded.
            kNotAuthorized, ///< Subscriber is not authorized for the track.
            kNotSubscribed, ///< No active subscription exists.
            kNoData         ///< No object data is available.
        };

        /**
         * @brief  Status codes for the subscribe track
         */
        enum class Status : uint8_t
        {
            kOk = 0,             ///< Subscription is active and may receive data.
            kNotConnected,       ///< Transport is disconnected.
            kError,              ///< Subscription failed for an unspecified error.
            kNotAuthorized,      ///< Peer denied authorization.
            kNotSubscribed,      ///< Initial/inactive state.
            kPendingResponse,    ///< Waiting for the peer's response.
            kSendingUnsubscribe, ///< In this state, callbacks will not be called,
            kPaused,             ///< Forwarding is locally marked paused.
            kNewGroupRequested,  ///< A new-group transition is in progress.
            kCancelled,          ///< Subscription was cancelled.
            kDoneByFin,          ///< Request or data stream ended normally.
            kDoneByReset,        ///< Request or data stream was reset.
        };

        /**
         * @brief Attributes to use when subscribing with a Joining Fetch.
         */
        struct JoiningFetch
        {
            const std::uint8_t priority{ 0 };                      ///< FETCH transport priority.
            const std::optional<messages::GroupOrder> group_order; ///< Requested order, if explicit.
            const messages::Parameters parameters; ///< Additional FETCH parameters copied into the request.
            const std::uint64_t joining_start;     ///< Joining offset interpreted according to `absolute`.
            const bool absolute;                   ///< Whether `joining_start` is absolute rather than relative.
        };

      protected:
        /**
         * @brief Subscribe track handler constructor
         *
         * @param full_track_name Full track name struct
         * @param priority Subscription scheduling priority.
         * @param group_order Requested group order, or `std::nullopt` to use the publisher default.
         * @param filter Subscription selection filter.
         * @param joining_fetch If set, subscribe with a joining fetch using these attributes.
         * @param publisher_initiated True if publisher initiated the subscribe, otherwise False
         * @details Publisher-initiated subscriptions discard `joining_fetch` and begin in `kOk`;
         * other subscriptions begin in `kNotSubscribed`.
         */
        SubscribeTrackHandler(const FullTrackName& full_track_name,
                              std::uint8_t priority,
                              std::optional<messages::GroupOrder> group_order,
                              const messages::Filter& filter = std::monostate{},
                              const std::optional<JoiningFetch>& joining_fetch = std::nullopt,
                              bool publisher_initiated = false)
          : TrackHandler(full_track_name)
          , subscriber_priority_(priority)
          , group_order_(group_order)
          , filter_(filter)
          , joining_fetch_(publisher_initiated ? std::nullopt : joining_fetch)
          , publisher_initiated_(publisher_initiated)
        {
            if (publisher_initiated) {
                status_ = Status::kOk; // Initially set to kOk
            }
        }

      public:
        /**
         * @brief Create shared Subscribe track handler
         *
         * @param full_track_name Full track name struct
         * @param priority Subscription priority, if omitted, publisher priority
         *                                  is considered
         * @param group_order Order for group delivery, or nullopt for publisher default
         * @param filter Subscription selection filter
         * @param publisher_initiated Whether the peer publisher initiated the subscription
         * @return Shared ownership of the new handler.
         */
        static std::shared_ptr<SubscribeTrackHandler> Create(
          const FullTrackName& full_track_name,
          std::uint8_t priority,
          std::optional<messages::GroupOrder> group_order = messages::GroupOrder::kAscending,
          const messages::Filter& filter = std::monostate{},
          bool publisher_initiated = false)
        {
            return std::shared_ptr<SubscribeTrackHandler>(new SubscribeTrackHandler(
              full_track_name, priority, group_order, filter, std::nullopt, publisher_initiated));
        }

        /**
         * @brief Get the status of the subscribe
         *
         * @return Status of the subscribe
         */
        constexpr Status GetStatus() const noexcept { return status_; }

        /**
         * @brief Sets the subscriber priority used in subsequent control updates.
         * @param priority Priority value; existing streams and already-sent requests are unchanged.
         */
        void SetSubscriberPriority(uint8_t priority) noexcept { subscriber_priority_ = priority; }

        /**
         * @brief Get subscription priority
         *
         * @return Current subscription priority.
         */
        constexpr std::uint8_t GetSubscriberPriority() const noexcept { return subscriber_priority_; }

        /**
         * @brief Get subscription group order
         *
         * @return Requested order, or `std::nullopt` when the publisher chooses.
         */

        constexpr std::optional<messages::GroupOrder> GetGroupOrder() const noexcept { return group_order_; }

        /// @return Publisher's default group order learned for this subscription.
        constexpr messages::GroupOrder GetPublisherDefaultGroupOrder() const noexcept
        {
            return publisher_default_group_order_;
        }

        /// @brief Stores the publisher's default group order. @param group_order New default.
        void SetPublisherDefaultGroupOrder(messages::GroupOrder group_order) noexcept
        {
            publisher_default_group_order_ = group_order;
        }

        /**
         * @brief Get subscription filter type
         *
         * @return Protocol discriminator for `GetFilter()`.
         */

        messages::FilterType GetFilterType() const noexcept { return messages::GetFilterType(filter_); }

        /// @return Borrowed immutable filter, valid for this handler's lifetime.
        constexpr const messages::Filter& GetFilter() const noexcept { return filter_; }

        /// @return Most recently recorded object location, or `std::nullopt` if none is known.
        constexpr std::optional<messages::Location> GetLatestLocation() const noexcept { return latest_location_; }

        /// @brief Records the latest object location. @param new_location Location to store.
        constexpr void SetLatestLocation(messages::Location new_location) noexcept { latest_location_ = new_location; }

        /**
         * @brief Gets joining-fetch attributes.
         * @return A copy of the attributes, or `std::nullopt` when no joining fetch was requested.
         */
        std::optional<JoiningFetch> GetJoiningFetch() const noexcept { return joining_fetch_; }

        /**
         * @brief Set the track alias
         *
         * @param track_alias MoQ track alias for track namespace+name that
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
         * @param track_alias MoQ track alias for track namespace+name that
         *                          is relative to the QUIC connection session
         */
        void SetReceivedTrackAlias(uint64_t track_alias) { received_track_alias_ = track_alias; }

        /**
         * @brief Set the new group request Id
         *
         * @param group_id Group ID to request via new group request
         * @details Stores the larger pending ID. A non-increasing ID replaces the pending value
         * with zero, which represents an unknown/new group request.
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
         * @brief Marks this handler publisher-initiated.
         * @details Also changes `kNotSubscribed` to `kOk`. No callback or protocol message is sent,
         * and any previously retained joining-fetch attributes are not cleared.
         */
        void SetPublishInitiated() noexcept
        {
            publisher_initiated_ = true;
            if (status_ == Status::kNotSubscribed) {
                status_ = Status::kOk; // Initially set to kOk
            }
        }

        /**
         * @brief Pause receiving data
         * @details Sends a REQUEST_UPDATE with forwarding disabled and immediately changes local
         * state to `kPaused`. It silently does nothing without a live session/request stream, when
         * already paused, or in `kNotConnected`. No `StatusChanged` callback is made.
         */
        void Pause() noexcept;

        /**
         * @brief Resume receiving data
         * @details Sends a REQUEST_UPDATE with forwarding enabled and immediately changes local
         * state to `kOk`. It silently does nothing unless currently paused and attached to a live
         * session/request stream. No `StatusChanged` callback is made.
         */
        void Resume() noexcept;

        /**
         * @brief Generate a new group request for this subscription
         *
         * @param group_id Value of group requested or zero if unknown
         * @details Silently does nothing unless status is `kOk`, new-group requests are enabled,
         * and both the session and request stream are available. Local status is unchanged.
         */
        void RequestNewGroup(uint64_t group_id = 0) noexcept;

        /**
         * @brief Indicate if subscribe handler should send new group requests or not
         *
         * @param is_supported True to send new group requests, False to disable sending
         * @note This changes local capability only and sends no control message.
         */
        void SupportNewGroupRequest(bool is_supported) noexcept;

        /**
         * @brief Indicate if subscribe handler can send new group requests or not
         * @return True if new group requests are supported.
         */
        bool IsNewGroupRequestSupported() const noexcept { return support_new_group_request_; }

        /**
         * @brief Gets the requested delivery timeout.
         * @return Borrowed optional timeout, valid for this handler's lifetime.
         */
        const std::optional<std::chrono::milliseconds>& GetDeliveryTimeout() const noexcept
        {
            return delivery_timeout_;
        }

        /**
         * @brief Stores a requested delivery timeout.
         * @param timeout Timeout to store; negative values are accepted and no update is sent.
         */
        void SetDeliveryTimeout(std::chrono::milliseconds timeout) noexcept { delivery_timeout_ = timeout; }

        /**
         * @brief Gets the requested rendezvous timeout.
         * @return Non-negative timeout; zero requests no rendezvous wait.
         */
        constexpr std::chrono::milliseconds GetRendezvousTimeout() const noexcept { return rendezvous_timeout_; }

        /**
         * @brief Set the subscription's rendezvous timeout.
         * @details This requests that the peer holds this subscribe pending for up to this amount of time if a
         * publisher is not yet present for this track.
         * @param timeout Non-negative duration to request.
         * @throws std::invalid_argument if the timeout is negative.
         * @note Stores local request attributes only; it does not update an active subscription.
         */
        void SetRendezvousTimeout(std::chrono::milliseconds timeout)
        {
            if (timeout < std::chrono::milliseconds::zero()) {
                throw std::invalid_argument("Rendezvous timeout must >=0");
            }
            rendezvous_timeout_ = timeout;
        }

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
         * @warning `data` is borrowed and invalid after this method returns. Copy it to retain it.
         *
         * @param object_headers Object headers, must include group and object Ids
         * @param data Object payload data received, **MUST** match ObjectHeaders::payload_length.
         * @param stream_mode If this is the first object of a subgroup, its properties.
         * @note Called synchronously on the session receive path. The default implementation
         * discards the object.
         */
        virtual void ObjectReceived([[maybe_unused]] const ObjectHeaders& object_headers,
                                    [[maybe_unused]] BytesSpan data,
                                    std::optional<messages::StreamHeaderProperties> stream_mode = std::nullopt);

        /**
         * @brief Notification of received object status
         *
         * @details Event notification when an ObjectDatagramStatus message is received.
         *
         * @param group_id Group ID of the status
         * @param object_id Object ID of the status
         * @param priority Priority of the status
         * @param status Status
         * @param extensions Mutable extensions, if any
         * @param immutable_extensions Immutable extensions, if any
         * @note Extension optionals are passed by value and may be retained or moved by an
         * override. The default implementation does nothing.
         */
        virtual void ObjectStatusReceived([[maybe_unused]] uint64_t group_id,
                                          [[maybe_unused]] uint64_t object_id,
                                          [[maybe_unused]] std::uint8_t priority,
                                          [[maybe_unused]] ObjectStatus status,
                                          [[maybe_unused]] std::optional<Extensions> extensions,
                                          [[maybe_unused]] std::optional<Extensions> immutable_extensions)
        {
        }

        /**
         * @brief Notification that no more objects are coming for a subgroup
         *
         * @details The publisher has either finished the subgroup or given up on it, so a
         *      subscriber forwarding it on can close out the subgroup it is publishing. Only
         *      reported for subgroups at least one object arrived for.
         *
         * @param group_id Group the subgroup belongs to
         * @param subgroup_id Subgroup that has ended
         * @param reset True if the publisher gave up on the subgroup rather than
         *                      finishing it
         * @note Called only for a subgroup that reached the handler. The default does nothing.
         */
        virtual void SubgroupEnded([[maybe_unused]] std::uint64_t group_id,
                                   [[maybe_unused]] std::uint64_t subgroup_id,
                                   [[maybe_unused]] bool reset)
        {
        }

        /**
         * @brief Notification of received datagram data
         *
         * @details Event notification to provide the caller the raw data received as a datagram
         *
         * @param data Shared immutable ownership of the complete encoded datagram; an override may
         * retain it after return.
         * @details The default implementation decodes the datagram synchronously, updates object
         * counters, and invokes `ObjectReceived` or `ObjectStatusReceived`. The pointer must not be
         * null and the buffer must not be empty.
         * @throws messages::ProtocolViolationException Propagated for malformed protocol fields.
         */
        virtual void DgramDataRecv(std::shared_ptr<const std::vector<uint8_t>> data);

        /**
         * @brief Notification of a partial object received data object
         *
         * @details Event notification to provide the caller the received data object
         *
         * @warning `data` is borrowed and invalid after this method returns. Copy it to retain it.
         *
         * @param object_headers Object headers, must include group and object Ids
         * @param data Object payload data received, can be <= ObjectHeaders::payload_length
         * @note The default implementation does nothing. This callback is reserved for partial
         * delivery paths and does not imply object completion.
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
         * @param status Indicates status of the subscribe
         * @note The default implementation does nothing. Depending on the initiating operation,
         * some direct local state changes (including `Pause()` and `Resume()`) do not invoke it.
         */
        virtual void StatusChanged([[maybe_unused]] Status status) {}

        /**
         * @brief Notification callback to provide sampled metrics
         *
         * @details Called at the connection metrics cadence, including idle periods, on the
         * session's metrics-reporting path. The structure is a cumulative live snapshot for object
         * and byte counters; those counters are not reset after the callback.
         *
         * @param metrics Copy of the subscribed metrics for the sample period
         * @note The reference is borrowed for the callback. Copy it to retain a stable snapshot.
         */
        virtual void MetricsSampled([[maybe_unused]] const SubscribeTrackMetrics& metrics) {}

        /**
         * @brief Handles an update received from the publisher.
         * @param params Borrowed update parameters.
         * @return Immediate success with empty parameters for a valid publisher-initiated
         * authorization-token-only update.
         * @throws messages::ProtocolViolationException for updates to subscriber-initiated
         * subscriptions or unsupported parameters.
         */
        Reply<messages::Parameters, ErrorCode> RequestUpdateReceived(const messages::Parameters& params) override;

        ///@}

        /**
         * @brief Check if the subscribe is publisher initiated or not
         * @return True if publisher initiated, false if initiated by the relay/server
         */
        bool IsPublisherInitiated() const noexcept { return publisher_initiated_; }

        /**
         * @brief Subscribe metrics for the track
         *
         * @details Public live counters updated by receive/session processing. Object counters
         * include decoded data and status datagrams; byte counters count payload bytes for decoded
         * objects and transport stream bytes for streamed delivery. Values are cumulative and are
         * not reset after `MetricsSampled`. Unsynchronized application writes race with session updates.
         */
        SubscribeTrackMetrics subscribe_track_metrics_;

      protected:
        /**
         * @brief Validates REQUEST_UPDATE_OK parameters.
         * @param params Borrowed response parameters; only expiration and largest-object are accepted.
         * @throws messages::ProtocolViolationException for unsupported parameters.
         * @note Accepted values are currently validated but otherwise ignored.
         */
        void RequestOkReceived(const messages::Parameters& params) override;

        /**
         * @brief Set the subscribe status
         * @param status Status of the subscribe
         * @details Assigns the status and synchronously invokes `StatusChanged`, even when the
         * value did not change.
         */
        void SetStatus(Status status) noexcept
        {
            status_ = status;
            StatusChanged(status);
        }

        /// Pending new-group request ID; zero denotes an unspecified group.
        std::optional<uint64_t> pending_new_group_request_id_;

        /// Reusable internal datagram decode buffer; derived classes should not retain views into it.
        StreamBuffer<uint8_t> dgram_buffer_;

      private:
        Status status_{ Status::kNotSubscribed };
        std::uint8_t subscriber_priority_;
        std::optional<messages::GroupOrder> group_order_;
        messages::GroupOrder publisher_default_group_order_{ messages::GroupOrder::kAscending };
        messages::Filter filter_;
        std::optional<messages::Location> latest_location_;
        std::optional<JoiningFetch> joining_fetch_;
        std::optional<uint64_t> track_alias_;
        std::optional<uint64_t> received_track_alias_; ///< Received track alias from publisher client or relay
        std::optional<std::chrono::milliseconds> delivery_timeout_ = std::nullopt;
        std::chrono::milliseconds rendezvous_timeout_{ 0 };

        bool publisher_initiated_{ false };
        bool support_new_group_request_{ false };

        friend class Session;
    };

} // namespace moq
