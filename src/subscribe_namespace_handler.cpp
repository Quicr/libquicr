#include "quicr/subscribe_namespace_handler.h"
#include "quicr/detail/messages.h"
#include "quicr/detail/transport.h"
#include "quicr/subscribe_track_handler.h"

quicr::SubscribeNamespaceHandler::SubscribeNamespaceHandler(const TrackNamespace& prefix)
  : BaseTrackHandler({ prefix, {} })
  , prefix_(prefix)
{
}

quicr::SubscribeNamespaceHandler::~SubscribeNamespaceHandler()
{
    const auto& transport = transport_.lock();
    if (!transport) {
        return;
    }

    for (const auto& [_, handler] : handlers_) {
        transport->UnsubscribeTrack(connection_handle_, handler);
    }
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

bool
quicr::SubscribeNamespaceHandler::IsTrackAcceptable(const FullTrackName&) const
{
    return false;
}

std::shared_ptr<quicr::SubscribeTrackHandler>
quicr::SubscribeNamespaceHandler::CreateHandler(const quicr::messages::PublishAttributes& attributes)

{
    return SubscribeTrackHandler::Create(attributes.track_full_name, attributes.priority);
}

void
quicr::SubscribeNamespaceHandler::AcceptNewTrack([[maybe_unused]] ConnectionHandle connection_handle,
                                                 [[maybe_unused]] quicr::messages::RequestID request_id,
                                                 [[maybe_unused]] const messages::PublishAttributes& attributes)
{
}
