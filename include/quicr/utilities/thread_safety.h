// SPDX-FileCopyrightText: Copyright (c) 2024 Cisco Systems
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

/**
 * @file thread_safety.h
 * @brief Portable wrappers for Clang thread-safety analysis attributes.
 *
 * When `QUICR_HAS_THREAD_SAFETY_ANNOTATIONS` is not defined, every macro expands to nothing and has no runtime
 * effect. These annotations describe locking contracts to static analysis; they do not acquire locks themselves.
 */

#if defined(QUICR_HAS_THREAD_SAFETY_ANNOTATIONS)
/** @brief Marks a type or object as a named lockable capability. */
#define QUICR_CAPABILITY(name) __attribute__((capability(name)))

/** @brief Declares that a function acquires the listed capabilities. */
#define QUICR_ACQUIRE(...) __attribute__((acquire_capability(__VA_ARGS__)))

/** @brief Declares that a function releases the listed capabilities. */
#define QUICR_RELEASE(...) __attribute__((release_capability(__VA_ARGS__)))

/** @brief Declares capabilities that callers must already hold. */
#define QUICR_REQUIRES(...) __attribute__((requires_capability(__VA_ARGS__)))

/** @brief Declares capabilities that callers must not hold. */
#define QUICR_EXCLUDES(...) __attribute__((locks_excluded(__VA_ARGS__)))

/** @brief Disables Clang thread-safety analysis for a declaration. */
#define QUICR_NO_THREAD_SAFETY_ANALYSIS __attribute__((no_thread_safety_analysis))

/** @brief Declares a try-lock function and the return value indicating success. */
#define QUICR_TRY_ACQUIRE(...) __attribute__((try_acquire_capability(__VA_ARGS__)))

/** @brief Declares that access to a field requires the listed capability. */
#define QUICR_GUARDED_BY(...) __attribute__((guarded_by(__VA_ARGS__)))

/** @brief Declares that access through a pointer requires the listed capability. */
#define QUICR_PT_GUARDED_BY(...) __attribute__((pt_guarded_by(__VA_ARGS__)))
#else
/** @brief No-op capability marker when thread-safety annotations are disabled. */
#define QUICR_CAPABILITY(name)

/** @brief No-op acquire annotation when thread-safety annotations are disabled. */
#define QUICR_ACQUIRE(...)

/** @brief No-op release annotation when thread-safety annotations are disabled. */
#define QUICR_RELEASE(...)

/** @brief No-op lock-requirement annotation when thread-safety annotations are disabled. */
#define QUICR_REQUIRES(...)

/** @brief No-op lock-exclusion annotation when thread-safety annotations are disabled. */
#define QUICR_EXCLUDES(...)

/** @brief No-op analysis-suppression annotation when thread-safety annotations are disabled. */
#define QUICR_NO_THREAD_SAFETY_ANALYSIS

/** @brief No-op try-acquire annotation when thread-safety annotations are disabled. */
#define QUICR_TRY_ACQUIRE(...)

/** @brief No-op guarded-field annotation when thread-safety annotations are disabled. */
#define QUICR_GUARDED_BY(...)

/** @brief No-op pointer-guard annotation when thread-safety annotations are disabled. */
#define QUICR_PT_GUARDED_BY(...)
#endif
