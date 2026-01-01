/*
 * SPDX-FileCopyrightText: Copyright (c) 2024 Cisco Systems
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Bare-metal sys/socket.h compatibility header
 */

#ifndef QUICR_BAREMETAL_SYS_SOCKET_H
#define QUICR_BAREMETAL_SYS_SOCKET_H

#ifdef QUICR_BAREMETAL

#include <stdint.h>
#include <stddef.h>
#include <sys/types.h>

/* Define ssize_t if not already defined (needed for socket functions) */
#ifndef _SSIZE_T_DECLARED
#define _SSIZE_T_DECLARED
typedef int ssize_t;
#endif

#ifdef QUICR_USE_LWIP
#include "lwip/sockets.h"
#else

#ifdef __cplusplus
extern "C" {
#endif

/* Socket types */
#ifndef SOCK_STREAM
#define SOCK_STREAM 1
#endif

#ifndef SOCK_DGRAM
#define SOCK_DGRAM 2
#endif

#ifndef SOCK_RAW
#define SOCK_RAW 3
#endif

/* Address families (also in netinet/in.h) */
#ifndef AF_UNSPEC
#define AF_UNSPEC 0
#endif

#ifndef AF_INET
#define AF_INET 2
#endif

/* Protocol families (typically same as address families) */
#ifndef PF_UNSPEC
#define PF_UNSPEC AF_UNSPEC
#endif

#ifndef PF_INET
#define PF_INET AF_INET
#endif

#ifndef AF_INET6
#define AF_INET6 10
#endif

#ifndef PF_INET6
#define PF_INET6 AF_INET6
#endif

/* Socket options */
#ifndef SOL_SOCKET
#define SOL_SOCKET 0xFFFF
#endif

#ifndef SO_REUSEADDR
#define SO_REUSEADDR 0x0004
#endif

#ifndef SO_KEEPALIVE
#define SO_KEEPALIVE 0x0008
#endif

#ifndef SO_BROADCAST
#define SO_BROADCAST 0x0020
#endif

#ifndef SO_SNDBUF
#define SO_SNDBUF 0x1001
#endif

#ifndef SO_RCVBUF
#define SO_RCVBUF 0x1002
#endif

#ifndef SO_SNDTIMEO
#define SO_SNDTIMEO 0x1005
#endif

#ifndef SO_RCVTIMEO
#define SO_RCVTIMEO 0x1006
#endif

#ifndef SO_ERROR
#define SO_ERROR 0x1007
#endif

#ifndef SO_TYPE
#define SO_TYPE 0x1008
#endif

/* Flags for send/recv */
#ifndef MSG_PEEK
#define MSG_PEEK 0x02
#endif

#ifndef MSG_WAITALL
#define MSG_WAITALL 0x08
#endif

#ifndef MSG_DONTWAIT
#define MSG_DONTWAIT 0x40
#endif

/* Generic socket address */
#ifndef _SOCKADDR_DECLARED
#define _SOCKADDR_DECLARED
struct sockaddr {
    uint8_t  sa_len;
    uint8_t  sa_family;
    char     sa_data[14];
};
#endif

/* Socket address storage */
#ifndef _SOCKADDR_STORAGE_DECLARED
#define _SOCKADDR_STORAGE_DECLARED
struct sockaddr_storage {
    uint8_t  ss_len;
    uint8_t  ss_family;
    char     __ss_pad1[6];
    int64_t  __ss_align;
    char     __ss_pad2[112];
};
#endif

/* Type definitions */
#ifndef _SOCKLEN_T_DECLARED
#define _SOCKLEN_T_DECLARED
typedef uint32_t socklen_t;
#endif

#ifndef _SA_FAMILY_T_DECLARED
#define _SA_FAMILY_T_DECLARED
typedef uint8_t sa_family_t;
#endif

/* iovec structure */
#ifndef _IOVEC_DECLARED
#define _IOVEC_DECLARED
struct iovec {
    void   *iov_base;
    size_t  iov_len;
};
#endif

/* msghdr structure for sendmsg/recvmsg */
struct msghdr {
    void         *msg_name;
    socklen_t     msg_namelen;
    struct iovec *msg_iov;
    int           msg_iovlen;
    void         *msg_control;
    socklen_t     msg_controllen;
    int           msg_flags;
};

/* cmsghdr structure for ancillary data */
struct cmsghdr {
    socklen_t cmsg_len;    /* data byte count, including header */
    int       cmsg_level;  /* originating protocol */
    int       cmsg_type;   /* protocol-specific type */
    /* followed by unsigned char cmsg_data[] */
};

/* CMSG macros for control message handling */
#define CMSG_ALIGN(len) (((len) + sizeof(size_t) - 1) & ~(sizeof(size_t) - 1))
#define CMSG_SPACE(len) (CMSG_ALIGN(sizeof(struct cmsghdr)) + CMSG_ALIGN(len))
#define CMSG_LEN(len)   (CMSG_ALIGN(sizeof(struct cmsghdr)) + (len))
#define CMSG_DATA(cmsg) ((unsigned char *)((cmsg) + 1))

#define CMSG_FIRSTHDR(msg) \
    ((msg)->msg_controllen >= sizeof(struct cmsghdr) ? \
     (struct cmsghdr *)(msg)->msg_control : (struct cmsghdr *)0)

#define CMSG_NXTHDR(msg, cmsg) \
    (((char *)(cmsg) + CMSG_ALIGN((cmsg)->cmsg_len) + sizeof(struct cmsghdr) > \
      (char *)(msg)->msg_control + (msg)->msg_controllen) ? \
     (struct cmsghdr *)0 : \
     (struct cmsghdr *)((char *)(cmsg) + CMSG_ALIGN((cmsg)->cmsg_len)))

/* Protocol numbers */
#ifndef IPPROTO_IP
#define IPPROTO_IP      0
#endif
#ifndef IPPROTO_ICMP
#define IPPROTO_ICMP    1
#endif
#ifndef IPPROTO_TCP
#define IPPROTO_TCP     6
#endif
#ifndef IPPROTO_UDP
#define IPPROTO_UDP     17
#endif
#ifndef IPPROTO_IPV6
#define IPPROTO_IPV6    41
#endif
#ifndef IPPROTO_ICMPV6
#define IPPROTO_ICMPV6  58
#endif
#ifndef IPPROTO_RAW
#define IPPROTO_RAW     255
#endif

/* IP-level socket options */
#ifndef IP_TOS
#define IP_TOS          1
#endif
#ifndef IP_TTL
#define IP_TTL          2
#endif
#ifndef IP_PKTINFO
#define IP_PKTINFO      8
#endif
#ifndef IP_RECVDSTADDR
#define IP_RECVDSTADDR  7
#endif
#ifndef IP_RECVTOS
#define IP_RECVTOS      13
#endif

/* IPv6-level socket options */
#ifndef IPV6_V6ONLY
#define IPV6_V6ONLY     26
#endif
#ifndef IPV6_RECVPKTINFO
#define IPV6_RECVPKTINFO 49
#endif
#ifndef IPV6_PKTINFO
#define IPV6_PKTINFO     50
#endif
#ifndef IPV6_RECVTCLASS
#define IPV6_RECVTCLASS  66
#endif
#ifndef IPV6_TCLASS
#define IPV6_TCLASS      67
#endif

/* Stub socket functions - bare-metal doesn't have sockets by default */
/* These return error values; actual implementation needs lwIP or similar */
static inline int socket(int domain, int type, int protocol) {
    (void)domain; (void)type; (void)protocol;
    return -1;
}

static inline int bind(int sockfd, const struct sockaddr *addr, socklen_t addrlen) {
    (void)sockfd; (void)addr; (void)addrlen;
    return -1;
}

static inline int listen(int sockfd, int backlog) {
    (void)sockfd; (void)backlog;
    return -1;
}

static inline int accept(int sockfd, struct sockaddr *addr, socklen_t *addrlen) {
    (void)sockfd; (void)addr; (void)addrlen;
    return -1;
}

static inline int connect(int sockfd, const struct sockaddr *addr, socklen_t addrlen) {
    (void)sockfd; (void)addr; (void)addrlen;
    return -1;
}

static inline ssize_t send(int sockfd, const void *buf, size_t len, int flags) {
    (void)sockfd; (void)buf; (void)len; (void)flags;
    return -1;
}

static inline ssize_t recv(int sockfd, void *buf, size_t len, int flags) {
    (void)sockfd; (void)buf; (void)len; (void)flags;
    return -1;
}

static inline ssize_t sendto(int sockfd, const void *buf, size_t len, int flags,
                             const struct sockaddr *dest_addr, socklen_t addrlen) {
    (void)sockfd; (void)buf; (void)len; (void)flags; (void)dest_addr; (void)addrlen;
    return -1;
}

static inline ssize_t recvfrom(int sockfd, void *buf, size_t len, int flags,
                               struct sockaddr *src_addr, socklen_t *addrlen) {
    (void)sockfd; (void)buf; (void)len; (void)flags; (void)src_addr; (void)addrlen;
    return -1;
}

static inline int setsockopt(int sockfd, int level, int optname,
                             const void *optval, socklen_t optlen) {
    (void)sockfd; (void)level; (void)optname; (void)optval; (void)optlen;
    return -1;
}

static inline int getsockopt(int sockfd, int level, int optname,
                             void *optval, socklen_t *optlen) {
    (void)sockfd; (void)level; (void)optname; (void)optval; (void)optlen;
    return -1;
}

static inline int close_socket(int sockfd) {
    (void)sockfd;
    return -1;
}

static inline ssize_t sendmsg(int sockfd, const struct msghdr *msg, int flags) {
    (void)sockfd; (void)msg; (void)flags;
    return -1;
}

static inline ssize_t recvmsg(int sockfd, struct msghdr *msg, int flags) {
    (void)sockfd; (void)msg; (void)flags;
    return -1;
}

static inline int shutdown(int sockfd, int how) {
    (void)sockfd; (void)how;
    return -1;
}

static inline int getpeername(int sockfd, struct sockaddr *addr, socklen_t *addrlen) {
    (void)sockfd; (void)addr; (void)addrlen;
    return -1;
}

static inline int getsockname(int sockfd, struct sockaddr *addr, socklen_t *addrlen) {
    (void)sockfd; (void)addr; (void)addrlen;
    return -1;
}

#ifdef __cplusplus
}
#endif

#endif /* QUICR_USE_LWIP */

#endif /* QUICR_BAREMETAL */

#endif /* QUICR_BAREMETAL_SYS_SOCKET_H */
