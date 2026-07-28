// SPDX-FileCopyrightText: Copyright (c) 2024 Cisco Systems
// SPDX-License-Identifier: BSD-2-Clause

#include "quicr/handlers/subscribe_track_handler.h"

#include "quicr/containers/stream_buffer.h"
#include "quicr/messages/messages.h"
#include "quicr/messages/parameters.h"
#include "quicr/session.h"

#include <spdlog/spdlog.h>

namespace quicr {

    void SubscribeTrackHandler::SupportNewGroupRequest(bool is_supported) noexcept
    {
        support_new_group_request_ = is_supported;
    }

    void SubscribeTrackHandler::ObjectReceived(
      [[maybe_unused]] const ObjectHeaders& object_headers,
      [[maybe_unused]] BytesSpan data,
      [[maybe_unused]] std::optional<messages::StreamHeaderProperties> stream_mode)
    {
    }

    void SubscribeTrackHandler::StreamDataRecv(uint64_t stream_id, StreamBuffer<uint8_t>&& initial_buffer)
    {
        auto [it, inserted] = streams_.try_emplace(stream_id, std::move(initial_buffer));
        if (!inserted) {
            SPDLOG_ERROR("StreamDataRecv got new stream for existing Stream ID {}", stream_id);
            return;
        }
        TryParseStreamBufferData(it->second);
    }

    void SubscribeTrackHandler::StreamDataRecv(uint64_t stream_id, std::shared_ptr<const std::vector<uint8_t>> data)
    {
        const auto it = streams_.find(stream_id);
        if (it == streams_.end()) {
            SPDLOG_ERROR("StreamDataRecv had no stream for expected Stream ID {}", stream_id);
            return;
        }
        it->second.buffer.Push(*data);
        TryParseStreamBufferData(it->second);
    }

    void SubscribeTrackHandler::TryParseStreamBufferData(StreamContext& stream)
    {
        auto& s_hdr = stream.buffer.GetAny<messages::StreamHeaderSubGroup>();
        if (not(stream.buffer >> s_hdr)) {
            return;
        }

        // TODO: This shouldn't override subscriber priority, but keeping existing behaviour.
        if (s_hdr.priority.has_value()) {
            SetPriority(*s_hdr.priority);
        }

        while (not stream.buffer.Empty()) {
            if (not stream.buffer.AnyHasValueB()) {
                stream.buffer.InitAnyB<messages::StreamSubGroupObject>();
            }

            auto& obj = stream.buffer.GetAnyB<messages::StreamSubGroupObject>();
            obj.properties.emplace(*s_hdr.properties);
            if (not(stream.buffer >> obj)) {
                return;
            }

            std::optional<messages::StreamHeaderProperties> stream_properties;
            if (!stream.next_object_id.has_value()) {
                stream_properties.emplace(*s_hdr.properties);
            }

            SPDLOG_TRACE("Received stream_subgroup_object priority: {} track_alias: {} "
                         "group: {} subgroup: {} object: {} data size: {} type: {}",
                         s_hdr.priority,
                         s_hdr.track_alias,
                         s_hdr.group_id,
                         s_hdr.subgroup_id.has_value() ? *s_hdr.subgroup_id : -1,
                         obj.object_delta,
                         obj.payload.size(),
                         static_cast<int>(obj.object_status));

            if (stream.next_object_id.has_value()) {
                if (stream.current_group_id != s_hdr.group_id || stream.current_subgroup_id != s_hdr.subgroup_id) {
                    stream.next_object_id = obj.object_delta;
                } else {
                    *stream.next_object_id += obj.object_delta;
                }
            } else {
                stream.next_object_id = obj.object_delta;
            }

            if (!s_hdr.subgroup_id.has_value()) {
                if (obj.properties->subgroup_id_mode != messages::SubgroupIdType::kSetFromFirstObject) {
                    throw messages::ProtocolViolationException("Subgoup ID mismatch");
                }
                // Set the subgroup ID from the first object ID.
                s_hdr.subgroup_id = stream.next_object_id;
            }

            stream.current_group_id = s_hdr.group_id;
            stream.current_subgroup_id = s_hdr.subgroup_id.value();

            subscribe_track_metrics_.objects_received++;

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
                  obj.payload,
                  std::move(stream_properties));

                *stream.next_object_id += 1;
            } catch (const std::exception& e) {
                SPDLOG_ERROR("Caught exception trying to receive Subscribe object. (error={})", e.what());
            }

