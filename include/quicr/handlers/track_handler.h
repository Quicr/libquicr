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

#include <memory>
#include <mutex>

namespace quicr {
    class Session;
    struct Request;

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
         * @brief Get the full track name
         *
         * @return A copy of the full track name.
         */
        FullTrackName GetFullTrackName() const noexcept { return { full_track_name_ }; }

        /**
         * @brief Whether this handler is registered with a live session.
         */
        bool IsRegistered() const;

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
         * @brief Gets the non-owning session handle.
         * @return A weak pointer to the registered session.
         */
        std::weak_ptr<Session> GetSession() const;

        /**
         * @brief Gets the request bound to this handler.
         * @return Bound request, or nullptr if the handler is unbound.
         */
        std::shared_ptr<Request> GetRequest() const;

        std::mutex binding_operation_mutex_;

      private:
        void BindRequest(std::shared_ptr<Request> request);

        bool UnbindRequest();

        void RegisterSession(const std::shared_ptr<Session>& session);

        void UnregisterSession(const std::shared_ptr<Session>& session);

        mutable std::mutex request_mutex_;
        std::weak_ptr<Request> request_ QUICR_GUARDED_BY(request_mutex_);

        mutable std::mutex registration_mutex_;
        std::weak_ptr<Session> registered_session_ QUICR_GUARDED_BY(registration_mutex_);
    };

} // namespace moq
