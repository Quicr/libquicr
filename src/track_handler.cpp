// SPDX-FileCopyrightText: Copyright (c) 2026 Cisco Systems
// SPDX-License-Identifier: BSD-2-Clause

#include "quicr/handlers/track_handler.h"
#include "quicr/session.h"
#include "stream.h"

#include <stdexcept>

namespace quicr {
    std::optional<uint64_t> TrackHandler::GetRequestStreamId() const
    {
        std::lock_guard lock(request_stream_mutex_);
        if (request_stream_ == nullptr) {
            return std::nullopt;
        }
        return request_stream_->GetStreamId();
    }

    std::shared_ptr<Connection> TrackHandler::GetConnection() const
    {
        std::lock_guard lock(request_stream_mutex_);
        return request_stream_ ? request_stream_->GetConnection() : nullptr;
    }

    std::weak_ptr<Session> TrackHandler::GetSession() const
    {
        std::lock_guard lock(registration_mutex_);
        return registered_session_;
    }

    void TrackHandler::RegisterSession(const std::shared_ptr<Session>& session)
    {
        if (!session) {
            throw std::invalid_argument("Session cannot be null");
        }

        std::lock_guard lock(registration_mutex_);
        if (!registered_session_.expired()) {
            throw std::logic_error("Handler is already registered with a session");
        }

        registered_session_ = session;
    }

    void TrackHandler::UnregisterSession(const std::shared_ptr<Session>& session)
    {
        std::lock_guard lock(registration_mutex_);
        const auto current = registered_session_.lock();
        if (current && current != session) {
            throw std::logic_error("Handler is registered with another session");
        }

        registered_session_.reset();
    }

    bool TrackHandler::IsRegistered() const
    {
        std::lock_guard lock(registration_mutex_);
        return !registered_session_.expired();
    }

    void TrackHandler::RequestError(ErrorCode, std::string) {}
}
