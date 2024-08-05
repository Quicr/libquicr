/*
 *  Copyright (C) 2024
 *  Cisco Systems, Inc.
 *  All Rights Reserved
 */

#include <quicr/moqt_base_track_handler.h>
#include <quicr/moqt_core.h>

namespace quicr {

    MOQTBaseTrackHandler::SendError MOQTBaseTrackHandler::sendObject([[maybe_unused]] const uint64_t  group_id,
                                                             [[maybe_unused]] const uint64_t object_id,
                                                             [[maybe_unused]] std::span<const uint8_t> object)
    {
        return sendObject(group_id, object_id, object, _def_priority, _def_ttl);
    }

    MOQTBaseTrackHandler::SendError MOQTBaseTrackHandler::sendObject([[maybe_unused]] const uint64_t  group_id,
                                                             [[maybe_unused]] const uint64_t object_id,
                                                             [[maybe_unused]] std::span<const uint8_t> object,
                                                             [[maybe_unused]] uint32_t ttl)
    {
        return sendObject(group_id, object_id, object, _def_priority, ttl);
    }

    MOQTBaseTrackHandler::SendError MOQTBaseTrackHandler::sendObject([[maybe_unused]] const uint64_t  group_id,
                                                             [[maybe_unused]] const uint64_t object_id,
                                                             [[maybe_unused]] std::span<const uint8_t> object,
                                                             [[maybe_unused]] uint8_t priority)
    {

        return sendObject(group_id, object_id, object, priority, _def_ttl);
    }

    MOQTBaseTrackHandler::SendError MOQTBaseTrackHandler::sendObject([[maybe_unused]] const uint64_t  group_id,
                                                             [[maybe_unused]] const uint64_t object_id,
                                                             [[maybe_unused]] std::span<const uint8_t> object,
                                                             [[maybe_unused]] uint8_t priority,
                                                             [[maybe_unused]] uint32_t ttl)
    {
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
            return _mi_sendObjFunc(priority, ttl, is_stream_header_needed, group_id, object_id, object);
        } else {
            return SendError::INTERNAL_ERROR;
        }
    }


} // namespace quicr
