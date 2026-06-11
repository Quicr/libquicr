#include "quicr/detail/base_track_handler.h"
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

    void BaseTrackHandler::RequestOk(uint64_t, const messages::Parameters&, const messages::TrackExtensions&) {}

    void BaseTrackHandler::RequestUpdate(uint64_t, const messages::Parameters&) {}

    void BaseTrackHandler::RequestError(messages::ErrorCode, std::string) {}
}
