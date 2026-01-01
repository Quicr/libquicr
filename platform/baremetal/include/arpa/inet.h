/*
 * SPDX-FileCopyrightText: Copyright (c) 2024 Cisco Systems
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Bare-metal arpa/inet.h compatibility header
 */

#ifndef QUICR_BAREMETAL_ARPA_INET_H
#define QUICR_BAREMETAL_ARPA_INET_H

#ifdef QUICR_BAREMETAL

#include <stdint.h>
#include <string.h>

#ifdef QUICR_USE_LWIP
#include "lwip/inet.h"
#else

#ifdef __cplusplus
extern "C" {
#endif

/* Address families (must match sys/socket.h) */
#ifndef AF_INET
#define AF_INET 2
#endif

#ifndef AF_INET6
#define AF_INET6 10
#endif

/* Internet address structure */
#ifndef _IN_ADDR_DECLARED
#define _IN_ADDR_DECLARED
struct in_addr {
    uint32_t s_addr;
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

/* inet_ntoa - convert IPv4 address to string */
static inline char *inet_ntoa(struct in_addr in) {
    static char buf[16];
    uint8_t *b = (uint8_t *)&in.s_addr;
    int len = 0;
    for (int i = 0; i < 4; i++) {
        if (i > 0) buf[len++] = '.';
        if (b[i] >= 100) buf[len++] = '0' + b[i] / 100;
        if (b[i] >= 10) buf[len++] = '0' + (b[i] / 10) % 10;
        buf[len++] = '0' + b[i] % 10;
    }
    buf[len] = '\0';
    return buf;
}

/* inet_addr - convert dotted-decimal to network byte order */
static inline uint32_t inet_addr(const char *cp) {
    uint32_t addr = 0;
    int octet = 0;
    int value = 0;
    while (*cp) {
        if (*cp >= '0' && *cp <= '9') {
            value = value * 10 + (*cp - '0');
        } else if (*cp == '.') {
            if (value > 255) return 0xffffffff;
            addr |= ((uint32_t)value << (octet * 8));
            value = 0;
            octet++;
            if (octet > 3) return 0xffffffff;
        } else {
            return 0xffffffff;
        }
        cp++;
    }
    if (value > 255 || octet != 3) return 0xffffffff;
    addr |= ((uint32_t)value << (octet * 8));
    return addr;
}

/* inet_pton - convert text address to binary form */
static inline int inet_pton(int af, const char *src, void *dst) {
    if (af == AF_INET) {
        struct in_addr *addr = (struct in_addr *)dst;
        uint32_t result = inet_addr(src);
        if (result == 0xffffffff && strcmp(src, "255.255.255.255") != 0) {
            return 0;
        }
        addr->s_addr = result;
        return 1;
    } else if (af == AF_INET6) {
        struct in6_addr *addr = (struct in6_addr *)dst;
        memset(addr, 0, sizeof(*addr));

        const char *p = src;
        int idx = 0;
        int double_colon_idx = -1;
        uint16_t val = 0;
        int seen_digit = 0;

        while (*p && idx < 8) {
            if (*p == ':') {
                if (!seen_digit) {
                    if (double_colon_idx >= 0) return 0;
                    double_colon_idx = idx;
                    p++;
                    if (*p == ':') p++;
                    continue;
                }
                addr->s6_addr16[idx++] = htons(val);
                val = 0;
                seen_digit = 0;
                p++;
            } else if ((*p >= '0' && *p <= '9') ||
                       (*p >= 'a' && *p <= 'f') ||
                       (*p >= 'A' && *p <= 'F')) {
                seen_digit = 1;
                val <<= 4;
                if (*p >= '0' && *p <= '9') val |= (*p - '0');
                else if (*p >= 'a' && *p <= 'f') val |= (*p - 'a' + 10);
                else val |= (*p - 'A' + 10);
                p++;
            } else {
                return 0;
            }
        }

        if (seen_digit && idx < 8) {
            addr->s6_addr16[idx++] = htons(val);
        }

        if (double_colon_idx >= 0 && idx < 8) {
            int move_count = idx - double_colon_idx;
            int new_pos = 8 - move_count;
            for (int i = move_count - 1; i >= 0; i--) {
                addr->s6_addr16[new_pos + i] = addr->s6_addr16[double_colon_idx + i];
                if (double_colon_idx + i < new_pos + i) {
                    addr->s6_addr16[double_colon_idx + i] = 0;
                }
            }
        }

        return 1;
    }
    return -1;
}

/* inet_ntop - convert binary address to text form */
static inline const char *inet_ntop(int af, const void *src, char *dst, uint32_t size) {
    if (af == AF_INET) {
        const struct in_addr *addr = (const struct in_addr *)src;
        char *result = inet_ntoa(*addr);
        if (strlen(result) >= size) return NULL;
        strcpy(dst, result);
        return dst;
    } else if (af == AF_INET6) {
        const struct in6_addr *addr = (const struct in6_addr *)src;
        if (size < 40) return NULL;
        char *p = dst;
        for (int i = 0; i < 8; i++) {
            if (i > 0) *p++ = ':';
            uint16_t val = ntohs(addr->s6_addr16[i]);
            char hex[5];
            int len = 0;
            if (val >= 0x1000) hex[len++] = "0123456789abcdef"[(val >> 12) & 0xf];
            if (val >= 0x100) hex[len++] = "0123456789abcdef"[(val >> 8) & 0xf];
            if (val >= 0x10) hex[len++] = "0123456789abcdef"[(val >> 4) & 0xf];
            hex[len++] = "0123456789abcdef"[val & 0xf];
            hex[len] = '\0';
            strcpy(p, hex);
            p += len;
        }
        *p = '\0';
        return dst;
    }
    return NULL;
}

#ifdef __cplusplus
}
#endif

#endif /* QUICR_USE_LWIP */

#endif /* QUICR_BAREMETAL */

#endif /* QUICR_BAREMETAL_ARPA_INET_H */
