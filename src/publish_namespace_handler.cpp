#include "quicr/publish_namespace_handler.h"
#include "quicr/detail/transport.h"

#include <ranges>

quicr::PublishNamespaceHandler::PublishNamespaceHandler(const TrackNamespace& prefix)
  : BaseTrackHandler({ prefix, {} })
  , prefix_(prefix)
{
}

quicr::PublishNamespaceHandler::~PublishNamespaceHandler()
{
    const auto& transport = transport_.lock();
    if (!transport) {
        return;
    }

    // NOLINTNEXTLINE(clang-diagnostic-error)
    for (const auto& [_, handler] : handlers_) {
        if (handler) {
            transport->UnpublishTrack(connection_handle_, handler);
        }
    }
}

void
quicr::PublishNamespaceHandler::StatusChanged(Status status)
{

    auto th = quicr::TrackHash({ GetPrefix(), {} });

    switch (status) {
        case Status::kOk:
            SPDLOG_INFO("Publication to namespace with hash: {} status changed to OK", th.track_namespace_hash);
            break;
        case Status::kNotConnected:
            SPDLOG_WARN("Publication to namespace with hash: {} status changed to NOT_CONNECTED",
                        th.track_namespace_hash);
            break;
        case Status::kNotPublished:
            SPDLOG_WARN("Publication to namespace with hash: {} status changed to NOT_PUBLISHED",
                        th.track_namespace_hash);
            break;
        case Status::kPendingResponse:
            SPDLOG_INFO("Publication to namespace with hash: {} status changed to PENDING_RESPONSE",
                        th.track_namespace_hash);
            break;
        case Status::kPublishNotAuthorized:
            SPDLOG_ERROR("Publication to namespace with hash: {} status changed to PUBLISH_NOT_AUTHORIZED",
                         th.track_namespace_hash);
            break;
        case Status::kSendingDone:
            SPDLOG_INFO("Publication to namespace with hash: {} status changed to SENDING_DONE",
                        th.track_namespace_hash);
        case Status::kError:
            if (error_ != std::nullopt) {
                SPDLOG_ERROR("Publication to namespace with hash: {} status changed to ERROR: {}",
                             th.track_namespace_hash,
                             std::string(error_->second.begin(), error_->second.end()));
            } else {
                SPDLOG_ERROR("Publication to namespace with hash: {} status changed to unknown ERROR");
            }
            break;
    }
}

void
quicr::PublishNamespaceHandler::PublishTrack(std::shared_ptr<PublishTrackHandler> handler)
{
    if (!handler->GetFullTrackName().name_space.HasSamePrefix(GetPrefix())) {
        throw std::invalid_argument("New Publish track MUST have the same prefix as owning Namespace Handler");
    }

    handlers_.emplace(TrackHash(handler->GetFullTrackName()).track_fullname_hash, handler);

    const auto& transport = transport_.lock();
    if (!transport) {
        throw std::runtime_error("Cannot create publish track when transport is null");
    }

    transport->PublishTrack(connection_handle_, std::move(handler));
}
