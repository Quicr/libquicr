/*
 *  Copyright (C) 2024
 *  Cisco Systems, Inc.
 *  All Rights Reserved
 */

#include <moq/publish_track_handler.h>

namespace moq {

    PublishTrackHandler::PublishObjectStatus PublishTrackHandler::PublishObject(
      const moq::ObjectHeaders& object_headers,
      moq::BytesSpan data)
    {
        bool is_stream_header_needed{ false };
        switch (track_mode_) {
            case TrackMode::kDatagram:
                break;
            case TrackMode::kStreamPerGroup:
                is_stream_header_needed = prev_object_group_id_ != object_headers.group_id;
                break;
            case TrackMode::kStreamPerObject:
                is_stream_header_needed = true;
                break;
            case TrackMode::kStreamPerTrack:
                if (not sent_track_header_) {
                    is_stream_header_needed = true;
                    sent_track_header_ = true;
                }
                
                break;
        }

        prev_object_group_id_ = object_headers.group_id;

        if (publish_object_func_ != nullptr) {
            return publish_object_func_(priority, ttl, is_stream_header_needed, group_id, object_id, object);
        } else {
            return PublishObjectStatus::kInternalError;
        }
    }

} // namespace quicr
