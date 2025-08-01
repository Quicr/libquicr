// SPDX-FileCopyrightText: Copyright (c) 2024 Cisco Systems
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

#include "quicr/common.h"
#include "quicr/detail/ctrl_message_types.h"
#include "quicr/track_name.h"

#include <optional>
#include <span>
#include <vector>

namespace quicr {

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
     * @brief Response to received MOQT Subscribe message
     */
    struct SubscribeResponse
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
            kInvalidRange,
        };
        ReasonCode reason_code;

        std::optional<std::string> error_reason = std::nullopt;

        std::optional<messages::Location> largest_location = std::nullopt;
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
        };
        ReasonCode reason_code;

        std::optional<std::string> error_reason = std::nullopt;

        std::optional<messages::Location> largest_location = std::nullopt;
    };

    /**
     * @brief MoQ track base handler for tracks (subscribe/publish)
     *
     * @details Base MoQ track handler
     */
    class BaseTrackHandler
    {
      public:
        friend class Transport;
        friend class Server;
        friend class Client;

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
         * @param request_id          62bit request ID
         */
        void SetRequestId(std::optional<uint64_t> request_id) { request_id_ = request_id; }

        /**
         * @brief Get the request ID
         *
         * @return nullopt if not subscribed, otherwise the request ID
         */
        std::optional<uint64_t> GetRequestId() const noexcept { return request_id_; }

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

        ConnectionHandle connection_handle_; // QUIC transport connection ID

        /**
         * request_id_ is the primary index/key for subscribe context/delegate storage.
         *   It is use as the request_id in MoQ related subscribes.  Request ID will adapt
         *   to received reqeust IDs, so the value will reflect either the received reqeust ID
         *   or the next one that increments from last received ID.
         */
        std::optional<uint64_t> request_id_;
    };

} // namespace moq
