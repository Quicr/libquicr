#include "quicr/subscribe_namespace_handler.h"
#include "quicr/detail/messages.h"
#include "quicr/detail/parameters.h"
#include "quicr/session.h"
#include "quicr/subscribe_track_handler.h"

quicr::SubscribeNamespaceHandler::SubscribeNamespaceHandler(const TrackNamespace& prefix,
                                                            const Mode mode,
                                                            const messages::Filter& filter)
  : BaseTrackHandler({ prefix, {} })
  , mode_(mode)
  , prefix_(prefix)
  , filter_(std::move(filter))
{
}

quicr::SubscribeNamespaceHandler::~SubscribeNamespaceHandler()
{
    const auto transport = GetTransport().lock();
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

quicr::TrackNamespace
quicr::SubscribeNamespaceHandler::ExpandSuffix(const TrackNamespace& suffix) const
{
    const auto& prefix_entries = GetPrefix().GetEntries();
    const auto& suffix_entries = suffix.GetEntries();
    std::vector<std::span<const uint8_t>> entries;
    entries.reserve(prefix_entries.size() + suffix_entries.size());
    entries.insert(entries.end(), prefix_entries.begin(), prefix_entries.end());
    entries.insert(entries.end(), suffix_entries.begin(), suffix_entries.end());
    return TrackNamespace{ std::span{ entries } };
}
