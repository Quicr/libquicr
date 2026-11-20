// SPDX-FileCopyrightText: Copyright (c) 2026 Cisco Systems
// SPDX-License-Identifier: BSD-2-Clause

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
    UnPublishAllTracks();
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

    const auto& transport = GetSession().lock();
    if (!transport) {
        throw std::runtime_error("Cannot create publish track when transport is null");
    }

    const auto track_hash = TrackHash(handler->GetFullTrackName()).track_fullname_hash;
    {
        std::lock_guard lock(handlers_mutex_);
        if (!handlers_.emplace(track_hash, handler).second) {
            throw std::logic_error("Track is already published by this namespace");
        }
    }

    try {
        transport->PublishTrack(handler);
    } catch (...) {
        std::lock_guard lock(handlers_mutex_);
        const auto handler_it = handlers_.find(track_hash);
        if (handler_it != handlers_.end() && handler_it->second == handler) {
            handlers_.erase(handler_it);
        }
        throw;
    }
}

void
quicr::PublishNamespaceHandler::UnPublishTrack(std::shared_ptr<PublishTrackHandler> handler)
{
    const auto& transport = GetSession().lock();
    if (!transport) {
        throw std::runtime_error("Cannot create publish track when transport is null");
    }

    {
        std::lock_guard lock(handlers_mutex_);
        const auto handler_it = handlers_.find(TrackHash(handler->GetFullTrackName()).track_fullname_hash);
        if (handler_it == handlers_.end() || handler_it->second != handler) {
            return;
        }

        handlers_.erase(handler_it);
    }

    transport->UnpublishTrack(handler);
}

void
quicr::PublishNamespaceHandler::UnPublishAllTracks() noexcept
{
    decltype(handlers_) handlers;
    {
        std::lock_guard lock(handlers_mutex_);
        handlers.swap(handlers_);
    }

    const auto& transport = GetSession().lock();
    if (!transport) {
        return;
    }

    for (const auto& [_, handler] : handlers) {
        if (handler) {
            try {
                transport->UnpublishTrack(handler);
            } catch (...) {
                // Cleanup is final. Session::UnpublishTrack unregisters the child before propagating an error.
            }
        }
    }
}

quicr::PublishTrackHandler::PublishObjectStatus
quicr::PublishNamespaceHandler::PublishObject(uint64_t track_alias,
                                              const ObjectHeaders& object_headers,
                                              BytesSpan data,
                                              std::optional<messages::StreamHeaderProperties> stream_mode)
{
    std::shared_ptr<PublishTrackHandler> handler;
    {
        std::lock_guard lock(handlers_mutex_);
        const auto handler_it = handlers_.find(track_alias);
        if (handler_it != handlers_.end()) {
            handler = handler_it->second;
        }
    }

    if (handler) {
        return handler->PublishObject(object_headers, data, stream_mode);
    }

    return PublishTrackHandler::PublishObjectStatus::kInternalError;
}

void
quicr::PublishNamespaceHandler::RequestOkReceived([[maybe_unused]] const messages::Parameters& params)
{
    // TODO: should we messages::ValidateParameters(params, {});
    SetStatus(Status::kOk);
}

quicr::Reply<quicr::messages::Parameters, quicr::ErrorCode>
quicr::PublishNamespaceHandler::RequestUpdateReceived(const messages::Parameters&)
{
    // TODO: See moq-wg #1769.
    throw messages::ProtocolViolationException("Unexpected REQUEST_UPDATE");
}
