// SPDX-FileCopyrightText: Copyright (c) 2026 Cisco Systems
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

#include <chrono>
#include <cstdint>
#include <memory>

namespace quicr {

    /// Monotonic microsecond clock abstraction.
    class Clock
    {
      public:
        /**
         * Creates the default clock implementation.
         * @param sleep_delay_us Polling delay in microseconds.
         * @return Shared clock instance.
         */
        static std::shared_ptr<Clock> Create(std::uint64_t sleep_delay_us = 333);

        /// Destroys the clock.
        virtual ~Clock() = default;

        /// @return Current monotonic time in microseconds.
        virtual std::chrono::microseconds Get() const = 0;

        /**
         * Returns the current monotonic time converted to another duration.
         * @tparam D Target std::chrono duration type.
         * @return Current time represented as @p D.
         */
        template<typename D>
        D Get() const
        {
            return std::chrono::duration_cast<D>(Get());
        }
    };

}
