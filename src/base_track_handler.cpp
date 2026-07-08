#include "quicr/handlers/base_track_handler.h"
#include "quicr/session.h"

namespace quicr {
    void BaseTrackHandler::SetTransport(std::shared_ptr<Session> transport)
    {
        transport_ = transport;
    }

    const std::weak_ptr<Session>& BaseTrackHandler::GetTransport() const noexcept
    {
        return transport_;
    }

    void BaseTrackHandler::ResolveRequestUpdate(const std::optional<quicr::RequestError>& error)
    {
        // Consume a pending request.
        auto pending = pending_request_updates_.load(std::memory_order_acquire);
        while (pending != 0) {
            if (pending_request_updates_.compare_exchange_weak(
                  pending, pending - 1, std::memory_order_acq_rel, std::memory_order_acquire)) {

                // Resolve.
                if (const auto transport = GetTransport().lock()) {
                    transport->ResolveRequestUpdate(*this, error);
                }
                return;
            }
        }
        throw std::logic_error("ResolveRequestUpdate called with no update pending");
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
