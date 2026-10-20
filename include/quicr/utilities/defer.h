// SPDX-FileCopyrightText: Copyright (c) 2024 Cisco Systems
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

#include <functional>

/** @brief Expands and concatenates two preprocessing tokens. */
#define DEFER_CONCAT(a, b) DEFER_CONCAT_INNER(a, b)

/** @brief Concatenates two preprocessing tokens after outer expansion. */
#define DEFER_CONCAT_INNER(a, b) a##b

#if defined(__clang__) && __clang_major__ >= 21
/** @brief Begins suppression of Clang thread-safety diagnostics inside a deferred action. */
#define DEFER_PUSH _Pragma("clang diagnostic push") _Pragma("clang diagnostic ignored \"-Wthread-safety-analysis\"")

/** @brief Ends suppression of Clang thread-safety diagnostics inside a deferred action. */
#define DEFER_POP _Pragma("clang diagnostic pop")
#else
/** @brief No-op on compilers that do not require deferred-action diagnostic suppression. */
#define DEFER_PUSH

/** @brief No-op on compilers that do not require deferred-action diagnostic suppression. */
#define DEFER_POP
#endif

#ifdef _WIN32
/**
 * @brief Runs statements when the current scope exits.
 * @param n Statements captured by reference and executed exactly once by a generated scope guard.
 * @warning Every referenced object must outlive the generated guard. An exception escaping `n` during stack
 * unwinding terminates the process because the guard destructor is `noexcept`.
 */
#define defer(n)                                                                                                       \
    quicr::ScopeGuard DEFER_CONCAT(defer_, __LINE__)([&]() [[msv::forceinline]] {                                      \
        DEFER_PUSH n;                                                                                                  \
        DEFER_POP                                                                                                      \
    })
#else
/**
 * @brief Runs statements when the current scope exits.
 * @param n Statements captured by reference and executed exactly once by a generated scope guard.
 * @warning Every referenced object must outlive the generated guard. An exception escaping `n` during stack
 * unwinding terminates the process because the guard destructor is `noexcept`.
 */
#define defer(n)                                                                                                       \
    quicr::ScopeGuard DEFER_CONCAT(defer_, __LINE__)([&]() __attribute__((always_inline)) {                            \
        DEFER_PUSH n;                                                                                                  \
        DEFER_POP                                                                                                      \
    })
#endif

namespace quicr {
    /**
     * @brief Non-copyable RAII guard that invokes a callable at scope exit.
     * @tparam F Callable type, stored by value.
     * @note The guard is not thread-safe; synchronize concurrent access to captured state externally.
     */
    template<typename F>
    class ScopeGuard
    {
      public:
        /**
         * @brief Takes ownership of a callable to run at destruction.
         * @param f Callable moved into the guard.
         * @throws Any exception thrown while moving `f`.
         */
        explicit ScopeGuard(F&& f)
          : func(std::forward<F>(f))
        {
        }

        /** @brief Scope guards cannot be copied. @param other Guard that cannot be copied. */
        ScopeGuard(const ScopeGuard& other) = delete;

        /** @brief Scope guards cannot be copy-assigned. @param other Guard that cannot be copied. @return `*this`. */
        ScopeGuard& operator=(const ScopeGuard& other) = delete;

        /**
         * @brief Invokes the stored callable.
         * @warning If the callable throws, `std::terminate` is called.
         */
        ~ScopeGuard() noexcept { func(); }

      private:
        F func;
    };
}
