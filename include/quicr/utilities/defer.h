// SPDX-FileCopyrightText: Copyright (c) 2024 Cisco Systems
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

#include <functional>

#define DEFER_CONCAT(a, b) DEFER_CONCAT_INNER(a, b)
#define DEFER_CONCAT_INNER(a, b) a##b

#if defined(__clang__) && __clang_major__ >= 21
#define DEFER_PUSH _Pragma("clang diagnostic push") _Pragma("clang diagnostic ignored \"-Wthread-safety-analysis\"")
#define DEFER_POP _Pragma("clang diagnostic pop")
#else
#define DEFER_PUSH
#define DEFER_POP
#endif

#ifdef _WIN32
#define defer(n)                                                                                                       \
    quicr::ScopeGuard DEFER_CONCAT(defer_, __LINE__)([&]() {                                                           \
        DEFER_PUSH n;                                                                                                  \
        DEFER_POP                                                                                                      \
    })
#else
#define defer(n)                                                                                                       \
    quicr::ScopeGuard DEFER_CONCAT(defer_, __LINE__)([&]() __attribute__((always_inline)) {                            \
        DEFER_PUSH n;                                                                                                  \
        DEFER_POP                                                                                                      \
    })
#endif

namespace quicr {
    template<typename F>
    class ScopeGuard
    {
      public:
        explicit ScopeGuard(F&& f)
          : func(std::forward<F>(f))
        {
        }

        ScopeGuard(const ScopeGuard&) = delete;
        ScopeGuard& operator=(const ScopeGuard&) = delete;

        ~ScopeGuard() noexcept { func(); }

      private:
        F func;
    };
}
