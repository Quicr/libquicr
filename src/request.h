// SPDX-FileCopyrightText: Copyright (c) 2026 Cisco Systems
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

#include "quicr/track_name.h"

#include <cstdint>
#include <memory>
#include <vector>

namespace quicr {

    class TrackHandler;
    class Stream;

    struct Request
    {
        enum class Type
        {
            kPublish,
            kSubscribe,
            kFetch,
            kPublishNamespace,
            kSubscribeNamespace,
            kTrackStatus,
        };

        const std::uint64_t request_id;
        const Type type;
        std::shared_ptr<Stream> stream;
        const FullTrackName name;
        std::vector<std::shared_ptr<TrackHandler>> handlers;
        const bool is_locally_initiated;
    };

}
