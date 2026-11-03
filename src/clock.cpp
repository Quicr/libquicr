// SPDX-FileCopyrightText: Copyright (c) 2026 Cisco Systems
// SPDX-License-Identifier: BSD-2-Clause

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

    std::shared_ptr<Clock> Clock::Create(std::uint64_t sleep_delay_us)
    {
        return std::make_shared<TimeqClock>(sleep_delay_us);
    }
} // namespace quicr
