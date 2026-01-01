/*
 * SPDX-FileCopyrightText: Copyright (c) 2024 Cisco Systems
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Bare-metal netinet/udp.h stub header
 */

#ifndef QUICR_BAREMETAL_NETINET_UDP_H
#define QUICR_BAREMETAL_NETINET_UDP_H

#ifdef QUICR_BAREMETAL

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* UDP header structure */
struct udphdr {
    uint16_t uh_sport;   /* source port */
    uint16_t uh_dport;   /* destination port */
    uint16_t uh_ulen;    /* udp length */
    uint16_t uh_sum;     /* udp checksum */
};

/* Alternative field names (BSD style) */
#define source uh_sport
#define dest   uh_dport
#define len    uh_ulen
#define check  uh_sum

#ifdef __cplusplus
}
#endif

#endif /* QUICR_BAREMETAL */

#endif /* QUICR_BAREMETAL_NETINET_UDP_H */