            stream.buffer.ResetAnyB();
        }
    }

    void SubscribeTrackHandler::DgramDataRecv(std::shared_ptr<const std::vector<uint8_t>> data)
    {
        dgram_buffer_.Clear();
        dgram_buffer_.Push(*data);

        // Payload or status?
        const auto properties = messages::DatagramHeaderProperties(data->front());
        if (properties.status) {
            messages::ObjectDatagramStatus status_msg;
            if (dgram_buffer_ >> status_msg) {
                SPDLOG_TRACE("Received object datagram status track_alias: {} group_id: {} object_id: {} status: {}",
                             status_msg.track_alias,
                             status_msg.group_id,
                             status_msg.object_id,
                             static_cast<uint8_t>(status_msg.status));

                subscribe_track_metrics_.objects_received++;

                try {
                    ObjectStatusReceived(
                      status_msg.group_id,
                      status_msg.object_id,
                      status_msg.priority.value_or(priority_), // TODO: This should be publisher priority.
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
        if (!transport || status_ == Status::kPaused || status_ == Status::kNotConnected ||
            !GetDataContextId().has_value()) {
            return;
        }

        status_ = Status::kPaused;
        auto& conn_ctx = transport->GetConnectionContext(GetConnectionId());
        transport->SendRequestUpdate(conn_ctx,
                                     GetDataContextId().value(),
                                     conn_ctx.GetNextRequestId(),
                                     TrackHash(GetFullTrackName()),
                                     std::nullopt,
                                     GetPriority(),
                                     false);
    }

    void SubscribeTrackHandler::Resume() noexcept
    {
        auto transport = GetTransport().lock();
        if (!transport || !GetDataContextId().has_value()) {
            return;
        }

        if (status_ != Status::kPaused) {
            return;
        }

        status_ = Status::kOk;
        auto& conn_ctx = transport->GetConnectionContext(GetConnectionId());
        transport->SendRequestUpdate(conn_ctx,
                                     GetDataContextId().value(),
                                     conn_ctx.GetNextRequestId(),
                                     TrackHash(GetFullTrackName()),
                                     std::nullopt,
                                     GetPriority(),
                                     true);
    }

    void SubscribeTrackHandler::RequestNewGroup(uint64_t group_id) noexcept
    {
        auto transport = GetTransport().lock();
        if (!transport || status_ != Status::kOk || !support_new_group_request_ || !GetDataContextId().has_value()) {
            return;
        }

        auto& conn_ctx = transport->GetConnectionContext(GetConnectionId());
        transport->SendRequestUpdate(conn_ctx,
                                     GetDataContextId().value(),
                                     conn_ctx.GetNextRequestId(),
                                     TrackHash(GetFullTrackName()),
                                     group_id,
                                     GetPriority(),
                                     true);
    }

    void SubscribeTrackHandler::StreamClosed(std::uint64_t stream_id, bool)
    {
        streams_.erase(stream_id);
    }

    void SubscribeTrackHandler::RequestOkReceived(const messages::Parameters& params)
    {
        // SUBSCRIBE request OK itself is SUBSCRIBE_OK, so any RequestOk will be REQUEST_UPDATE_OK.
        messages::ValidateParameters(params,
                                     {
                                       messages::ParameterType::kExpires,
                                       messages::ParameterType::kLargestObject,
                                     });
        // TODO: EXPIRES
        // TODO: LARGEST_OBJECT
    }

    void SubscribeTrackHandler::RequestUpdateReceived(const messages::Parameters& params)
    {
        if (IsPublisherInitiated()) {
            // Publish can rev keys but nothing else.
            messages::ValidateParameters(params,
                                         {
                                           messages::ParameterType::kAuthorizationToken,
                                         });
            // TODO: AUTHORIZATION_TOKEN
            if (const auto transport = GetTransport().lock()) {
                transport->ResolveRequestUpdate(
                  GetConnectionId(), *GetRequestId(), { .error = std::nullopt, .params = {} });
            }
            return;
        }

        throw messages::ProtocolViolationException("Unexpected REQUEST_UPDATE");
    }

} // namespace quicr
