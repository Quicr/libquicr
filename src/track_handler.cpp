// SPDX-FileCopyrightText: Copyright (c) 2026 Cisco Systems
// SPDX-License-Identifier: BSD-2-Clause

#include "quicr/handlers/track_handler.h"
#include "quicr/session.h"
#include "request.h"
#include "stream.h"

#include <stdexcept>

namespace quicr {
    void TrackHandler::BindRequest(std::shared_ptr<Request> request)
    {
        if (!request) {
            throw std::invalid_argument("Request cannot be null.");
        }

        std::lock_guard lock(request_mutex_);
        if (!request_.expired()) {
            throw std::logic_error("Handler is already bound to a request.");
        }
        request_ = std::move(request);
    }

    bool TrackHandler::UnbindRequest()
    {
        std::shared_ptr<Request> request;
        {
            std::lock_guard lock(request_mutex_);
            request = request_.lock();
            request_.reset();
            if (!request) {
                return false;
            }
        }

        if (std::erase(request->handlers, shared_from_this()) == 0) {
            throw std::logic_error("Handler was already removed from the request");
        }

        return true;
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

    std::shared_ptr<Request> TrackHandler::GetRequest() const
    {
        std::lock_guard lock(request_mutex_);
        return request_.lock();
    }

    bool TrackHandler::IsRegistered() const
    {
        std::lock_guard lock(registration_mutex_);
        return !registered_session_.expired();
    }

    void TrackHandler::RequestError(ErrorCode, std::string) {}
}
