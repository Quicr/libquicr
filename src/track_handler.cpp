#include "quicr/track_handler.h"
#include "quicr/transport.h"

namespace quicr {
    void TrackHandler::SetTransport(std::shared_ptr<Transport> transport)
    {
        transport_ = transport;
    }

    const std::weak_ptr<Transport>& TrackHandler::GetTransport() const noexcept
    {
        return transport_;
    }
}
