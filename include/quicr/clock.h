// SPDX-FileCopyrightText: Copyright (c) 2026 Cisco Systems
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

#include <chrono>
#include <cstdint>
#include <memory>

namespace quicr {

    class Clock
    {
      public:
        static std::shared_ptr<Clock> Create(std::uint64_t sleep_delay_us = 333);

        virtual ~Clock() = default;

        virtual std::chrono::microseconds Get() const = 0;

        template<typename D>
        D Get() const
        {
            return std::chrono::duration_cast<D>(Get());
        }
    };

}
