// SPDX-FileCopyrightText: Copyright (c) 2026 Cisco Systems
// SPDX-License-Identifier: BSD-2-Clause

/*
 * The examples poll in a few places while waiting on the network, and standard C
 * has no portable sleep. This is the one platform difference they need.
 */

#ifndef QUICR_BRIDGE_EXAMPLE_SLEEP_H
#define QUICR_BRIDGE_EXAMPLE_SLEEP_H

#ifdef _WIN32

#include <windows.h>

static inline void
example_sleep_ms(unsigned milliseconds)
{
    Sleep((DWORD)milliseconds);
}

#else

#include <time.h>

static inline void
example_sleep_ms(unsigned milliseconds)
{
    const struct timespec duration = { .tv_sec = milliseconds / 1000,
                                       .tv_nsec = (long)(milliseconds % 1000) * 1000000L };
    nanosleep(&duration, NULL);
}

#endif

#endif /* QUICR_BRIDGE_EXAMPLE_SLEEP_H */
