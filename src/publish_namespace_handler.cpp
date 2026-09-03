#include "quicr/handlers/publish_namespace_handler.h"
#include "quicr/log.h"
#include "quicr/messages/parameters.h"
#include "quicr/session.h"

#include <ranges>

quicr::PublishNamespaceHandler::PublishNamespaceHandler(const TrackNamespace& prefix)
  : TrackHandler({ prefix, {} })
  , prefix_(prefix)
{
}

quicr::PublishNamespaceHandler::~PublishNamespaceHandler()
{
    const auto& transport = GetSession().lock();
    if (!transport) {
        return;
    }

    for (const auto& [_, handler] : handlers_) {
        if (handler) {
            transport->UnpublishTrack(handler);
        }
    }
}

void
quicr::PublishNamespaceHandler::StatusChanged(Status)
{
}

void
quicr::PublishNamespaceHandler::PublishTrack(std::shared_ptr<PublishTrackHandler> handler)
{
    if (!handler->GetFullTrackName().name_space.HasSamePrefix(GetPrefix())) {
        throw std::invalid_argument("New Publish track MUST have the same prefix as owning Namespace Handler");
    }

    handlers_.emplace(TrackHash(handler->GetFullTrackName()).track_fullname_hash, handler);

    const auto& transport = GetSession().lock();
    if (!transport) {
        throw std::runtime_error("Cannot create publish track when transport is null");
    }

    transport->PublishTrack(std::move(handler));
}

void
quicr::PublishNamespaceHandler::UnPublishTrack(std::shared_ptr<PublishTrackHandler> handler)
{
    const auto& transport = GetSession().lock();
    if (!transport) {
        throw std::runtime_error("Cannot create publish track when transport is null");
    }

    transport->UnpublishTrack(handler);
    handlers_.erase(TrackHash(handler->GetFullTrackName()).track_fullname_hash);
}

quicr::PublishTrackHandler::PublishObjectStatus
quicr::PublishNamespaceHandler::PublishObject(uint64_t track_alias,
                                              const ObjectHeaders& object_headers,
                                              BytesSpan data,
                                              std::optional<messages::StreamHeaderProperties> stream_mode)
{
    if (const auto pub_it = handlers_.find(track_alias); pub_it != handlers_.end()) {
        return pub_it->second->PublishObject(object_headers, data, stream_mode);
    }

    return PublishTrackHandler::PublishObjectStatus::kInternalError;
}

void
quicr::PublishNamespaceHandler::RequestOkReceived(const messages::Parameters& params)
{
    messages::ValidateParameters(params, {});
    SetStatus(Status::kOk);
}

quicr::Reply<quicr::messages::Parameters, quicr::ErrorCode>
quicr::PublishNamespaceHandler::RequestUpdateReceived(const messages::Parameters&)
{
    // TODO: See moq-wg #1769.
    throw messages::ProtocolViolationException("Unexpected REQUEST_UPDATE");
}
