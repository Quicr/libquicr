// SPDX-FileCopyrightText: Copyright (c) 2025 Cisco Systems
// SPDX-License-Identifier: BSD-2-Clause

#include "quicr/handlers/publish_fetch_handler.h"

#include "quicr/session.h"

namespace quicr {
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

        bool is_stream_header_needed{ !sent_first_header_ };
        if (is_stream_header_needed) {
            serialization_state_ = messages::FetchObjectSerializationState(group_order_);
        }

        const auto request_id = GetRequestId();
        if (!request_id.has_value()) {
            return PublishTrackHandler::PublishObjectStatus::kNoSubscribers;
        }

        Transport::EnqueueFlags eflags;

        std::uint16_t ttl = object_headers.ttl.has_value() ? object_headers.ttl.value() : default_ttl_;
        object_msg_buffer_.clear();

        if (is_stream_header_needed) {
            eflags.close_stream = false;
            eflags.clear_tx_queue = true;
            eflags.use_reset = false;

            stream_ = session->CreateStream(*GetRequestId(), GetPublishObjectPriority());

            messages::FetchHeader fetch_hdr;
            fetch_hdr.request_id = *request_id;
            object_msg_buffer_ << fetch_hdr;

            auto result = session->EnqueueStreamData(
              stream_,
              std::make_shared<std::vector<uint8_t>>(object_msg_buffer_.begin(), object_msg_buffer_.end()),
              ttl,
              eflags);

            object_msg_buffer_.clear();
            eflags.close_stream = false;
            eflags.clear_tx_queue = false;
            eflags.use_reset = false;

            if (!result) {
                closed_ = true;
                return PublishObjectStatus::kFetchClosed;
            }
            sent_first_header_ = true;
        }

        auto next_serialization_state = serialization_state_;
        auto object = next_serialization_state.Encode(object_headers, GetPublisherPriority(), data);
        object_msg_buffer_ << object;

        auto result = session->EnqueueStreamData(
          stream_,
          std::make_shared<std::vector<uint8_t>>(object_msg_buffer_.begin(), object_msg_buffer_.end()),
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
        if (stream_ == nullptr) {
            return;
        }

        auto session = GetSession().lock();
        if (!session) {
            return;
        }

        Transport::EnqueueFlags eflags;
        eflags.close_stream = true;
        eflags.use_reset = false;

        (void)session->EnqueueStreamData(stream_, {}, default_ttl_, eflags);

        stream_.reset();
    }
}
