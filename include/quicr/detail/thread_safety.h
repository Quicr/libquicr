// SPDX-FileCopyrightText: Copyright (c) 2024 Cisco Systems
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

#if defined(__clang__) && __clang_major__ >= 21
#define QUICR_CAPABILITY(name) __attribute__((capability(name)))
#define QUICR_ACQUIRE(...) __attribute__((acquire_capability(__VA_ARGS__)))
#define QUICR_RELEASE(...) __attribute__((release_capability(__VA_ARGS__)))
#define QUICR_REQUIRES(...) __attribute__((requires_capability(__VA_ARGS__)))
#define QUICR_TRY_ACQUIRE(...) __attribute__((try_acquire_capability(__VA_ARGS__)))
#define QUICR_GUARDED_BY(...) __attribute__((guarded_by(__VA_ARGS__)))
#else
#define QUICR_CAPABILITY(name)
#define QUICR_ACQUIRE(...)
#define QUICR_RELEASE(...)
#define QUICR_REQUIRES(...)
#define QUICR_TRY_ACQUIRE(...)
#define QUICR_GUARDED_BY(...)
#endif
