// SPDX-FileCopyrightText: Copyright (c) 2025 Cisco Systems
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

#include "quicr/detail/attributes.h"
#include "quicr/detail/ctrl_message_types.h"
#include "quicr/detail/receive_track_handler.h"
#include "quicr/metrics.h"
#include "quicr/object.h"
#include "quicr/track_name.h"

#include <cstdint>
#include <map>
#include <memory>
#include <utility>

namespace quicr {

    /**
     * @brief MOQ track handler for subscribe namespace and associated tracks.
     *
     * @details MOQ subscribe namespace handler defines all track related callbacks and
     *  functions for subscribe namespace and accepted tracks.
     *  This Track handler notifies of available tracks, and handles object delivery of accepted
     *  ones.
     */
    class SubscribeNamespaceHandler : public ReceiveTrackHandler
    {
      public:
        using Error = std::pair<messages::SubscribeNamespaceErrorCode, messages::ReasonPhrase>;
        /**
         * @brief  Status codes for the subscribe track
         */
        enum class Status : uint8_t
        {
            kOk = 0,
            kNotSubscribed,
            kError,
        };

      protected:
        /**
         * @brief Subscribe track handler constructor
         *
         * @param namespace_prefix Namespace prefix to receive notifications for.
         */
        explicit SubscribeNamespaceHandler(const TrackNamespace& namespace_prefix)
          : ReceiveTrackHandler(FullTrackName{ .name_space = namespace_prefix, .name = {} })
          , namespace_prefix_(namespace_prefix)
          , request_id_{ 0 }
        {
        }

      public:
        /**
         * @brief Create shared Subscribe Namespace handler.
         *
         * @param namespace_prefix Namespace prefix to receive notifications for.
         */
        static std::shared_ptr<SubscribeNamespaceHandler> Create(const TrackNamespace& namespace_prefix)
        {
            return std::shared_ptr<SubscribeNamespaceHandler>(new SubscribeNamespaceHandler(namespace_prefix));
        }

        virtual ~SubscribeNamespaceHandler() = default;

        /**
         * @brief Get the namespace prefix this handler is interested in.
         * @return The namespace prefix.
         */
        TrackNamespace GetNamespacePrefix() const noexcept { return namespace_prefix_; }

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
         * @brief Get the error code and reason for the subscribe namespace, if any.
         * @return Subscribe namespace error code and reason.
         */
        std::optional<Error> GetError() const noexcept { return error_; }

        void SetRequestID(messages::RequestID new_id) noexcept { request_id_ = new_id; }

        // --------------------------------------------------------------------------
        // Public Virtual API callback event methods
        // --------------------------------------------------------------------------

        /** @name Callbacks
         */
        ///@{

        /**
         * @brief Notification of subscribe status
         *
         * @details Notification of the subscribe status
         *
         * @param status        Indicates status of the subscribe
         */
        virtual void StatusChanged(Status status);

        /**
         * @brief
         * @param attrs
         */
        virtual bool TrackAvailable(const FullTrackName& track_name);

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
        virtual void ObjectReceived(const messages::TrackAlias& track_alias,
                                    const ObjectHeaders& object_headers,
                                    BytesSpan data);

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
                                    std::shared_ptr<const std::vector<uint8_t>> data) override;

        /**
         * @brief Notification of received datagram data
         *
         * @details Event notification to provide the caller the raw data received as a datagram
         *
         * @param data        Shared pointer to the data received
         */
        virtual void DgramDataRecv(std::shared_ptr<const std::vector<uint8_t>> data) override;

      protected:
        /**
         * @brief Set the subscribe status
         * @param status                Status of the subscribe
         */
        void SetStatus(const Status status) noexcept
        {
            status_ = status;
            StatusChanged(status);
        }

        void SetError(const Error& error)
        {
            error_ = error;
            SetStatus(Status::kError);
        }

      public:
        SubscribeTrackMetrics subscribe_track_metrics_;

      private:
        Status status_{ Status::kNotSubscribed };
        const TrackNamespace namespace_prefix_;
        messages::RequestID request_id_;
        std::optional<Error> error_{};

        StreamBuffer<uint8_t> stream_buffer_;

        std::optional<uint64_t> next_object_id_;
        uint64_t current_group_id_{ 0 };
        uint64_t current_subgroup_id_{ 0 };
        std::optional<uint64_t> pending_new_group_request_id_;

        messages::SubscriberPriority priority_ = 0;
        messages::GroupOrder group_order_;
        messages::FilterType filter_type_;
        uint64_t current_stream_id_{ 0 };
        std::optional<messages::Location> latest_location_;
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
