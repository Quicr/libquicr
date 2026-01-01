/*
 * SPDX-FileCopyrightText: Copyright (c) 2024 Cisco Systems
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Bare-metal sys/select.h stub header
 */

#ifndef QUICR_BAREMETAL_SYS_SELECT_H
#define QUICR_BAREMETAL_SYS_SELECT_H

#ifdef QUICR_BAREMETAL

#include <stdint.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

/* FD_SETSIZE - maximum number of file descriptors */
#ifndef FD_SETSIZE
#define FD_SETSIZE 64
#endif

/* fd_set type */
typedef struct {
    unsigned long fds_bits[FD_SETSIZE / (8 * sizeof(unsigned long))];
} fd_set;

/* fd_set macros */
#define FD_ZERO(set)    memset((set), 0, sizeof(fd_set))
#define FD_SET(fd, set) ((set)->fds_bits[(fd) / (8 * sizeof(unsigned long))] |= (1UL << ((fd) % (8 * sizeof(unsigned long)))))
#define FD_CLR(fd, set) ((set)->fds_bits[(fd) / (8 * sizeof(unsigned long))] &= ~(1UL << ((fd) % (8 * sizeof(unsigned long)))))
#define FD_ISSET(fd, set) (((set)->fds_bits[(fd) / (8 * sizeof(unsigned long))] & (1UL << ((fd) % (8 * sizeof(unsigned long))))) != 0)

/* timeval structure */
#ifndef _TIMEVAL_DEFINED
#define _TIMEVAL_DEFINED
struct timeval {
    long tv_sec;
    long tv_usec;
};
#endif

/* select - stub that returns error (not supported) */
static inline int select(int nfds, fd_set *readfds, fd_set *writefds,
                         fd_set *exceptfds, struct timeval *timeout) {
    (void)nfds; (void)readfds; (void)writefds; (void)exceptfds; (void)timeout;
    return -1;  /* Not supported on bare-metal */
}

#ifdef __cplusplus
}
#endif

#endif /* QUICR_BAREMETAL */

#endif /* QUICR_BAREMETAL_SYS_SELECT_H */
