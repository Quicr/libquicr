// SPDX-FileCopyrightText: Copyright (c) 2024 Cisco Systems
// SPDX-License-Identifier: BSD-2-Clause

#include <quicr/publish_track_handler.h>

namespace quicr {
    void PublishTrackHandler::StatusChanged(Status) {}
    void PublishTrackHandler::MetricsSampled(const PublishTrackMetrics&) {}

    PublishTrackHandler::PublishObjectStatus PublishTrackHandler::PublishObject(const ObjectHeaders& object_headers,
                                                                                BytesSpan data)
    {
        switch (publish_status_) {
            case Status::kOk:
                break;
            case Status::kNoSubscribers:
                publish_track_metrics_.objects_dropped_not_ok++;
                return PublishObjectStatus::kNoSubscribers;
            case Status::kPendingAnnounceResponse:
                [[fallthrough]];
            case Status::kNotAnnounced:
                [[fallthrough]];
            case Status::kNotConnected:
                publish_track_metrics_.objects_dropped_not_ok++;
                return PublishObjectStatus::kNotAnnounced;
            case Status::kAnnounceNotAuthorized:
                publish_track_metrics_.objects_dropped_not_ok++;
                return PublishObjectStatus::kNotAuthorized;
            default:
                publish_track_metrics_.objects_dropped_not_ok++;
                return PublishObjectStatus::kInternalError;
        }

        if (object_headers.track_mode.has_value() && object_headers.track_mode != default_track_mode_) {
            SetDefaultTrackMode(*object_headers.track_mode);
        }

        bool is_stream_header_needed{ false };
        switch (default_track_mode_) {
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

        publish_track_metrics_.bytes_published += data.size();
        publish_track_metrics_.objects_published++;

        if (publish_object_func_ != nullptr) {
            return publish_object_func_(object_headers.priority.has_value() ? object_headers.priority.value()
                                                                            : default_priority_,
                                        object_headers.ttl.has_value() ? object_headers.ttl.value() : default_ttl_,
                                        is_stream_header_needed,
                                        object_headers.group_id,
                                        object_headers.object_id,
                                        object_headers.extensions,
                                        data);
        } else {
            return PublishObjectStatus::kInternalError;
        }
    }

} // namespace quicr
