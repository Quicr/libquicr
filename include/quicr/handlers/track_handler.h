// SPDX-FileCopyrightText: Copyright (c) 2024 Cisco Systems
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

#include "quicr/attributes.h"
#include "quicr/errors.h"
#include "quicr/messages/message_serialisation.h"
#include "quicr/messages/parameters.h"
#include "quicr/reply.h"
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

    /// @brief Internal stream type; exposed only through opaque shared ownership.
    class Stream;

    /**
     * @brief Common request state for track-related handlers.
     *
     * @details Handlers are intended to be managed by `std::shared_ptr`; `Get()` and session
     * registration rely on shared ownership. Request stream access is synchronized, while most
     * other fields are initialized or mutated by the session and are not generally thread-safe.
     */
    class TrackHandler : public std::enable_shared_from_this<TrackHandler>
    {
      public:
        friend class Session;

        /// Sends its own request updates, so it reaches the request stream directly. Friendship does not
        /// reach an application's subclass of it, which is the point.
        friend class SubscribeTrackHandler;

        /// @brief Releases this handler; shared stream/session handles are released automatically.
        virtual ~TrackHandler() = default;

        // --------------------------------------------------------------------------
        // Public API methods that normally should not be overridden
        // --------------------------------------------------------------------------

        /// @brief A track identity is mandatory.
        TrackHandler() = delete;

      protected:
        /**
         * @brief Constructs a handler for one full track name.
         * @param full_track_name Track identity copied into the handler.
         */
        TrackHandler(const FullTrackName& full_track_name)
          : full_track_name_(full_track_name)
        {
        }

        /// Full track identity retained for the handler's lifetime.
        FullTrackName full_track_name_;

        // --------------------------------------------------------------------------
        // Public Virtual API callback event methods to be overridden
        // --------------------------------------------------------------------------
      public:
        /**
         * @brief Get the derived type of the track handler.
         * @tparam T Derived type of the track handler.
         * @return Shared ownership cast to `T`, or null when the dynamic type is incompatible.
         * @throws std::bad_weak_ptr if this object is not currently owned by a `shared_ptr`.
         */
        template<typename T>
            requires std::is_base_of_v<TrackHandler, T>
        std::shared_ptr<T> Get()
        {
            return std::dynamic_pointer_cast<T>(shared_from_this());
        }

        /**
         * @brief Sets or clears the connection-scoped request ID.
         * @param request_id Request ID assigned by the session, or `std::nullopt` when unbound.
         * @note This setter is not synchronized; it is primarily for session state management.
         */
        void SetRequestId(std::optional<uint64_t> request_id) { request_id_ = request_id; }

        /**
         * @brief Get the request ID
         *
         * @return Current connection-scoped request ID, or `std::nullopt` when none is assigned.
         */
        std::optional<uint64_t> GetRequestId() const noexcept { return request_id_; }

        /**
         * @brief Get the stream ID of the request control stream.
         * @return Request stream's ID, or `std::nullopt` if no stream is attached.
         * @note Thread-safe with request-stream installation/removal.
         */
        std::optional<uint64_t> GetRequestStreamId() const;

        /**
         * @brief Get the full track name
         *
         * @return A copy of the full track name.
         */
        FullTrackName GetFullTrackName() const noexcept { return { full_track_name_ }; }

        /**
         * @brief Gets the transport connection identifier assigned by the session.
         * @return Connection ID, or the initial value zero before assignment.
         */
        uint64_t GetConnectionId() const noexcept { return connection_id_; };

        /**
         * @brief Received an update for this handler's request.
         *
         * @details The session acknowledges the update with the returned reply: success sends
         * REQUEST_UPDATE_OK with its parameters and failure sends REQUEST_ERROR. `Reply::Resolve`
         * invokes a deferred producer synchronously, so the producer must start any slow authorization,
         * I/O, or other asynchronous work itself. Completion is queued back through the transport. The
         * request and its stream may disappear before deferred completion, in which case no response is sent.
         *
         * @param params Borrowed updated parameters, valid only for this callback.
         * @return Immediate or deferred acknowledgement.
         * @throws messages::ProtocolViolationException if the update is invalid for this request.
         */
        virtual Reply<messages::Parameters, ErrorCode> RequestUpdateReceived(const messages::Parameters& params) = 0;

        /**
         * @brief Handles a REQUEST_ERROR for this handler.
         * @param error_code Peer-provided request error code.
         * @param reason Peer-provided reason, passed by value for optional retention.
         * @note The base implementation intentionally does nothing.
         */
        virtual void RequestError(ErrorCode error_code, std::string reason);

      protected:
        /**
         * @brief Handles REQUEST_OK for this handler's request.
         * @param params Borrowed response parameters, valid only for this callback.
         * @throws messages::ProtocolViolationException when parameters violate the request contract.
         */
        virtual void RequestOkReceived(const messages::Parameters& params) = 0;

        /**
         * @brief Sets the session used for control and data operations.
         * @param transport Session retained weakly; passing null clears effective access.
         * @note The caller must keep the session alive independently.
         */
        void SetTransport(std::shared_ptr<Session> transport);

        /**
         * @brief Gets the non-owning session handle.
         * @return Borrowed reference to the stored weak pointer, valid for this handler's lifetime.
         * Callers must lock it before use.
         */
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

        mutable std::mutex request_stream_mutex_; ///< Guards request_stream_

        std::weak_ptr<Session> session_;
    };

} // namespace moq
