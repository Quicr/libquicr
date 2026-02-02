#include "quicr/publish_namespace_handler.h"

#include "quicr/detail/transport.h"

quicr::PublishNamespaceHandler::PublishNamespaceHandler(const TrackNamespace& prefix)
  : prefix_(prefix)
{
}

quicr::PublishNamespaceHandler::~PublishNamespaceHandler()
{
    const auto& transport = transport_.lock();
    if (!transport) {
        return;
    }

    for (const auto& [_, handler] : handlers_) {
        transport->UnpublishTrack(connection_handle_, handler);
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

std::weak_ptr<quicr::PublishTrackHandler>
quicr::PublishNamespaceHandler::PublishTrack(const FullTrackName& full_track_name,
                                             TrackMode track_mode,
                                             uint8_t default_priority,
                                             uint32_t default_ttl)
{
    if (!full_track_name.name_space.HasSamePrefix(GetPrefix())) {
        throw std::invalid_argument("New Publish track MUST have the same prefix as owning Namespace Handler");
    }

    auto& handler = handlers_[TrackHash(full_track_name).track_fullname_hash] =
      PublishTrackHandler::Create(full_track_name, track_mode, default_priority, default_ttl);

    const auto& transport = transport_.lock();
    if (!transport) {
        throw std::runtime_error("Cannot create publish track when transport is null");
    }

    transport->PublishTrack(connection_handle_, handler);

    return handler;
}
