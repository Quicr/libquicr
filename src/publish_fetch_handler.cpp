// SPDX-FileCopyrightText: Copyright (c) 2025 Cisco Systems
// SPDX-License-Identifier: BSD-2-Clause

#include "quicr/publish_fetch_handler.h"

#include "quicr/detail/transport.h"

namespace quicr {
    PublishTrackHandler::PublishObjectStatus PublishFetchHandler::PublishObject(const ObjectHeaders& object_headers,
                                                                                const BytesSpan data)
    {
        auto transport = GetTransport().lock();

        if (!transport) {
            return PublishObjectStatus::kInternalError;
        }

        bool is_stream_header_needed{ !sent_first_header_ };
        sent_first_header_ = true;

        if (!GetRequestId().has_value()) {
            return PublishTrackHandler::PublishObjectStatus::kNoSubscribers;
        }

        ITransport::EnqueueFlags eflags;

        std::uint64_t group_id = object_headers.group_id;
        std::uint64_t object_id = object_headers.object_id;
        std::uint16_t ttl = object_headers.ttl.has_value() ? object_headers.ttl.value() : default_ttl_;
        std::uint8_t priority =
          object_headers.priority.has_value() ? object_headers.priority.value() : default_priority_;

        object_msg_buffer_.clear();

        eflags.use_reliable = true;

        if (is_stream_header_needed) {
            eflags.new_stream = true;
            eflags.clear_tx_queue = true;
            eflags.use_reset = true;

            messages::StreamHeaderSubGroup subgroup_hdr;
            subgroup_hdr.type = GetStreamMode();
            subgroup_hdr.group_id = object_headers.group_id;
            auto properties = messages::StreamHeaderProperties(subgroup_hdr.type);
            if (properties.subgroup_id_type == messages::SubgroupIdType::kExplicit) {
                subgroup_hdr.subgroup_id = object_headers.subgroup_id;
            }
            subgroup_hdr.priority = priority;
            subgroup_hdr.track_alias = GetTrackAlias().value();
            object_msg_buffer_ << subgroup_hdr;

            auto result = transport->Enqueue(
              GetConnectionId(),
              publish_data_ctx_id_,
              group_id,
              std::make_shared<std::vector<uint8_t>>(object_msg_buffer_.begin(), object_msg_buffer_.end()),
              priority,
              ttl,
              0,
              eflags);

            object_msg_buffer_.clear();
            eflags.new_stream = false;
            eflags.clear_tx_queue = false;
            eflags.use_reset = false;

            if (result != TransportError::kNone) {
                throw TransportException(result);
            }
        }

        messages::StreamSubGroupObject object;
        object.object_id = object_id;
        object.stream_type = GetStreamMode();
        object.extensions = object_headers.extensions;
        object.payload.assign(data.begin(), data.end());
        object_msg_buffer_ << object;

        auto result = transport->Enqueue(
          GetConnectionId(),
          publish_data_ctx_id_,
          group_id,
          std::make_shared<std::vector<uint8_t>>(object_msg_buffer_.begin(), object_msg_buffer_.end()),
          priority,
          ttl,
          0,
          eflags);

        if (result != TransportError::kNone) {
            throw TransportException(result);
        }

        return PublishTrackHandler::PublishObjectStatus::kOk;
    }
}
