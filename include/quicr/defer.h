// SPDX-FileCopyrightText: Copyright (c) 2024 Cisco Systems
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

#include <functional>

#define DEFER_CONCAT(a, b) DEFER_CONCAT_INNER(a, b)
#define DEFER_CONCAT_INNER(a, b) a##b

#if defined(__clang__)
#define DEFER_PUSH _Pragma("clang diagnostic push") _Pragma("clang diagnostic ignored \"-Wthread-safety-analysis\"")
#define DEFER_POP _Pragma("clang diagnostic pop")
#else
#define DEFER_PUSH
#define DEFER_POP
#endif

#define defer(n)                                                                                                       \
    quicr::DeferType DEFER_CONCAT(defer_, __LINE__)([&] {                                                              \
        DEFER_PUSH n;                                                                                                  \
        DEFER_POP                                                                                                      \
    })

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
