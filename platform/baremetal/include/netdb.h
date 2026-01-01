/*
 * SPDX-FileCopyrightText: Copyright (c) 2024 Cisco Systems
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Bare-metal netdb.h stub header
 */

#ifndef QUICR_BAREMETAL_NETDB_H
#define QUICR_BAREMETAL_NETDB_H

#ifdef QUICR_BAREMETAL

#ifdef QUICR_USE_LWIP
#include "lwip/netdb.h"
#else

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Error codes for getaddrinfo/getnameinfo */
#define EAI_AGAIN    2
#define EAI_BADFLAGS 3
#define EAI_FAIL     4
#define EAI_FAMILY   5
#define EAI_MEMORY   6
#define EAI_NONAME   8
#define EAI_SERVICE  9
#define EAI_SOCKTYPE 10
#define EAI_SYSTEM   11
#define EAI_OVERFLOW 14

/* Flags for getaddrinfo */
#define AI_PASSIVE     0x0001
#define AI_CANONNAME   0x0002
#define AI_NUMERICHOST 0x0004
#define AI_NUMERICSERV 0x0400

/* Flags for getnameinfo */
#define NI_NUMERICHOST 0x0001
#define NI_NUMERICSERV 0x0002
#define NI_NOFQDN      0x0004
#define NI_NAMEREQD    0x0008
#define NI_DGRAM       0x0010

struct addrinfo {
    int              ai_flags;
    int              ai_family;
    int              ai_socktype;
    int              ai_protocol;
    uint32_t         ai_addrlen;
    struct sockaddr *ai_addr;
    char            *ai_canonname;
    struct addrinfo *ai_next;
};

/* Stub functions - DNS is not available on bare-metal by default */
static inline int getaddrinfo(const char *node, const char *service,
                              const struct addrinfo *hints,
                              struct addrinfo **res) {
    (void)node; (void)service; (void)hints; (void)res;
    return EAI_FAIL;
}

static inline void freeaddrinfo(struct addrinfo *res) {
    (void)res;
}

static inline const char *gai_strerror(int errcode) {
    (void)errcode;
    return "DNS not supported on bare-metal";
}

static inline int getnameinfo(const struct sockaddr *addr, uint32_t addrlen,
                              char *host, uint32_t hostlen,
                              char *serv, uint32_t servlen, int flags) {
    (void)addr; (void)addrlen; (void)host; (void)hostlen;
    (void)serv; (void)servlen; (void)flags;
    return EAI_FAIL;
}

#ifdef __cplusplus
}
#endif

#endif /* QUICR_USE_LWIP */

#endif /* QUICR_BAREMETAL */

#endif /* QUICR_BAREMETAL_NETDB_H */
