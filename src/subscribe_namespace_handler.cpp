#include "quicr/subscribe_namespace_handler.h"
#include "quicr/detail/messages.h"
#include "quicr/detail/transport.h"
#include "quicr/subscribe_track_handler.h"

quicr::SubscribeNamespaceHandler::SubscribeNamespaceHandler(const TrackNamespace& prefix,
                                                            const messages::Filter& filter)
  : BaseTrackHandler({ prefix, {} })
  , prefix_(prefix)
  , filter_(std::move(filter))
{
}

quicr::SubscribeNamespaceHandler::~SubscribeNamespaceHandler()
{
    const auto& transport = transport_.lock();
    if (!transport) {
        return;
    }

#if 0
    /**
     * TODO: Need to revist this as the draft suggests subscribe namespace done should not result
     *       in unsubscribe of tracks
     */
    for (const auto& [_, handler] : handlers_) {
        transport->UnsubscribeTrack(connection_handle_, handler);
    }
#endif
}

void
quicr::SubscribeNamespaceHandler::StatusChanged(Status status)
{
    auto th = quicr::TrackHash({ GetPrefix(), {} });

    switch (status) {
        case Status::kOk:
            SPDLOG_TRACE("Subscription to namespace with hash: {} status changed to OK", th.track_namespace_hash);
            break;
        case Status::kNotSubscribed:
            SPDLOG_TRACE("Subscription to namespace with hash: {} status changed to NOT_SUBSCRIBED",
                         th.track_namespace_hash);
            break;
        case Status::kError:
            if (error_ != std::nullopt) {
                SPDLOG_ERROR("Subscription to namespace with hash: {} status changed to ERROR: {}",
                             th.track_namespace_hash,
                             std::string(error_->second.begin(), error_->second.end()));
            } else {
                SPDLOG_ERROR("Subscription to namespace with hash: {} status changed to unknown ERROR");
            }
            break;
        default:
            break;
    }
}

std::shared_ptr<quicr::SubscribeTrackHandler>
quicr::SubscribeNamespaceHandler::NewTrackReceived(const messages::PublishAttributes& attributes) const
{
    return SubscribeTrackHandler::Create(
      attributes.track_full_name, attributes.priority, messages::GroupOrder::kAscending, std::monostate{}, true);
}
