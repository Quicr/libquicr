// SPDX-FileCopyrightText: Copyright (c) 2024 Cisco Systems
// SPDX-License-Identifier: BSD-2-Clause

#include "quicr/subscribe_track_handler.h"

#include "quicr/detail/messages.h"
#include "quicr/detail/stream_buffer.h"
#include "quicr/detail/transport.h"

namespace quicr {

    void SubscribeTrackHandler::SupportNewGroupRequest(bool is_supported) noexcept
    {
        support_new_group_request_ = is_supported;
    }

    void SubscribeTrackHandler::ObjectReceived([[maybe_unused]] const ObjectHeaders& object_headers,
                                               [[maybe_unused]] BytesSpan data)
    {
    }

    void SubscribeTrackHandler::ObjectReceived(const messages::TrackAlias&,
                                               const ObjectHeaders& object_headers,
                                               BytesSpan data)
    {
        ObjectReceived(object_headers, data);
    }

    void SubscribeTrackHandler::Pause() noexcept
    {
        auto transport = GetTransport().lock();
        if (!transport || status_ == Status::kPaused || status_ == Status::kNotConnected) {
            return;
        }

        status_ = Status::kPaused;
        auto& conn_ctx = transport->GetConnectionContext(GetConnectionId());
        transport->SendSubscribeUpdate(conn_ctx,
                                       conn_ctx.GetNextRequestId(),
                                       GetRequestId().value(),
                                       GetFullTrackName(),
                                       {},
                                       0,
                                       GetPriority(),
                                       false,
                                       false);
    }

    void SubscribeTrackHandler::Resume() noexcept
    {
        auto transport = GetTransport().lock();
        if (!transport) {
            return;
        }

        if (status_ != Status::kPaused) {
            return;
        }

        status_ = Status::kOk;
        auto& conn_ctx = transport->GetConnectionContext(GetConnectionId());
        transport->SendSubscribeUpdate(conn_ctx,
                                       conn_ctx.GetNextRequestId(),
                                       GetRequestId().value(),
                                       GetFullTrackName(),
                                       {},
                                       0,
                                       GetPriority(),
                                       true,
                                       false);
    }

    void SubscribeTrackHandler::RequestNewGroup(uint64_t group_id) noexcept
    {
        auto transport = GetTransport().lock();
        if (!transport || status_ != Status::kOk || !support_new_group_request_) {
            return;
        }

        auto& conn_ctx = transport->GetConnectionContext(GetConnectionId());
        transport->SendSubscribeUpdate(conn_ctx,
                                       conn_ctx.GetNextRequestId(),
                                       GetRequestId().value(),
                                       GetFullTrackName(),
                                       {},
                                       group_id,
                                       GetPriority(),
                                       true,
                                       true);
    }
} // namespace quicr
