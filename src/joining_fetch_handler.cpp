// SPDX-FileCopyrightText: Copyright (c) 2025 Cisco Systems
// SPDX-License-Identifier: BSD-2-Clause

#include "quicr/handlers/joining_fetch_handler.h"

namespace quicr {
    void JoiningFetchHandler::ObjectReceived(const ObjectHeaders& object_headers,
                                             BytesSpan data,
                                             std::optional<messages::StreamHeaderProperties> stream_mode)
    {
        joining_subscribe_->ObjectReceived(object_headers, data, std::move(stream_mode));
    }
}
