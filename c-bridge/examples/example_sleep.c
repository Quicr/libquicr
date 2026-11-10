// SPDX-FileCopyrightText: Copyright (c) 2026 Cisco Systems
// SPDX-License-Identifier: BSD-2-Clause

#include "example_sleep.h"

#ifdef _WIN32

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

void
example_sleep_ms(unsigned milliseconds)
{
    Sleep((DWORD)milliseconds);
}

#else

#include <errno.h>
#include <time.h>

void
example_sleep_ms(unsigned milliseconds)
{
    struct timespec remaining = { .tv_sec = milliseconds / 1000, .tv_nsec = (long)(milliseconds % 1000) * 1000000L };

    while (nanosleep(&remaining, &remaining) == -1 && errno == EINTR) {
    }
}

#endif
