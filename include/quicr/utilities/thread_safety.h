// SPDX-FileCopyrightText: Copyright (c) 2024 Cisco Systems
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

// Probe the standard library implementation. The thread-safety attributes below
// only work when the std synchronization primitives are themselves annotated:
// guarded_by() requires its guard's type to carry the `capability` attribute, and
// std::lock_guard must be `scoped_lockable` for the analysis to see a lock being
// held. libc++ annotates std::mutex/std::lock_guard this way; libstdc++ does not,
// so on Clang + libstdc++ these attributes fail to compile (e.g. guarded_by on a
// plain std::mutex). Gate on libc++ so the annotations are active only where the
// std types support them, and no-op everywhere else.
#include <version>

#if defined(__clang__) && __clang_major__ >= 21 && defined(_LIBCPP_HAS_THREAD_SAFETY_ANNOTATIONS)
#define QUICR_CAPABILITY(name) __attribute__((capability(name)))
#define QUICR_ACQUIRE(...) __attribute__((acquire_capability(__VA_ARGS__)))
#define QUICR_RELEASE(...) __attribute__((release_capability(__VA_ARGS__)))
#define QUICR_REQUIRES(...) __attribute__((requires_capability(__VA_ARGS__)))
#define QUICR_TRY_ACQUIRE(...) __attribute__((try_acquire_capability(__VA_ARGS__)))
#define QUICR_GUARDED_BY(...) __attribute__((guarded_by(__VA_ARGS__)))
#define QUICR_PT_GUARDED_BY(...) __attribute__((pt_guarded_by(__VA_ARGS__)))
#else
#define QUICR_CAPABILITY(name)
#define QUICR_ACQUIRE(...)
#define QUICR_RELEASE(...)
#define QUICR_REQUIRES(...)
#define QUICR_TRY_ACQUIRE(...)
#define QUICR_GUARDED_BY(...)
#define QUICR_PT_GUARDED_BY(...)
#endif
