#include "quicr/handlers/track_handler.h"
#include "quicr/session.h"

namespace quicr {
    void TrackHandler::SetTransport(std::shared_ptr<Session> transport)
    {
        session_ = transport;
    }

    const std::weak_ptr<Session>& TrackHandler::GetSession() const noexcept
    {
        return session_;
    }

    void TrackHandler::RequestError(messages::ErrorCode, std::string) {}
}
