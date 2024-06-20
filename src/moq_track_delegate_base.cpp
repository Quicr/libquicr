/*
 *  Copyright (C) 2024
 *  Cisco Systems, Inc.
 *  All Rights Reserved
 */

#include <quicr/moq_instance.h>
#include <quicr/moq_track_delegate.h>

namespace quicr {


    MoQTrackDelegate::SendError MoQTrackDelegate::sendObject([[maybe_unused]] const uint64_t  group_id,
                                                             [[maybe_unused]] const uint64_t object_id,
                                                             [[maybe_unused]] const std::span<const uint8_t>& object)
    {
        return sendObject(group_id, object_id, object, _def_priority, _def_ttl);
    }

    MoQTrackDelegate::SendError MoQTrackDelegate::sendObject([[maybe_unused]] const uint64_t  group_id,
                                                             [[maybe_unused]] const uint64_t object_id,
                                                             [[maybe_unused]] const std::span<const uint8_t>& object,
                                                             [[maybe_unused]] uint32_t ttl)
    {
        return sendObject(group_id, object_id, object, _def_priority, ttl);
    }

    MoQTrackDelegate::SendError MoQTrackDelegate::sendObject([[maybe_unused]] const uint64_t  group_id,
                                                             [[maybe_unused]] const uint64_t object_id,
                                                             [[maybe_unused]] const std::span<const uint8_t>& object,
                                                             [[maybe_unused]] uint8_t priority)
    {

        return sendObject(group_id, object_id, object, priority, _def_ttl);
    }

    MoQTrackDelegate::SendError MoQTrackDelegate::sendObject([[maybe_unused]] const uint64_t  group_id,
                                                             [[maybe_unused]] const uint64_t object_id,
                                                             [[maybe_unused]] const std::span<const uint8_t>& object,
                                                             [[maybe_unused]] uint8_t priority,
                                                             [[maybe_unused]] uint32_t ttl)
    {
        std::vector<uint8_t> data_copy(object.begin(), object.end());

        bool is_stream_header_needed{ false };
        switch (_mi_track_mode) {
            case TrackMode::DATAGRAM:
                break;
            case TrackMode::STREAM_PER_GROUP:
                is_stream_header_needed = _prev_group_id != group_id;
                break;
            case TrackMode::STREAM_PER_OBJECT:
                is_stream_header_needed = true;
                break;
            case TrackMode::STREAM_PER_TRACK:
                if (not _sent_track_header) {
                    is_stream_header_needed = true;
                    _sent_track_header = true;
                }
                break;
        }

        _prev_group_id = group_id;

        if (_mi_sendObjFunc != nullptr) {
            return _mi_sendObjFunc(priority, ttl, is_stream_header_needed, group_id, object_id, std::move(data_copy));
        } else {
            return SendError::INTERNAL_ERROR;
        }
    }


} // namespace quicr
