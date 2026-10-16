// SPDX-FileCopyrightText: Copyright (c) 2026 Cisco Systems
// SPDX-License-Identifier: BSD-2-Clause

#include "quicr/clock.h"
#include "timeq_clock.h"

#include <timeq/tick_service.h>

namespace quicr {
    std::shared_ptr<Clock> Clock::Create(std::uint64_t sleep_delay_us)
    {
        return std::make_shared<TimeqClock>(sleep_delay_us);
    }
} // namespace quicr
