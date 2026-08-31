// SPDX-FileCopyrightText: Copyright (c) 2024 Cisco Systems
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

#include "quicr/attributes.h"
#include "quicr/messages/messages.h"
#include "quicr/messages/parameters.h"
#include "quicr/stream.h"
#include "quicr/track_name.h"
#include "quicr/utilities/thread_safety.h"

#include <cstdint>
#include <memory>
#include <mutex>
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
         * @brief Set the bidir request control stream.
         * @param request_stream Handle to the request stream.
         */
        void SetRequestStream(const std::shared_ptr<Stream>& request_stream)
        {
            std::lock_guard lock(request_stream_mutex_);
            request_stream_ = request_stream;
        }

        /**
         * @brief Get the request control stream.
         *
         * @details Returns an owning handle, which keeps the stream alive for as long as the caller
         *      holds it. Hold the result for the duration of an operation rather than calling this
         *      repeatedly, so a null check and the use that follows cannot disagree.
         *
         * @return Handle to the request stream, or nullptr if unset.
         */
        std::shared_ptr<Stream> GetRequestStream() const
        {
            std::lock_guard lock(request_stream_mutex_);
            return request_stream_;
        }

        /**
         * @brief Get the stream ID of the request control stream.
         * @return Request stream's ID, or nullopt if unset.
         */
        std::optional<uint64_t> GetRequestStreamId() const
        {
            std::lock_guard lock(request_stream_mutex_);
            if (request_stream_ == nullptr) {
                return std::nullopt;
            }
            return request_stream_->GetStreamId();
        }

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
         * The bidirectional request control stream, which control messages for this request go out on.
         *
         * @details Held for the life of the request so that control messages go out without a lookup.
         *      The transport may close the stream underneath this handle, which `Stream::IsOpen`
         *      reports and the transport rejects on send.
         *
         *      The session installs and releases the handle while application threads read it, so
         *      access is guarded. Never call into the session while holding the mutex, so that the
         *      session lock is always taken first.
         */
        std::shared_ptr<Stream> request_stream_ QUICR_GUARDED_BY(request_stream_mutex_);

        /// Guards request_stream_
        mutable std::mutex request_stream_mutex_;

        std::weak_ptr<Session> session_;
    };

} // namespace moq
