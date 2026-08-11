#include "quicr/handlers/base_track_handler.h"
#include "quicr/session.h"
#include <spdlog/spdlog.h>

namespace quicr {
    void BaseTrackHandler::SetTransport(std::shared_ptr<Session> transport)
    {
        transport_ = transport;
    }

    const std::weak_ptr<Session>& BaseTrackHandler::GetTransport() const noexcept
    {
        return transport_;
    }

    void BaseTrackHandler::RequestUpdateReceived(const messages::Parameters& params)
    {
        std::optional<messages::Parameters> active_update;
        {
            std::lock_guard lock(request_update_mutex_);
            if (request_update_state_ == RequestUpdateState::kTerminal) {
                // Ignore.
                SPDLOG_WARN("Ignoring request update for terminal request");
                return;
            }

            // Store in queue.
            pending_request_updates_.push_back(params);

            // If we can immediately apply, do so.
            if (request_update_state_ == RequestUpdateState::kIdle) {
                request_update_state_ = RequestUpdateState::kAwaitingResolution;
                active_update = pending_request_updates_.front();
            }
        }
        if (active_update.has_value()) {
            ApplyRequestUpdate(*active_update);
        }
    }

    void BaseTrackHandler::ResolveRequestUpdate(const std::optional<quicr::RequestError>& error)
    {
        {
            // Check/update queue state.
            std::lock_guard lock(request_update_mutex_);
            if (pending_request_updates_.empty() || request_update_state_ != RequestUpdateState::kAwaitingResolution) {
                throw std::logic_error("ResolveRequestUpdate called with no update pending");
            }
            request_update_state_ = RequestUpdateState::kResolving;
        }

        // Get the session for this request.
        const auto session = GetTransport().lock();
        if (!session) {
            ClearRequestUpdates();
            return;
        }

        auto live_handler = session->ResolveRequestUpdate(*this, error);
        if (!live_handler) {
            ClearRequestUpdates();
            return;
        }

        // Complete the current update.
        bool activate_next = false;
        {
            std::lock_guard lock(request_update_mutex_);
            if (error.has_value()) {
                // We're done.
                pending_request_updates_.clear();
                request_update_state_ = RequestUpdateState::kTerminal;
            } else {
                pending_request_updates_.pop_front();
                if (pending_request_updates_.empty()) {
                    request_update_state_ = RequestUpdateState::kIdle;
                } else {
                    request_update_state_ = RequestUpdateState::kPendingApply;
                    activate_next = true;
                }
            }
        }

        session->DispatchRequestUpdateCompletion(std::move(live_handler), error.has_value(), activate_next);
    }

    void BaseTrackHandler::ApplyNextRequestUpdate()
    {
        // Apply the next queued update, if any.
        messages::Parameters params;
        {
            std::lock_guard lock(request_update_mutex_);
            if (request_update_state_ != RequestUpdateState::kPendingApply || pending_request_updates_.empty()) {
                return;
            }
            request_update_state_ = RequestUpdateState::kAwaitingResolution;
            params = pending_request_updates_.front();
        }
        ApplyRequestUpdate(params);
    }

    void BaseTrackHandler::ClearRequestUpdates()
    {
        std::lock_guard lock(request_update_mutex_);
        pending_request_updates_.clear();
        request_update_state_ = RequestUpdateState::kTerminal;
    }

    RequestResponse::ReasonCode RequestResponse::FromErrorCode(messages::ErrorCode error_code)
    {
        switch (error_code) {
            case messages::ErrorCode::kInternalError:
                return ReasonCode::kInternalError;
            case messages::ErrorCode::kUnauthorized:
                return ReasonCode::kUnauthorized;
            case messages::ErrorCode::kTimeout:
                return ReasonCode::kTimeout;
            case messages::ErrorCode::kNotSupported:
                return ReasonCode::kNotSupported;
            case messages::ErrorCode::kMalformedAuthToken:
                return ReasonCode::kMalformedAuthToken;
            case messages::ErrorCode::kExpiredAuthToken:
                return ReasonCode::kExpiredAuthToken;
            case messages::ErrorCode::kDoesNotExist:
                return ReasonCode::kDoesNotExist;
            case messages::ErrorCode::kInvalidRange:
                return ReasonCode::kInvalidRange;
            case messages::ErrorCode::kMalformedTrack:
                return ReasonCode::kMalformedTrack;
            case messages::ErrorCode::kDuplicateSubscription:
                return ReasonCode::kDuplicateSubscription;
            case messages::ErrorCode::kUninterested:
                return ReasonCode::kUninterested;
            case messages::ErrorCode::kPrefixOverlap:
                return ReasonCode::kPrefixOverlap;
            case messages::ErrorCode::kInvalidJoiningRequestId:
                return ReasonCode::kInvalidJoiningRequestId;
        }

        return ReasonCode::kOk;
    }

    void BaseTrackHandler::RequestError(messages::ErrorCode, std::string) {}
}
