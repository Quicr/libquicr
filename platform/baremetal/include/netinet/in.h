/*
 * SPDX-FileCopyrightText: Copyright (c) 2024 Cisco Systems
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Bare-metal netinet/in.h compatibility header
 */

#ifndef QUICR_BAREMETAL_NETINET_IN_H
#define QUICR_BAREMETAL_NETINET_IN_H

#ifdef QUICR_BAREMETAL

#include <stdint.h>

#ifdef QUICR_USE_LWIP
#include "lwip/inet.h"
#else

#ifdef __cplusplus
extern "C" {
#endif

/* Address families */
#ifndef AF_UNSPEC
#define AF_UNSPEC 0
#endif

#ifndef AF_INET
#define AF_INET 2
#endif

#ifndef AF_INET6
#define AF_INET6 10
#endif

/* Protocol families */
#ifndef PF_INET
#define PF_INET AF_INET
#endif

#ifndef PF_INET6
#define PF_INET6 AF_INET6
#endif

/* IP protocols */
#ifndef IPPROTO_IP
#define IPPROTO_IP 0
#endif

#ifndef IPPROTO_TCP
#define IPPROTO_TCP 6
#endif

#ifndef IPPROTO_UDP
#define IPPROTO_UDP 17
#endif

/* Type definitions */
#ifndef _IN_PORT_T_DECLARED
#define _IN_PORT_T_DECLARED
typedef uint16_t in_port_t;
#endif

#ifndef _IN_ADDR_T_DECLARED
#define _IN_ADDR_T_DECLARED
typedef uint32_t in_addr_t;
#endif

/* Internet address structure */
#ifndef _IN_ADDR_DECLARED
#define _IN_ADDR_DECLARED
struct in_addr {
    in_addr_t s_addr;
};
#endif

/* IPv6 address structure */
#ifndef _IN6_ADDR_DECLARED
#define _IN6_ADDR_DECLARED
struct in6_addr {
    union {
        uint8_t  s6_addr[16];
        uint16_t s6_addr16[8];
        uint32_t s6_addr32[4];
    };
};
#endif

/* Socket address for IPv4 */
#ifndef _SOCKADDR_IN_DECLARED
#define _SOCKADDR_IN_DECLARED
struct sockaddr_in {
    uint8_t        sin_len;
    uint8_t        sin_family;
    in_port_t      sin_port;
    struct in_addr sin_addr;
    char           sin_zero[8];
};
#endif

/* Socket address for IPv6 */
#ifndef _SOCKADDR_IN6_DECLARED
#define _SOCKADDR_IN6_DECLARED
struct sockaddr_in6 {
    uint8_t         sin6_len;
    uint8_t         sin6_family;
    in_port_t       sin6_port;
    uint32_t        sin6_flowinfo;
    struct in6_addr sin6_addr;
    uint32_t        sin6_scope_id;
};
#endif

/* Special addresses */
#ifndef INADDR_ANY
#define INADDR_ANY       ((in_addr_t)0x00000000)
#endif

#ifndef INADDR_BROADCAST
#define INADDR_BROADCAST ((in_addr_t)0xffffffff)
#endif

#ifndef INADDR_NONE
#define INADDR_NONE      ((in_addr_t)0xffffffff)
#endif

#ifndef INADDR_LOOPBACK
#define INADDR_LOOPBACK  ((in_addr_t)0x7f000001)
#endif

/* IPv6 special addresses */
#define IN6ADDR_ANY_INIT      {{0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0}}
#define IN6ADDR_LOOPBACK_INIT {{0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1}}

/* Byte order conversion - assuming little-endian (ESP32-S3 is little-endian) */
#ifndef htons
#define htons(x) ((uint16_t)((((x) & 0x00ff) << 8) | (((x) & 0xff00) >> 8)))
#endif

#ifndef ntohs
#define ntohs(x) htons(x)
#endif

#ifndef htonl
#define htonl(x) ((uint32_t)((((x) & 0x000000ff) << 24) | \
                              (((x) & 0x0000ff00) <<  8) | \
                              (((x) & 0x00ff0000) >>  8) | \
                              (((x) & 0xff000000) >> 24)))
#endif

#ifndef ntohl
#define ntohl(x) htonl(x)
#endif

/* IPv4 packet info structure (for IP_PKTINFO) */
#ifndef _IN_PKTINFO_DECLARED
#define _IN_PKTINFO_DECLARED
struct in_pktinfo {
    int            ipi_ifindex;   /* interface index */
    struct in_addr ipi_spec_dst;  /* local address */
    struct in_addr ipi_addr;      /* destination address */
};
#endif

/* IPv6 packet info structure (for IPV6_PKTINFO) */
#ifndef _IN6_PKTINFO_DECLARED
#define _IN6_PKTINFO_DECLARED
struct in6_pktinfo {
    struct in6_addr ipi6_addr;    /* src/dst IPv6 address */
    unsigned int    ipi6_ifindex; /* interface index */
};
#endif

#ifdef __cplusplus
}
#endif

#endif /* QUICR_USE_LWIP */

#endif /* QUICR_BAREMETAL */

#endif /* QUICR_BAREMETAL_NETINET_IN_H */
