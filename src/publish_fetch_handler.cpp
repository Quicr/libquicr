// SPDX-FileCopyrightText: Copyright (c) 2025 Cisco Systems
// SPDX-License-Identifier: BSD-2-Clause

#include "quicr/handlers/publish_fetch_handler.h"

#include "quicr/session.h"

namespace quicr {
    bool PublishFetchHandler::StartFetch()
    {
        auto session = GetSession().lock();
        const auto request_id = GetRequestId();
        if (!session || !request_id || closed_) {
            return false;
        }

        stream_ = session->CreateStream(*request_id, GetPublishObjectPriority());
        messages::FetchHeader header;
        header.request_id = *request_id;
        Bytes buffer;
        buffer << header;

        Transport::EnqueueFlags flags;
        flags.use_reliable = true;
        flags.clear_tx_queue = true;
        if (!session->Enqueue(
              stream_, std::make_shared<Bytes>(std::move(buffer)), GetPublishObjectPriority(), default_ttl_, flags)) {
            closed_ = true;
            return false;
        }
        return true;
    }

    PublishTrackHandler::PublishObjectStatus PublishFetchHandler::PublishObject(
      const ObjectHeaders& object_headers,
      const BytesSpan data,
      [[maybe_unused]] std::optional<messages::StreamHeaderProperties> stream_mode)
    {
        if (closed_) {
            return PublishObjectStatus::kFetchClosed;
        }

        auto session = GetSession().lock();

        if (!session) {
            return PublishObjectStatus::kInternalError;
        }

        const auto request_id = GetRequestId();
        if (!request_id.has_value() || !stream_) {
            return PublishTrackHandler::PublishObjectStatus::kNoSubscribers;
        }

        Transport::EnqueueFlags eflags;

        std::uint16_t ttl = object_headers.ttl.has_value() ? object_headers.ttl.value() : default_ttl_;
        object_msg_buffer_.clear();

        eflags.use_reliable = true;

        auto next_serialization_state = serialization_state_;
        auto object = next_serialization_state.Encode(object_headers, GetPublisherPriority(), data);
        object_msg_buffer_ << object;

        auto result =
          session->Enqueue(stream_,
                           std::make_shared<std::vector<uint8_t>>(object_msg_buffer_.begin(), object_msg_buffer_.end()),
                           GetPublishObjectPriority(),
                           ttl,
                           eflags);

        if (!result) {
            closed_ = true;
            return PublishObjectStatus::kFetchClosed;
        }
        serialization_state_ = std::move(next_serialization_state);

        return PublishTrackHandler::PublishObjectStatus::kOk;
    }

    void PublishFetchHandler::EndFetch()
    {
        if (closed_) {
            return;
        }
        closed_ = true;

        auto session = GetSession().lock();
        if (!session || !stream_) {
            return;
        }

        Transport::EnqueueFlags eflags;
        eflags.use_reliable = true;
        eflags.close_stream = true;
        eflags.use_reset = false;

        session->Enqueue(stream_, {}, GetPublishObjectPriority(), default_ttl_, eflags);

        stream_.reset();
    }
}
