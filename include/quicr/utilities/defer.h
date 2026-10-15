// SPDX-FileCopyrightText: Copyright (c) 2024 Cisco Systems
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

#include <utility>

#define QUICR_DEFER_CONCAT(a, b) QUICR_DEFER_CONCAT_INNER(a, b)
#define QUICR_DEFER_CONCAT_INNER(a, b) a##b

#if defined(__clang__) && __clang_major__ >= 21
#define QUICR_DEFER_PUSH                                                                                               \
    _Pragma("clang diagnostic push") _Pragma("clang diagnostic ignored \"-Wthread-safety-analysis\"")
#define QUICR_DEFER_POP _Pragma("clang diagnostic pop")
#else
#define QUICR_DEFER_PUSH
#define QUICR_DEFER_POP
#endif

#define defer(n)                                                                                                       \
    quicr::ScopeGuard QUICR_DEFER_CONCAT(defer_, __LINE__)([&]() {                                                     \
        QUICR_DEFER_PUSH n;                                                                                            \
        QUICR_DEFER_POP                                                                                                \
    })

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
