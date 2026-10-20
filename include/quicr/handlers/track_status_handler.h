// SPDX-FileCopyrightText: Copyright (c) 2026 Cisco Systems
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

#include "quicr/handlers/track_handler.h"

namespace quicr {

    /**
     * @brief Represents one reusable TRACK_STATUS request.
     *
     * @details Pass the handler to `Session::RequestTrackStatus`. Getters are synchronized and may
     * be called from other threads. `StatusChanged` is invoked on session processing paths after a
     * response or error; stream-close and disconnect status changes currently do not invoke it.
     * A completed handler may be submitted again, but not while a response is pending.
     */
    class TrackStatusHandler : public TrackHandler
    {
      public:
        /// @brief Lifecycle state of the most recent query.
        enum class Status : std::uint8_t
        {
            kNotRequested,    ///< Ready to be passed to Session.
            kPendingResponse, ///< Waiting for response from the peer.
            kOk,              ///< Response received.
            kError,           ///< The peer rejected the query; GetError() contains its reason.
            kDoneByFin,       ///< The peer closed the stream without a response.
            kDoneByReset,     ///< The peer reset the stream without a response.
            kNotConnected,    ///< Transport disconnected.
        };

        /**
         * @brief Creates a track-status request handler.
         * @param full_track_name Track to query.
         * @return Shared ownership of a handler initially in `kNotRequested`.
         */
        static std::shared_ptr<TrackStatusHandler> Create(const FullTrackName& full_track_name)
        {
            return std::shared_ptr<TrackStatusHandler>(new TrackStatusHandler(full_track_name));
        }

        /// @return A thread-safe snapshot of the current request status.
        Status GetStatus() const;

        /// @return A copy of the successful response, or `std::nullopt` until/unless status is `kOk`.
        std::optional<TrackStatusResponse> GetResponse() const;

        /// @return A copy of the rejection, or `std::nullopt` until/unless status is `kError`.
        std::optional<Error<ErrorCode>> GetError() const;

        /**
         * @brief Reports completion with `kOk` or `kError`.
         * @param status New terminal status.
         * @note The default implementation does nothing. The callback runs outside the handler
         * mutex, so getters may safely be called from an override.
         */
        virtual void StatusChanged(Status status);

      protected:
        /**
         * @brief Constructs an unsubmitted request for a track.
         * @param full_track_name Track to query.
         */
        explicit TrackStatusHandler(const FullTrackName& full_track_name);

        /**
         * @brief Rejects an impossible generic REQUEST_OK path.
         * @param params Unused response parameters.
         * @throws std::logic_error Always; TRACK_STATUS responses are decoded directly by the session.
         */
        void RequestOkReceived(const messages::Parameters& params) override;

        /**
         * @brief Records a peer rejection while a response is pending.
         * @param error_code Peer-provided request error code.
         * @param reason Peer-provided diagnostic text, moved into the stored error.
         * @details A late error received after the pending state ended is ignored.
         */
        void RequestError(ErrorCode error_code, std::string reason) override;

        /**
         * @brief Rejects REQUEST_UPDATE for a TRACK_STATUS request.
         * @param params Unused update parameters.
         * @return Never returns.
         * @throws messages::ProtocolViolationException Always.
         */
        Reply<messages::Parameters, ErrorCode> RequestUpdateReceived(const messages::Parameters& params) override;

      private:
        friend class Session;

        void SetStatus(Status status);
        void ResponseReceived(TrackStatusResponse response);

        mutable std::mutex mutex_;
        Status status_ QUICR_GUARDED_BY(mutex_){ Status::kNotRequested };
        std::optional<TrackStatusResponse> response_ QUICR_GUARDED_BY(mutex_);
        std::optional<Error<ErrorCode>> error_ QUICR_GUARDED_BY(mutex_);
    };

} // namespace quicr
