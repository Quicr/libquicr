/*
 * SPDX-FileCopyrightText: Copyright (c) 2024 Cisco Systems
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Bare-metal pthread stub implementations for single-threaded embedded systems.
 * These functions are no-ops that allow code expecting POSIX threads to compile
 * and run on bare-metal platforms.
 *
 * Also provides mbedtls_ms_time() for bare-metal builds (when MBEDTLS_PLATFORM_MS_TIME_ALT is set).
 */

#include <stddef.h>
#include <stdint.h>

/*
 * mbedtls_ms_time implementation for bare-metal
 * This is called when MBEDTLS_PLATFORM_MS_TIME_ALT is defined.
 *
 * For bare-metal ESP32-S3, this should be implemented using the hardware timer.
 * For now, we provide a stub that increments on each call.
 * The actual implementation should be provided by the application linking against this.
 */
#ifdef MBEDTLS_PLATFORM_MS_TIME_ALT

/* Default weak implementation - should be overridden by application */
static volatile int64_t quicr_baremetal_time_ms = 0;

/* Weak symbol so application can override */
__attribute__((weak))
int64_t mbedtls_ms_time(void) {
    /* Simple stub - in real application, this should use hardware timer.
     * This weak implementation just increments to ensure unique timestamps.
     * Applications should override with actual time source (e.g., ESP timer). */
    return ++quicr_baremetal_time_ms;
}

/* Function to set the current time - call from main application */
__attribute__((weak))
void quicr_baremetal_set_time_ms(int64_t time_ms) {
    quicr_baremetal_time_ms = time_ms;
}

#endif /* MBEDTLS_PLATFORM_MS_TIME_ALT */

/* Mutex functions - single threaded, so these are no-ops */
int quicr_pthread_mutex_init(void *m, const void *a) {
    (void)m; (void)a;
    return 0;
}

int quicr_pthread_mutex_destroy(void *m) {
    (void)m;
    return 0;
}

int quicr_pthread_mutex_lock(void *m) {
    (void)m;
    return 0;
}

int quicr_pthread_mutex_unlock(void *m) {
    (void)m;
    return 0;
}

int quicr_pthread_mutex_trylock(void *m) {
    (void)m;
    return 0;
}

/* Condition variable functions - no-ops */
int quicr_pthread_cond_init(void *c, const void *a) {
    (void)c; (void)a;
    return 0;
}

int quicr_pthread_cond_destroy(void *c) {
    (void)c;
    return 0;
}

int quicr_pthread_cond_wait(void *c, void *m) {
    (void)c; (void)m;
    return 0;
}

int quicr_pthread_cond_signal(void *c) {
    (void)c;
    return 0;
}

int quicr_pthread_cond_broadcast(void *c) {
    (void)c;
    return 0;
}

int quicr_pthread_cond_timedwait(void *c, void *m, const void *abstime) {
    (void)c; (void)m; (void)abstime;
    return 0;
}

/* Thread identification - return a constant since we're single-threaded */
unsigned long quicr_pthread_self(void) {
    return 1;
}

/* Once functions */
/* Simple implementation - track if init has been called */
typedef struct {
    int is_initialized;
    int init_executed;
} quicr_pthread_once_internal_t;

int quicr_pthread_once(void *once, void (*routine)(void)) {
    quicr_pthread_once_internal_t *once_ctrl = (quicr_pthread_once_internal_t *)once;
    if (once_ctrl && !once_ctrl->init_executed) {
        once_ctrl->init_executed = 1;
        if (routine) routine();
    }
    return 0;
}

/* Thread-local storage stubs */
/* For single-threaded, we can use a simple static storage */
#define QUICR_MAX_PTHREAD_KEYS 16
static void *tls_values[QUICR_MAX_PTHREAD_KEYS];
static int tls_key_allocated[QUICR_MAX_PTHREAD_KEYS];
static void (*tls_destructors[QUICR_MAX_PTHREAD_KEYS])(void*);

int quicr_pthread_key_create(void *key, void (*destructor)(void*)) {
    unsigned long *key_ptr = (unsigned long *)key;
    for (int i = 0; i < QUICR_MAX_PTHREAD_KEYS; i++) {
        if (!tls_key_allocated[i]) {
            tls_key_allocated[i] = 1;
            tls_values[i] = NULL;
            tls_destructors[i] = destructor;
            *key_ptr = (unsigned long)i;
            return 0;
        }
    }
    return -1; /* No keys available */
}

int quicr_pthread_key_delete(unsigned long key) {
    if (key < QUICR_MAX_PTHREAD_KEYS && tls_key_allocated[key]) {
        tls_key_allocated[key] = 0;
        tls_values[key] = NULL;
        tls_destructors[key] = NULL;
        return 0;
    }
    return -1;
}

void *quicr_pthread_getspecific(unsigned long key) {
    if (key < QUICR_MAX_PTHREAD_KEYS && tls_key_allocated[key]) {
        return tls_values[key];
    }
    return NULL;
}

int quicr_pthread_setspecific(unsigned long key, const void *value) {
    if (key < QUICR_MAX_PTHREAD_KEYS && tls_key_allocated[key]) {
        tls_values[key] = (void *)value;
        return 0;
    }
    return -1;
}

/* Thread creation stubs - not supported on bare-metal */
int quicr_pthread_create(void *thread, const void *attr,
                         void *(*start_routine)(void*), void *arg) {
    (void)thread; (void)attr; (void)start_routine; (void)arg;
    return -1;  /* Not supported */
}

int quicr_pthread_join(unsigned long thread, void **retval) {
    (void)thread; (void)retval;
    return -1;  /* Not supported */
}

/* Note: mbedtls filesystem functions are now compiled by mbedtls itself
 * since we keep MBEDTLS_FS_IO defined. The actual file I/O will fail at
 * runtime on bare-metal but that's OK since we don't use file-based certs. */
