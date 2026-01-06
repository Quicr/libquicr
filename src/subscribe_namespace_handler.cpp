#include "quicr/subscribe_namespace_handler.h"
#include "quicr/detail/messages.h"
#include "quicr/detail/transport.h"
#include "quicr/subscribe_track_handler.h"

quicr::SubscribeNamespaceHandler::SubscribeNamespaceHandler(const TrackNamespace& prefix)
  : prefix_(prefix)
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
            SPDLOG_INFO("Subscription to namespace with hash: {} status changed to OK", th.track_namespace_hash);
            break;
        case Status::kNotSubscribed:
            SPDLOG_WARN("Subscription to namespace with hash: {} status changed to NOT_SUBSCRIBED",
                        th.track_namespace_hash);
            break;
        case Status::kError:
            if (error_ != std::nullopt) {
                SPDLOG_WARN("Subscription to namespace with hash: {} status changed to ERROR: {}",
                            th.track_namespace_hash,
                            std::string(error_->second.begin(), error_->second.end()));
            } else {
                SPDLOG_WARN("Subscription to namespace with hash: {} status changed to unknown ERROR");
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
quicr::SubscribeNamespaceHandler::AcceptNewTrack(const ConnectionHandle& connection_handle,
                                                 quicr::messages::RequestID request_id,
                                                 const messages::PublishAttributes& attributes)
{
    const auto& transport = transport_.lock();
    if (!transport) {
        return;
    }

    const auto& [track_handler_it, is_new] = handlers_.try_emplace(attributes.track_alias, CreateHandler(attributes));

    if (!is_new) {
        SPDLOG_WARN("Track already handled by existing by namespace handler"); // TODO: Specify which
        return;
    }

    connection_handle_ = connection_handle;

    const auto& [_, track_handler] = *track_handler_it;

    track_handler->SetRequestId(request_id);
    track_handler->SetReceivedTrackAlias(attributes.track_alias);
    track_handler->SetPriority(attributes.priority);
    track_handler->SetDeliveryTimeout(attributes.delivery_timeout);
    track_handler->SupportNewGroupRequest(attributes.dynamic_groups);

    transport->SubscribeTrack(connection_handle_, track_handler);
}
