// SPDX-FileCopyrightText: Copyright (c) 2026 Cisco Systems
// SPDX-License-Identifier: BSD-2-Clause

#include "quicr/handlers/track_status_handler.h"

namespace quicr {

    TrackStatusHandler::TrackStatusHandler(const FullTrackName& full_track_name)
      : TrackHandler(full_track_name)
    {
    }

    TrackStatusHandler::Status TrackStatusHandler::GetStatus() const
    {
        std::lock_guard _(mutex_);
        return status_;
    }

    std::optional<TrackStatusResponse> TrackStatusHandler::GetResponse() const
    {
        std::lock_guard _(mutex_);
        return response_;
    }

    std::optional<Error<ErrorCode>> TrackStatusHandler::GetError() const
    {
        std::lock_guard _(mutex_);
        return error_;
    }

    void TrackStatusHandler::StatusChanged(Status) {}

    void TrackStatusHandler::SetStatus(Status status)
    {
        std::lock_guard _(mutex_);
        if (status == Status::kPendingResponse) {
            response_.reset();
            error_.reset();
        }
        status_ = status;
    }

    void TrackStatusHandler::ResponseReceived(TrackStatusResponse response)
    {
        {
            std::lock_guard _(mutex_);
            if (status_ != Status::kPendingResponse) {
                return;
            }
            response_ = std::move(response);
            status_ = Status::kOk;
        }
        StatusChanged(Status::kOk);
    }

    void TrackStatusHandler::RequestError(ErrorCode error_code, std::string reason)
    {
        {
            std::lock_guard lock(mutex_);
            if (status_ != Status::kPendingResponse) {
                return;
            }
            error_ = Error<ErrorCode>{ error_code, std::move(reason) };
            status_ = Status::kError;
        }
        StatusChanged(Status::kError);
    }

    void TrackStatusHandler::RequestOkReceived(const messages::Parameters& params)
    {
        throw std::logic_error("TrackStatusHandler should not receive RequestOkReceived");
    }

    Reply<messages::Parameters, ErrorCode> TrackStatusHandler::RequestUpdateReceived(const messages::Parameters&)
    {
        throw messages::ProtocolViolationException("Unexpected REQUEST_UPDATE for track status");
    }

} // namespace quicr
