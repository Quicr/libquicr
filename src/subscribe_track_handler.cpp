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

    void SubscribeTrackHandler::StreamDataRecv(bool is_start,
                                               uint64_t stream_id,
                                               std::shared_ptr<const std::vector<uint8_t>> data)
    {
<<<<<<< Updated upstream
        SPDLOG_TRACE(
          "SubHandler:StreamDataRecv, is_start {}, stream_id: {}, data_sz {}", is_start, stream_id, data->size());

        auto& stream = streams_.at(stream_id);
=======
        auto& stream = streams_[stream_id];
>>>>>>> Stashed changes

        if (is_start) {
            stream.buffer.Clear();

            stream.buffer.InitAny<messages::StreamHeaderSubGroup>();
            stream.buffer.Push(*data);

            // Expect that on initial start of stream, there is enough data to process the stream headers

            auto& s_hdr = stream.buffer.GetAny<messages::StreamHeaderSubGroup>();
            if (not(stream.buffer >> s_hdr)) {
                SPDLOG_ERROR("Not enough data to process new stream headers, stream is invalid");
                // TODO: Add metrics to track this
                return;
            }
        } else {
            stream.buffer.Push(*data);
        }

        auto& s_hdr = stream.buffer.GetAny<messages::StreamHeaderSubGroup>();

        if (not stream.buffer.AnyHasValueB()) {
            stream.buffer.InitAnyB<messages::StreamSubGroupObject>();
        }

        auto& obj = stream.buffer.GetAnyB<messages::StreamSubGroupObject>();
        obj.stream_type = s_hdr.type;
        const auto subgroup_properties = messages::StreamHeaderProperties(s_hdr.type);
        if (stream.buffer >> obj) {
            SPDLOG_INFO("Received stream_subgroup_object priority: {} stream_id: {} track_alias: {} "
                        "group: {} subgroup: {} object: {} data size: {}",
                        s_hdr.priority,
                        stream_id,
                        s_hdr.track_alias,
                        s_hdr.group_id,
                        s_hdr.subgroup_id.has_value() ? *s_hdr.subgroup_id : -1,
                        obj.object_delta,
                        obj.payload.size());

            if (stream.next_object_id.has_value()) {
                if (stream.current_group_id != s_hdr.group_id || stream.current_subgroup_id != s_hdr.subgroup_id) {
                    stream.next_object_id = obj.object_delta;
                } else {
                    *stream.next_object_id += obj.object_delta;
                }
            } else {
                stream.next_object_id = obj.object_delta;
            }

            stream.current_group_id = s_hdr.group_id;
            stream.current_subgroup_id = s_hdr.subgroup_id.value();

            if (!s_hdr.subgroup_id.has_value()) {
                if (subgroup_properties.subgroup_id_type != messages::SubgroupIdType::kSetFromFirstObject) {
                    throw messages::ProtocolViolationException("Subgoup ID mismatch");
                }
                // Set the subgroup ID from the first object ID.
                s_hdr.subgroup_id = stream.next_object_id;
            }

            subscribe_track_metrics_.objects_received++;
            subscribe_track_metrics_.bytes_received += obj.payload.size();

            try {
                ObjectReceived(
                  {
                    s_hdr.group_id,
                    stream.next_object_id.value(),
                    s_hdr.subgroup_id.value(),
                    obj.payload.size(),
                    obj.object_status,
                    s_hdr.priority,
                    std::nullopt,
                    TrackMode::kStream,
                    obj.extensions,
                    obj.immutable_extensions,
                  },
                  obj.payload);

                *stream.next_object_id += 1;
            } catch (const std::exception& e) {
                SPDLOG_ERROR("Caught exception trying to receive Subscribe object. (error={})", e.what());
            }

            stream.buffer.ResetAnyB<messages::StreamSubGroupObject>();
        } else {
            SPDLOG_ERROR("SubHandler:StreamDataRecv, not enough data to process stream object, stream {}", stream_id);
        }
    }

    void SubscribeTrackHandler::DgramDataRecv(std::shared_ptr<const std::vector<uint8_t>> data)
    {
        dgram_buffer_.Clear();
        dgram_buffer_.Push(*data);

        // Payload or status?
        const auto msg_type = static_cast<messages::DatagramMessageType>(data->front());
        if (messages::TypeIsDatagramStatusType(msg_type)) {
            messages::ObjectDatagramStatus status_msg;
            if (dgram_buffer_ >> status_msg) {
                SPDLOG_TRACE("Received object datagram status track_alias: {} group_id: {} object_id: {} status: {}",
                             status_msg.track_alias,
                             status_msg.group_id,
                             status_msg.object_id,
                             static_cast<uint8_t>(status_msg.status));

                subscribe_track_metrics_.objects_received++;

                try {
                    ObjectStatusReceived(status_msg.group_id,
                                         status_msg.object_id,
                                         status_msg.status,
                                         status_msg.extensions,
                                         status_msg.immutable_extensions);
                } catch (const std::exception& e) {
                    SPDLOG_ERROR("Caught exception in ObjectStatusReceived. (error={})", e.what());
                }
            }
            return;
        }

        // Data.
        messages::ObjectDatagram msg;
        if (dgram_buffer_ >> msg) {
            SPDLOG_TRACE("Received object datagram conn_id: {0} data_ctx_id: {1} subscriber_id: {2} "
                         "track_alias: {3} group_id: {4} object_id: {5} data size: {6}",
                         conn_id,
                         (data_ctx_id ? *data_ctx_id : 0),
                         msg.subscribe_id,
                         msg.track_alias,
                         msg.group_id,
                         msg.object_id,
                         msg.payload.size());

            subscribe_track_metrics_.objects_received++;
            subscribe_track_metrics_.bytes_received += msg.payload.size();

            try {
                ObjectReceived(
                  {
                    msg.group_id,
                    msg.object_id,
                    0, // datagrams don't have subgroups
                    msg.payload.size(),
                    ObjectStatus::kAvailable,
                    msg.priority,
                    std::nullopt,
                    TrackMode::kDatagram,
                    msg.extensions,
                    msg.immutable_extensions,
                  },
                  std::move(msg.payload));
            } catch (const std::exception& e) {
                SPDLOG_ERROR("Caught exception trying to receive Subscribe object. (error={})", e.what());
            }
        }
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

    void SubscribeTrackHandler::StreamClosed(std::uint64_t stream_id)
    {
        streams_.erase(stream_id);
    }
} // namespace quicr
