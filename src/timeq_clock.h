// SPDX-FileCopyrightText: Copyright (c) 2026 Cisco Systems
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

#include "quicr/clock.h"

#include <timeq/tick_service.h>

namespace quicr {
    struct TimeqClock
      : public Clock
      , public timeq::threaded_tick_service
    {
        using timeq::threaded_tick_service::threaded_tick_service;

        virtual ~TimeqClock() = default;

        std::chrono::microseconds Get() const override { return get(); }
    };
}
