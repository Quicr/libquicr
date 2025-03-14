// SPDX-FileCopyrightText: Copyright (c) 2025 Cisco Systems
// SPDX-License-Identifier: BSD-2-Clause

#include <quicr/publish_fetch_handler.h>

namespace quicr {
    PublishTrackHandler::PublishObjectStatus PublishFetchHandler::PublishObject(const ObjectHeaders& object_headers,
                                                                                const BytesSpan data)
    {
        bool is_stream_header_needed{ !sent_first_header_ };
        sent_first_header_ = true;
        if (publish_object_func_ == nullptr) {
            return PublishObjectStatus::kInternalError;
        }
        return publish_object_func_(object_headers.priority.has_value() ? object_headers.priority.value()
                                                                        : GetDefaultPriority(),
                                    object_headers.ttl.has_value() ? object_headers.ttl.value() : 0,
                                    is_stream_header_needed,
                                    object_headers.group_id,
                                    object_headers.subgroup_id,
                                    object_headers.object_id,
                                    object_headers.extensions,
                                    data);
    }
}