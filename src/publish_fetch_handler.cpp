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
        auto transport = GetSession().lock();

        if (!transport) {
            return PublishObjectStatus::kInternalError;
        }

        bool is_stream_header_needed{ !sent_first_header_ };
        sent_first_header_ = true;
        if (is_stream_header_needed) {
            serialization_state_ = messages::FetchObjectSerializationState(group_order_);
        }

        const auto request_id = GetRequestId();
        if (!request_id.has_value()) {
            return PublishTrackHandler::PublishObjectStatus::kNoSubscribers;
        }

        Transport::EnqueueFlags eflags;

        std::uint16_t ttl = object_headers.ttl.has_value() ? object_headers.ttl.value() : default_ttl_;
        std::uint8_t priority =
          object_headers.priority.has_value() ? object_headers.priority.value() : default_priority_;

        object_msg_buffer_.clear();

        eflags.use_reliable = true;

        if (is_stream_header_needed) {
            eflags.close_stream = false;
            eflags.clear_tx_queue = true;
            eflags.use_reset = false;

            stream_id_ = transport->CreateStream(GetConnectionId(), publish_data_ctx_id_, priority);

            messages::FetchHeader fetch_hdr;
            fetch_hdr.request_id = *request_id;
            object_msg_buffer_ << fetch_hdr;

            auto result = transport->Enqueue(
              GetConnectionId(),
              publish_data_ctx_id_,
              stream_id_,
              std::make_shared<std::vector<uint8_t>>(object_msg_buffer_.begin(), object_msg_buffer_.end()),
              priority,
              ttl,
              0,
              eflags);

            object_msg_buffer_.clear();
            eflags.close_stream = false;
            eflags.clear_tx_queue = false;
            eflags.use_reset = false;

            if (result != TransportError::kNone) {
                throw TransportException(result);
            }
        }

        auto next_serialization_state = serialization_state_;
        auto object = next_serialization_state.Encode(object_headers, priority, data);
        object_msg_buffer_ << object;

        auto result = transport->Enqueue(
          GetConnectionId(),
          publish_data_ctx_id_,
          stream_id_,
          std::make_shared<std::vector<uint8_t>>(object_msg_buffer_.begin(), object_msg_buffer_.end()),
          priority,
          ttl,
          0,
          eflags);

        if (result != TransportError::kNone) {
            throw TransportException(result);
        }
        serialization_state_ = std::move(next_serialization_state);

        return PublishTrackHandler::PublishObjectStatus::kOk;
    }
}
