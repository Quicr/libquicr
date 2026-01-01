/*
 * SPDX-FileCopyrightText: Copyright (c) 2024 Cisco Systems
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Bare-metal fcntl.h compatibility header
 *
 * Note: This header provides O_CLOEXEC and other defines that may be
 * missing from newlib's fcntl.h. It should be included BEFORE system headers
 * via the baremetal include path.
 */

#ifndef QUICR_BAREMETAL_FCNTL_H
#define QUICR_BAREMETAL_FCNTL_H

/* Define O_CLOEXEC before including system fcntl.h */
#ifndef O_CLOEXEC
#define O_CLOEXEC 0  /* Set close-on-exec - no effect on bare-metal single-process */
#endif

/* Now include newlib's fcntl.h */
/* Using angle brackets with BEFORE include path means our header comes first */
#ifdef QUICR_BAREMETAL
/* Get the newlib definitions - this will use _default_fcntl.h or similar */
#include <sys/fcntl.h>
#else
/* Non-baremetal, just chain to system header */
#include_next <fcntl.h>
#endif

/* Additional defines if not present after system include */
#ifndef FD_CLOEXEC
#define FD_CLOEXEC 1
#endif

#ifndef F_GETFD
#define F_GETFD 1
#endif

#ifndef F_SETFD
#define F_SETFD 2
#endif

#ifndef F_GETFL
#define F_GETFL 3
#endif

#ifndef F_SETFL
#define F_SETFL 4
#endif

#endif /* QUICR_BAREMETAL_FCNTL_H */
