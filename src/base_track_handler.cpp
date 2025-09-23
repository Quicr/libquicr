#include "quicr/base_track_handler.h"
#include "quicr/transport.h"

namespace quicr {
    void BaseTrackHandler::SetTransport(std::shared_ptr<Transport> transport)
    {
        transport_ = transport;
    }

    const std::weak_ptr<Transport>& BaseTrackHandler::GetTransport() const noexcept
    {
        return transport_;
    }
}
