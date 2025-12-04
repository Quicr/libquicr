#include "quicr/detail/receive_track_handler.h"

namespace quicr {
    void ReceiveTrackHandler::StreamDataRecv(bool is_start,
                                             uint64_t stream_id,
                                             std::shared_ptr<const std::vector<uint8_t>> data)
    {
        if (stream_id > current_stream_id_) {
            current_stream_id_ = stream_id;
        } else if (stream_id < current_stream_id_) {
            SPDLOG_DEBUG(
              "Old stream data received, stream_id: {} is less than {}, ignoring", stream_id, current_stream_id_);
            return;
        }

        if (is_start) {
            stream_buffer_.Clear();

            stream_buffer_.InitAny<messages::StreamHeaderSubGroup>();
            stream_buffer_.Push(*data);

            // Expect that on initial start of stream, there is enough data to process the stream headers

            auto& s_hdr = stream_buffer_.GetAny<messages::StreamHeaderSubGroup>();
            if (not(stream_buffer_ >> s_hdr)) {
                SPDLOG_ERROR("Not enough data to process new stream headers, stream is invalid");
                // TODO: Add metrics to track this
                return;
            }
        } else {
            stream_buffer_.Push(*data);
        }

        auto& s_hdr = stream_buffer_.GetAny<messages::StreamHeaderSubGroup>();

        if (not stream_buffer_.AnyHasValueB()) {
            stream_buffer_.InitAnyB<messages::StreamSubGroupObject>();
        }

        auto& obj = stream_buffer_.GetAnyB<messages::StreamSubGroupObject>();
        obj.stream_type = s_hdr.type;
        const auto subgroup_properties = messages::StreamHeaderProperties(s_hdr.type);
        if (stream_buffer_ >> obj) {
            SPDLOG_TRACE("Received stream_subgroup_object priority: {} track_alias: {} "
                         "group_id: {} subgroup_id: {} object_id: {} data size: {}",
                         s_hdr.priority,
                         s_hdr.track_alias,
                         s_hdr.group_id,
                         s_hdr.subgroup_id.has_value() ? *s_hdr.subgroup_id : -1,
                         obj.object_id,
                         obj.payload.size());

            if (next_object_id_.has_value()) {
                if (current_group_id_ != s_hdr.group_id || current_subgroup_id_ != s_hdr.subgroup_id) {
                    next_object_id_ = obj.object_delta;
                } else {
                    *next_object_id_ += obj.object_delta;
                }
            } else {
                next_object_id_ = obj.object_delta;
            }

            current_group_id_ = s_hdr.group_id;
            current_subgroup_id_ = s_hdr.subgroup_id.value();

            if (!s_hdr.subgroup_id.has_value()) {
                if (subgroup_properties.subgroup_id_type != messages::SubgroupIdType::kSetFromFirstObject) {
                    throw messages::ProtocolViolationException("Subgoup ID mismatch");
                }
                // Set the subgroup ID from the first object ID.
                s_hdr.subgroup_id = next_object_id_;
            }

            track_metrics_.objects_received++;
            track_metrics_.bytes_received += obj.payload.size();

            try {
                ObjectReceived(s_hdr.track_alias,
                               {
                                 s_hdr.group_id,
                                 next_object_id_.value(),
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

                *next_object_id_ += 1;
            } catch (const std::exception& e) {
                SPDLOG_ERROR("Caught exception trying to receive Subscribe object. (error={})", e.what());
            }

            stream_buffer_.ResetAnyB<messages::StreamSubGroupObject>();
        }
    }

    void ReceiveTrackHandler::DgramDataRecv(std::shared_ptr<const std::vector<uint8_t>> data)
    {
        stream_buffer_.Clear();

        stream_buffer_.Push(*data);

        messages::ObjectDatagram msg;
        if (stream_buffer_ >> msg) {
            SPDLOG_TRACE("Received object datagram conn_id: {0} data_ctx_id: {1} subscriber_id: {2} "
                         "track_alias: {3} group_id: {4} object_id: {5} data size: {6}",
                         conn_id,
                         (data_ctx_id ? *data_ctx_id : 0),
                         msg.subscribe_id,
                         msg.track_alias,
                         msg.group_id,
                         msg.object_id,
                         msg.payload.size());

            track_metrics_.objects_received++;
            track_metrics_.bytes_received += msg.payload.size();

            try {
                ObjectReceived(msg.track_alias,
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
}
