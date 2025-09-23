// SPDX-FileCopyrightText: Copyright (c) 2024 Cisco Systems
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

#include <functional>

#define DEFER_CONCAT(a, b) DEFER_CONCAT_INNER(a, b)
#define DEFER_CONCAT_INNER(a, b) a##b
#define defer(n) quicr::DeferType DEFER_CONCAT(defer_, __COUNTER__)([&] { n; })

namespace quicr {
    class DeferType
    {
      public:
        explicit DeferType(std::function<void()>&& f)
          : func{ f }
        {
        }
        ~DeferType() { func(); }

      private:
        std::function<void()> func;
    };
}
