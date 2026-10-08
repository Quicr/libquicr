// SPDX-FileCopyrightText: Copyright (c) 2026 Cisco Systems
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

#include "quicr/handlers/track_handler.h"

namespace quicr {

    /**
     * @brief Request the details of a track.
     */
    class TrackStatusHandler : public TrackHandler
    {
      public:
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

        static std::shared_ptr<TrackStatusHandler> Create(const FullTrackName& full_track_name)
        {
            return std::shared_ptr<TrackStatusHandler>(new TrackStatusHandler(full_track_name));
        }

        Status GetStatus() const;
        std::optional<TrackStatusResponse> GetResponse() const;
        std::optional<Error<ErrorCode>> GetError() const;

        virtual void StatusChanged(Status status);

      protected:
        explicit TrackStatusHandler(const FullTrackName& full_track_name);

        void RequestOkReceived(const messages::Parameters& params) override;
        void RequestError(ErrorCode error_code, std::string reason) override;
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
