/*
 * SPDX-FileCopyrightText: Copyright (c) 2024 Cisco Systems
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Bare-metal sys/prctl.h stub header
 */

#ifndef QUICR_BAREMETAL_SYS_PRCTL_H
#define QUICR_BAREMETAL_SYS_PRCTL_H

#ifdef QUICR_BAREMETAL

#ifdef __cplusplus
extern "C" {
#endif

/* prctl options - not used on bare-metal */
#define PR_SET_NAME 15

/* Stub prctl function - no process control on bare-metal */
static inline int prctl(int option, ...) {
    (void)option;
    return 0;
}

#ifdef __cplusplus
}
#endif

#endif /* QUICR_BAREMETAL */

#endif /* QUICR_BAREMETAL_SYS_PRCTL_H */
