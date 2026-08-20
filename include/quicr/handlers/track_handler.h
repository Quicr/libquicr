// SPDX-FileCopyrightText: Copyright (c) 2024 Cisco Systems
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

#include "quicr/attributes.h"
#include "quicr/messages/messages.h"
#include "quicr/messages/parameters.h"
#include "quicr/track_name.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <vector>

namespace quicr {
    class Session;

    /**
     * @brief MoQ track base handler for tracks (subscribe/publish)
     *
     * @details Base MoQ track handler
     */
    class TrackHandler : public std::enable_shared_from_this<TrackHandler>
    {
      public:
        friend class Session;

        virtual ~TrackHandler() = default;

        // --------------------------------------------------------------------------
        // Public API methods that normally should not be overridden
        // --------------------------------------------------------------------------

        TrackHandler() = delete;

      protected:
        /**
         * @brief Track delegate constructor
         *
         * @param full_track_name       Full track name struct
         */
        TrackHandler(const FullTrackName& full_track_name)
          : full_track_name_(full_track_name)
        {
        }

        FullTrackName full_track_name_;

        // --------------------------------------------------------------------------
        // Public Virtual API callback event methods to be overridden
        // --------------------------------------------------------------------------
      public:
        /**
         * @brief Get the derived type of the track handler.
         * @tparam T Derived type of the track handler.
         * @return THe cast track handler as its derived type. Nullptr if the type is incorrect to the stored value.
         */
        template<typename T>
            requires std::is_base_of_v<TrackHandler, T>
        std::shared_ptr<T> Get()
        {
            return std::dynamic_pointer_cast<T>(shared_from_this());
        }

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
         * @brief Set the stream ID for the bidir request control stream.
         * @param request_stream_id Request stream's ID.
         */
        void SetRequestStreamId(uint64_t request_stream_id) { request_stream_id_ = request_stream_id; }

        /**
         * @brief Get the stream ID of the request control stream.
         * @return Request stream's ID.
         */
        std::optional<uint64_t> GetRequestStreamId() const noexcept { return request_stream_id_; }

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
        uint64_t GetConnectionId() const noexcept { return connection_id_; };

        /**
         * @brief Received an update for this handler's request.
         * Implementations MUST call ResolveRequestUpdate to acknowledge the request.
         * @param params The updated/new parameters for the request.
         */
        virtual void RequestUpdateReceived(const messages::Parameters& params) = 0;

        virtual void RequestError(messages::ErrorCode error_code, std::string reason);

      protected:
        /**
         * Received an OK for this handler's request.
         * @param params Parameters in the request.
         */
        virtual void RequestOkReceived(const messages::Parameters& params) = 0;

        /**
         * Set the transport to use.
         * @param transport The new transport for the handler to use.
         */
        void SetTransport(std::shared_ptr<Session> transport);

        const std::weak_ptr<Session>& GetSession() const noexcept;

        // --------------------------------------------------------------------------
        // Internal
        // --------------------------------------------------------------------------
      private:
        /**
         * @brief Set the connection ID
         *
         * @details The MOQ Handler sets the connection ID
         */
        void SetConnectionId(uint64_t connection_id) { connection_id_ = connection_id; };

        // --------------------------------------------------------------------------
        // Member variables
        // --------------------------------------------------------------------------
        std::uint64_t connection_id_{ 0 }; // QUIC transport connection ID

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

        /**
         * Stream ID of the bidirectional request control stream.
         */
        std::optional<uint64_t> request_stream_id_{ std::nullopt };

        std::weak_ptr<Session> session_;
    };

} // namespace moq
