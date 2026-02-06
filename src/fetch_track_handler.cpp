// SPDX-FileCopyrightText: Copyright (c) 2025 Cisco Systems
// SPDX-License-Identifier: BSD-2-Clause

#include "quicr/fetch_track_handler.h"

namespace quicr {
    void FetchTrackHandler::StreamDataRecv(bool is_start,
                                           std::uint64_t stream_id,
                                           std::shared_ptr<const std::vector<uint8_t>> data)
    {
        SPDLOG_DEBUG("Got fetch data size: {}", data->size());

        auto& stream = streams_[stream_id];

        if (is_start) {
            stream.buffer.Clear();

            stream.buffer.InitAny<messages::FetchHeader>();
            stream.buffer.Push(*data);

            // Expect that on initial start of stream, there is enough data to process the stream headers

            auto& f_hdr = stream.buffer.GetAny<messages::FetchHeader>();
            if (not(stream.buffer >> f_hdr)) {
                SPDLOG_ERROR("Not enough data to process new stream headers, stream is invalid len: {} / {}",
                             stream.buffer.Size(),
                             data->size());
                // TODO: Add metrics to track this
                return;
            }
        } else {
            stream.buffer.Push(*data);
        }

        if (not stream.buffer.AnyHasValueB()) {
            stream.buffer.InitAnyB<messages::FetchObject>();
        }

        auto& obj = stream.buffer.GetAnyB<messages::FetchObject>();

        if (stream.buffer >> obj) {
            // TODO: What to do with end of range?
            subscribe_track_metrics_.objects_received++;
            subscribe_track_metrics_.bytes_received += obj.payload.size();

            // Build.
            ObjectHeaders headers;
            try {
                headers = From(obj, state_);
                state_.Update(headers);
            } catch (const std::invalid_argument& e) {
                throw messages::ProtocolViolationException(e.what());
            }

            // Deliver.
            SPDLOG_TRACE("Received fetch_object subscribe_id: {} priority: {} "
                         "group_id: {} subgroup_id: {} object_id: {} data size: {}",
                         *GetSubscribeId(),
                         headers.priority.has_value() ? static_cast<int>(*headers.priority) : -1,
                         headers.group_id,
                         headers.subgroup_id,
                         headers.object_id,
                         headers.payload_length);
            try {
                ObjectReceived(headers, obj.payload);
            } catch (const std::exception& e) {
                SPDLOG_ERROR("Caught exception trying to receive Fetch object. (error={})", e.what());
            }

            stream.buffer.ResetAnyB<messages::FetchObject>();
        }
    }

    /// Parse a FetchObject into a resolved ObjectHeader.
    ObjectHeaders FetchTrackHandler::From(const messages::FetchObject& message,
                                          const messages::FetchObjectSerializationState& state)
    {
        ObjectHeaders headers{};
        if (message.group_id.has_value()) {
            headers.group_id = *message.group_id;
        } else {
            if (!state.prior_group_id.has_value()) {
                throw std::invalid_argument("No prior group ID found");
            }
            headers.group_id = *state.prior_group_id;
        }
        if (message.object_id.has_value()) {
            headers.object_id = *message.object_id;
        } else {
            if (!state.prior_object_id.has_value()) {
                throw std::invalid_argument("No prior object ID found");
            }
            headers.object_id = *state.prior_object_id + 1;
        }
        const auto& properties = *message.properties;
        if (!properties.datagram) {
            if (!properties.subgroup_id_mode.has_value()) {
                throw std::invalid_argument("Missing subgroup_id_mode");
            }
            switch (*properties.subgroup_id_mode) {
                case messages::FetchSerializationProperties::FetchSubgroupIdType::kSubgroupPrior:
                    if (!state.prior_subgroup_id.has_value()) {
                        throw std::invalid_argument("No prior subgroup ID found");
                    }
                    headers.subgroup_id = *state.prior_subgroup_id;
                    break;
                case messages::FetchSerializationProperties::FetchSubgroupIdType::kSubgroupNext:
                    if (!state.prior_subgroup_id.has_value()) {
                        throw std::invalid_argument("No prior subgroup ID found");
                    }
                    headers.subgroup_id = *state.prior_subgroup_id + 1;
                    break;
                case messages::FetchSerializationProperties::FetchSubgroupIdType::kSubgroupZero:
                    assert(*message.subgroup_id == 0);
                    headers.subgroup_id = 0;
                    break;
                case messages::FetchSerializationProperties::FetchSubgroupIdType::kSubgroupExplicit:
                    // TODO: Think about what to do if subgroup_id is not set.
                    // At this point in the code is this a bug or a protocol violation?
                    headers.subgroup_id = *message.subgroup_id;
                    break;
                default:
                    throw std::invalid_argument("Unknown subgroup_id_mode");
            }
        } else {
            assert(!message.subgroup_id.has_value());
        }
        if (!message.publisher_priority.has_value()) {
            if (!state.prior_priority.has_value()) {
                throw std::invalid_argument("No prior priority");
            }
            headers.priority = *state.prior_priority;
        } else {
            headers.priority = *message.publisher_priority;
        }

        headers.payload_length = message.payload.size();
        headers.status = message.payload.empty() ? message.object_status : ObjectStatus::kAvailable;
        headers.ttl = std::nullopt; // TODO: TTL?
        headers.track_mode = properties.datagram ? TrackMode::kDatagram : TrackMode::kStream;
        headers.extensions = message.extensions;
        headers.immutable_extensions = message.immutable_extensions;
        return headers;
    }
}
