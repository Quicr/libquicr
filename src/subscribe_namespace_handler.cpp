#include "quicr/subscribe_namespace_handler.h"

void
quicr::SubscribeNamespaceHandler::StatusChanged(Status status)
{
    auto th = quicr::TrackHash({ GetNamespacePrefix(), {} });

    switch (status) {
        case Status::kOk:
            SPDLOG_INFO("Subscription to namespace with hash: {} status changed to OK", th.track_namespace_hash);
            break;
        case Status::kNotSubscribed:
            SPDLOG_WARN("Subscription to namespace with hash: {} status changed to NOT_SUBSCRIBED",
                        th.track_namespace_hash);
            break;
        case Status::kError:
            SPDLOG_WARN("Subscription to namespace with hash: {} status changed to ERROR: {}",
                        th.track_namespace_hash,
                        std::string(error_->second.begin(), error_->second.end()));
            break;
        default:
            break;
    }
}
