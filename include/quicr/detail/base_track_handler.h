// SPDX-FileCopyrightText: Copyright (c) 2024 Cisco Systems
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

#include "quicr/common.h"
#include "quicr/detail/attributes.h"
#include "quicr/detail/ctrl_message_types.h"
#include "quicr/track_name.h"

#include <optional>
#include <span>
#include <vector>

namespace quicr {
    class Session;

    /**
     * @brief Track mode object of object published or received
     *
     * @details QUIC stream handling mode used to send objects or how object was received
     */
    enum class TrackMode : uint8_t
    {
        kDatagram,
        kStream,
    };

    /**
     * @brief Response to received MOQT Request message
     */
    struct RequestResponse
    {
        /**
         * @details **kOK** indicates that the subscribe is accepted and OK should be sent. Any other
         *       value indicates that the subscribe is not accepted and the reason code and other
         *       fields will be set.
         */
        enum class ReasonCode : uint8_t
        {
            kOk = 0,
            kInternalError,
            kUnauthorized,
            kTimeout,
            kNotSupported,
            kMalformedAuthToken,
            kExpiredAuthToken,
            kDoesNotExist,
            kInvalidRange,
            kMalformedTrack,
            kDuplicateSubscription,
            kUninterested,
            kPrefixOverlap,
            kInvalidJoiningRequestId,
        };

        static ReasonCode FromErrorCode(messages::ErrorCode error_code);

        ReasonCode reason_code;

        bool is_publisher_initiated = false;

        std::optional<std::string> error_reason = std::nullopt;

        std::optional<messages::Location> largest_location = std::nullopt;
        messages::GroupOrder publisher_default_group_order = messages::GroupOrder::kAscending;
    };

    /**
     * @brief Response to received MOQT Publish message
     */
    struct PublishResponse
    {
        /**
         * @details **kOK** indicates that the publish is accepted and OK should be sent. Any other
         *       value indicates that the publish is not accepted and the reason code and other
         *       fields will be set.
         */
        enum class ReasonCode : uint8_t
        {
            kOk = 0,
            kInternalError,
            kNotSupported,
            kNotAuthorized,
            kRejected,
        };
        ReasonCode reason_code;

        std::optional<std::string> error_reason = std::nullopt;

        bool forward = true;
        std::optional<std::uint8_t> subscriber_priority{};
        std::optional<messages::GroupOrder> group_order{};
        messages::Filter filter = std::monostate{};
    };

    struct SubscribeNamespaceResponse
    {
        /**
         * @details **kOK** indicates that the subscribe namespace is accepted and OK should be sent. Any other
         *       value indicates that the subscribe namespace is not accepted and the reason code and other
         *       fields will be set.
         */
        enum class ReasonCode : uint8_t
        {
            kOk = 0,
            kInternalError,
            kNotSupported,
        };
        ReasonCode reason_code;

        // Matched tracks that will be advertised in response via PUBLISH_NAMESPACE.
        std::vector<TrackNamespace> namespaces;

        std::optional<std::string> error_reason = std::nullopt;
    };

    /**
     * @brief Response to received MOQT Fetch message
     */
    struct FetchResponse
    {
        /**
         * @details **kOK** indicates that the fetch is accepted and OK should be sent. Any other
         *       value indicates that the subscribe is not accepted and the reason code and other
         *       fields will be set.
         */
        enum class ReasonCode : uint8_t
        {
            kOk = 0,
            kInvalidRange,
            kNoObjects,
            kInternalError,
            // TODO: Expand reasons.
        };
        ReasonCode reason_code;

        std::optional<std::string> error_reason = std::nullopt;

        std::optional<messages::Location> largest_location = std::nullopt;
        messages::GroupOrder publisher_default_group_order = messages::GroupOrder::kAscending;
    };

    /**
     * @brief MoQ track base handler for tracks (subscribe/publish)
     *
     * @details Base MoQ track handler
     */
    class BaseTrackHandler
    {
      public:
        friend class Session;

        virtual ~BaseTrackHandler() = default;

        // --------------------------------------------------------------------------
        // Public API methods that normally should not be overridden
        // --------------------------------------------------------------------------

        BaseTrackHandler() = delete;

      protected:
        /**
         * @brief Track delegate constructor
         *
         * @param full_track_name       Full track name struct
         */
        BaseTrackHandler(const FullTrackName& full_track_name)
          : full_track_name_(full_track_name)
        {
        }

        FullTrackName full_track_name_;

        // --------------------------------------------------------------------------
        // Public Virtual API callback event methods to be overridden
        // --------------------------------------------------------------------------
      public:
        /**
         * @brief Sets the reqeust ID
         * @details MoQ instance sets the request id based on subscribe track method call. Request
         *      id is specific to the connection, so it must be set by the moq instance/connection.
         *
         * @param request_id          64bit request ID
         */
        void SetRequestId(std::optional<uint64_t> request_id) { request_id_ = request_id; }

        /**
         * @brief Get the request ID
         *
         * @return nullopt if not subscribed, otherwise the request ID
         */
        std::optional<uint64_t> GetRequestId() const noexcept { return request_id_; }

        /**
         * @brief Sets the data context Id
         * @param data_ctx_id               Data context Id for control messages
         */
        void SetDataContextId(std::uint64_t data_ctx_id) { data_ctx_id_ = data_ctx_id; }

        /**
         * @brief Return the data context Id
         * @return Data context id if set
         */
        std::optional<std::uint64_t> GetDataContextId() const noexcept { return data_ctx_id_; }

        /**
         * @brief Get the full track name
         *
         * @details Gets the full track name
         *
         * @return FullTrackName
         */
        FullTrackName GetFullTrackName() const noexcept { return { full_track_name_ }; }

        /**
         * @brief Get the connection ID
         */
        uint64_t GetConnectionId() const noexcept { return connection_handle_; };

        virtual void RequestOk(uint64_t request_id, const messages::Parameters& params);
        virtual void RequestUpdate(uint64_t request_id, const messages::Parameters& params);

        virtual void RequestError(messages::ErrorCode error_code, std::string reason);

      protected:
        /**
         * Set the transport to use.
         * @param transport The new transport for the handler to use.
         */
        void SetTransport(std::shared_ptr<Session> transport);

        const std::weak_ptr<Session>& GetTransport() const noexcept;

        // --------------------------------------------------------------------------
        // Internal
        // --------------------------------------------------------------------------
      private:
        /**
         * @brief Set the connection ID
         *
         * @details The MOQ Handler sets the connection ID
         */
        void SetConnectionId(uint64_t connection_handle) { connection_handle_ = connection_handle; };

        // --------------------------------------------------------------------------
        // Member variables
        // --------------------------------------------------------------------------
        std::uint64_t connection_handle_{ 0 }; // QUIC transport connection ID

        /**
         * request_id_ is the primary index/key for subscribe context/delegate storage.
         *   It is use as the request_id in MoQ related subscribes.  Request ID will adapt
         *   to received reqeust IDs, so the value will reflect either the received reqeust ID
         *   or the next one that increments from last received ID.
         */
        std::optional<uint64_t> request_id_;

        /**
         * Data context ID (transport data context) that control messages are to be sent
         */
        std::optional<std::uint64_t> data_ctx_id_{ std::nullopt };

        std::weak_ptr<Session> transport_;
    };

} // namespace moq
