#include "quicr/handlers/track_handler.h"
#include "quicr/session.h"
#include "stream.h"

namespace quicr {
    std::optional<uint64_t> TrackHandler::GetRequestStreamId() const
    {
        std::lock_guard lock(request_stream_mutex_);
        if (request_stream_ == nullptr) {
            return std::nullopt;
        }
        return request_stream_->GetStreamId();
    }

    void TrackHandler::SetTransport(std::shared_ptr<Session> transport)
    {
        session_ = transport;
    }

    const std::weak_ptr<Session>& TrackHandler::GetSession() const noexcept
    {
        return session_;
    }

    void TrackHandler::RequestError(ErrorCode, std::string) {}
}
