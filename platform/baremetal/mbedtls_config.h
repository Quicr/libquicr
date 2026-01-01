/*
 * SPDX-FileCopyrightText: Copyright (c) 2024 Cisco Systems
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Custom mbedTLS configuration for bare-metal ESP32-S3
 */

#ifndef QUICR_BAREMETAL_MBEDTLS_CONFIG_H
#define QUICR_BAREMETAL_MBEDTLS_CONFIG_H

/* First, include the default mbedtls config */
#include "mbedtls/mbedtls_config.h"

/* Undefine modules not available on bare-metal */
#undef MBEDTLS_NET_C              /* Network sockets - requires Unix/Windows */
#undef MBEDTLS_TIMING_C           /* Unix/Windows timing functions */
/* Note: Keep MBEDTLS_FS_IO defined for declarations - stubs provided in pthread_stubs.c */
#undef MBEDTLS_THREADING_C        /* Threading support */
#undef MBEDTLS_THREADING_PTHREAD  /* POSIX threading */

/* Enable bare-metal specific options */
#define MBEDTLS_PLATFORM_MS_TIME_ALT  /* Use custom ms time function */
#define MBEDTLS_NO_PLATFORM_ENTROPY   /* No platform entropy source */
#define MBEDTLS_NO_DEFAULT_ENTROPY_SOURCES

/* Reduce memory footprint for embedded */
#define MBEDTLS_AES_ROM_TABLES
#define MBEDTLS_MPI_WINDOW_SIZE 2
#define MBEDTLS_MPI_MAX_SIZE 512

/* Disable debug on bare-metal */
#undef MBEDTLS_DEBUG_C

#endif /* QUICR_BAREMETAL_MBEDTLS_CONFIG_H */
