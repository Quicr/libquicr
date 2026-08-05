#include "quicr/handlers/subscribe_namespace_handler.h"
#include "quicr/handlers/subscribe_track_handler.h"
#include "quicr/log.h"
#include "quicr/messages/messages.h"
#include "quicr/messages/parameters.h"
#include "quicr/session.h"

quicr::SubscribeNamespaceHandler::SubscribeNamespaceHandler(const TrackNamespace& prefix,
                                                            const Mode mode,
                                                            const messages::Filter& filter)
  : TrackHandler({ prefix, {} })
  , mode_(mode)
  , prefix_(prefix)
  , filter_(std::move(filter))
{
}

quicr::SubscribeNamespaceHandler::~SubscribeNamespaceHandler()
{
    const auto& transport = GetSession().lock();
    if (!transport) {
        return;
    }

#if 0
    /**
     * TODO: Need to revist this as the draft suggests subscribe namespace done should not result
     *       in unsubscribe of tracks
     */
    for (const auto& [_, handler] : handlers_) {
        transport->UnsubscribeTrack(connection_id_, handler);
    }
#endif
}

void
quicr::SubscribeNamespaceHandler::StatusChanged(Status status)
{
}

void
quicr::SubscribeNamespaceHandler::RequestOkReceived(const messages::Parameters& params)
{
    messages::ValidateParameters(params, {});
    SetStatus(Status::kOk);
}

void
quicr::SubscribeNamespaceHandler::RequestUpdateReceived([[maybe_unused]] const messages::Parameters& params)
{
    throw messages::ProtocolViolationException("Unexpected REQUEST_UPDATE");
}
