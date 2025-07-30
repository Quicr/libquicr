// SPDX-FileCopyrightText: Copyright (c) 2024 Cisco Systems
// SPDX-License-Identifier: BSD-2-Clause

#include <quicr/publish_track_handler.h>

namespace quicr {
    void PublishTrackHandler::StatusChanged(Status) {}
    void PublishTrackHandler::MetricsSampled(const PublishTrackMetrics&) {}

    PublishTrackHandler::PublishObjectStatus PublishTrackHandler::ForwardPublishedData(
      bool is_new_stream,
      std::shared_ptr<const std::vector<uint8_t>> data)
    {
        switch (publish_status_) {
            case Status::kOk:
                break;

            case Status::kPaused:
                publish_track_metrics_.objects_dropped_not_ok++;
                return PublishObjectStatus::kPaused;

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
            case Status::kNewGroupRequested:
                [[fallthrough]];
            case Status::kSubscriptionUpdated:
                // reset the status to ok to imply change
                if (!is_new_stream) {
                    break;
                }
                publish_status_ = Status::kOk;
                break;
            default:
                publish_track_metrics_.objects_dropped_not_ok++;
                return PublishObjectStatus::kInternalError;
        }

        publish_track_metrics_.bytes_published += data->size();

        if (forward_publish_data_func_ != nullptr) {
            return forward_publish_data_func_(default_priority_, latest_group_id_, default_ttl_, is_new_stream, data);
        }

        return PublishObjectStatus::kInternalError;
    }

    PublishTrackHandler::PublishObjectStatus PublishTrackHandler::PublishObject(const ObjectHeaders& object_headers,
                                                                                BytesSpan data)
    {
        bool is_stream_header_needed{ false };

        // change in subgroups and groups require a new stream

        is_stream_header_needed = not sent_first_header_ || latest_sub_group_id_ != object_headers.subgroup_id ||
                                  latest_group_id_ != object_headers.group_id;

        switch (publish_status_) {
            case Status::kOk:
                break;

            case Status::kPaused:
                publish_track_metrics_.objects_dropped_not_ok++;
                return PublishObjectStatus::kPaused;

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
            case Status::kNewGroupRequested:
                // reset the status to ok to imply change
                if (!is_stream_header_needed) {
                    break;
                }
                publish_status_ = Status::kOk;
                break;
            case Status::kSubscriptionUpdated:

                /*
                 * Always start a new stream on subscription update to support peering/pipelining
                 */
                is_stream_header_needed = true;

                publish_status_ = Status::kOk;
                break;
            default:
                publish_track_metrics_.objects_dropped_not_ok++;
                return PublishObjectStatus::kInternalError;
        }

        if (object_headers.track_mode.has_value() && object_headers.track_mode != default_track_mode_) {
            SetDefaultTrackMode(*object_headers.track_mode);
        }

        sent_first_header_ = true;

        latest_group_id_ = object_headers.group_id;
        latest_sub_group_id_ = object_headers.subgroup_id;
        publish_track_metrics_.bytes_published += data.size();
        publish_track_metrics_.objects_published++;

        if (publish_object_func_ != nullptr) {
            return publish_object_func_(object_headers.priority.has_value() ? object_headers.priority.value()
                                                                            : default_priority_,
                                        object_headers.ttl.has_value() ? object_headers.ttl.value() : default_ttl_,
                                        is_stream_header_needed,
                                        object_headers.group_id,
                                        object_headers.subgroup_id,
                                        object_headers.object_id,
                                        object_headers.extensions,
                                        data);
        }

        return PublishObjectStatus::kInternalError;
    }

} // namespace quicr
