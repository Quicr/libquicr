/*
 * SPDX-FileCopyrightText: Copyright (c) 2024 Cisco Systems
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Bare-metal compatibility header for quicr
 * This is force-included before other headers.
 *
 * Note: This header must be compatible with newlib (xtensa-esp-elf toolchain).
 * The xtensa newlib already provides pthread types via sys/_pthreadtypes.h
 * so we only provide stub implementations for the functions.
 */

#ifndef QUICR_BAREMETAL_COMPAT_H
#define QUICR_BAREMETAL_COMPAT_H

#ifdef QUICR_BAREMETAL

/* Include standard headers first so we know what's already defined */
#include <stdint.h>
#include <stddef.h>

/*
 * Define O_CLOEXEC early - newlib doesn't define it but picotls uses it.
 * This must be defined before fcntl.h is included anywhere.
 */
#ifndef O_CLOEXEC
#define O_CLOEXEC 0  /* No effect on bare-metal single-process system */
#endif

/*
 * Socket address structures - needed by picotls before netinet/in.h is found.
 * Define these early since this header is force-included.
 */

/* Address families */
#ifndef AF_INET
#define AF_INET 2
#endif

#ifndef AF_INET6
#define AF_INET6 10
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

/*
 * For posix_memalign: only define if not already available
 * newlib may or may not have this depending on configuration
 */
#ifndef HAVE_POSIX_MEMALIGN
/* Use memalign which is available in newlib */
#include <malloc.h>
#ifdef __cplusplus
extern "C" {
#endif
static inline int quicr_posix_memalign(void **memptr, size_t alignment, size_t size) {
    void *p = memalign(alignment, size);
    if (p == NULL) return -1;
    *memptr = p;
    return 0;
}
#ifdef __cplusplus
}
#endif
/* Map posix_memalign to our wrapper - but only in our code, not stdlib */
#define posix_memalign quicr_posix_memalign
#endif

/*
 * Include system pthread types - newlib provides these via sys/_pthreadtypes.h
 * included from sys/types.h. We DO NOT define our own types.
 */
#include <sys/types.h>

/* Include pthread.h if available to get proper declarations */
#if defined(_POSIX_THREADS) || defined(__unix__) || defined(__linux__)
#include <pthread.h>
#else
/*
 * Newlib on xtensa provides types but not functions.
 * We need to provide stub function declarations (not implementations yet,
 * as the types are already declared in newlib headers that will be
 * included later).
 *
 * The trick: We'll define these as weak symbols or inline functions
 * AFTER the proper types are defined.
 */
#endif

/*
 * Provide macro for PTHREAD_MUTEX_INITIALIZER if not defined
 * These need to match the newlib pthread type sizes
 */
#ifndef PTHREAD_MUTEX_INITIALIZER
#define PTHREAD_MUTEX_INITIALIZER ((pthread_mutex_t)0)
#endif
#ifndef PTHREAD_COND_INITIALIZER
#define PTHREAD_COND_INITIALIZER ((pthread_cond_t)0)
#endif
#ifndef PTHREAD_ONCE_INIT
#define PTHREAD_ONCE_INIT {0, 0}
#endif

/*
 * Stub pthread function implementations for single-threaded bare-metal.
 * These are implemented as macros that expand to simple values,
 * since we can't safely create inline functions before the types
 * from newlib are fully defined.
 */

/* We'll define the actual stub implementations in a separate .c file
 * or as weak symbols that get linked in. For now, declare them as extern. */

#ifdef __cplusplus
extern "C" {
#endif

/* Declare pthread stubs that we'll implement separately */
/* These will be weak symbols so they can be overridden if needed */

/* Mutex functions - single threaded, so these are no-ops */
int quicr_pthread_mutex_init(void *m, const void *a);
int quicr_pthread_mutex_destroy(void *m);
int quicr_pthread_mutex_lock(void *m);
int quicr_pthread_mutex_unlock(void *m);
int quicr_pthread_mutex_trylock(void *m);

/* Condition variable functions - no-ops */
int quicr_pthread_cond_init(void *c, const void *a);
int quicr_pthread_cond_destroy(void *c);
int quicr_pthread_cond_wait(void *c, void *m);
int quicr_pthread_cond_signal(void *c);
int quicr_pthread_cond_broadcast(void *c);
int quicr_pthread_cond_timedwait(void *c, void *m, const void *abstime);

/* Thread identification */
unsigned long quicr_pthread_self(void);

/* Once functions */
int quicr_pthread_once(void *once, void (*routine)(void));

/* Thread-local storage stubs */
int quicr_pthread_key_create(void *key, void (*destructor)(void*));
int quicr_pthread_key_delete(unsigned long key);
void *quicr_pthread_getspecific(unsigned long key);
int quicr_pthread_setspecific(unsigned long key, const void *value);

/* Thread creation stubs - not supported */
int quicr_pthread_create(void *thread, const void *attr,
                         void *(*start_routine)(void*), void *arg);
int quicr_pthread_join(unsigned long thread, void **retval);

#ifdef __cplusplus
}
#endif

/* Map pthread functions to our stubs via macros */
#define pthread_mutex_init(m, a)      quicr_pthread_mutex_init((void*)(m), (const void*)(a))
#define pthread_mutex_destroy(m)      quicr_pthread_mutex_destroy((void*)(m))
#define pthread_mutex_lock(m)         quicr_pthread_mutex_lock((void*)(m))
#define pthread_mutex_unlock(m)       quicr_pthread_mutex_unlock((void*)(m))
#define pthread_mutex_trylock(m)      quicr_pthread_mutex_trylock((void*)(m))

#define pthread_cond_init(c, a)       quicr_pthread_cond_init((void*)(c), (const void*)(a))
#define pthread_cond_destroy(c)       quicr_pthread_cond_destroy((void*)(c))
#define pthread_cond_wait(c, m)       quicr_pthread_cond_wait((void*)(c), (void*)(m))
#define pthread_cond_signal(c)        quicr_pthread_cond_signal((void*)(c))
#define pthread_cond_broadcast(c)     quicr_pthread_cond_broadcast((void*)(c))
#define pthread_cond_timedwait(c,m,t) quicr_pthread_cond_timedwait((void*)(c), (void*)(m), (const void*)(t))

#define pthread_self()                quicr_pthread_self()

#define pthread_once(o, r)            quicr_pthread_once((void*)(o), (r))

#define pthread_key_create(k, d)      quicr_pthread_key_create((void*)(k), (d))
#define pthread_key_delete(k)         quicr_pthread_key_delete((unsigned long)(k))
#define pthread_getspecific(k)        quicr_pthread_getspecific((unsigned long)(k))
#define pthread_setspecific(k, v)     quicr_pthread_setspecific((unsigned long)(k), (v))

#define pthread_create(t, a, r, g)    quicr_pthread_create((void*)(t), (const void*)(a), (r), (g))
#define pthread_join(t, r)            quicr_pthread_join((unsigned long)(t), (r))

#endif /* QUICR_BAREMETAL */

#endif /* QUICR_BAREMETAL_COMPAT_H */
