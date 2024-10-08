# SPDX-FileCopyrightText: Copyright (c) 2024 Cisco Systems
# SPDX-License-Identifier: BSD-2-Clause

if (NOT ${USE_MBEDTLS})

    set (OPENSSL_FOUND TRUE)

    set (OPENSSL_CRYPTO_LIBRARY BoringSSL::crypto)
    set (OPENSSL_CRYPTO_LIBRARIES BoringSSL::crypto)
    set (OPENSSL_SSL_LIBRARY BoringSSL::ssl)
    set (OPENSSL_LIBRARIES BoringSSL::ssl BoringSSL::crypto)

    set (OPENSSL_VERSION "1.0.1f")
    set (OPENSSL_VERSION_NUMBER 1)
    set (OPENSSL_VERSION_MAJOR 1)
    set (OPENSSL_VERSION_MINOR 0)
    set (OPENSSL_VERSION_FIX 1)
    set (OPENSSL_VERSION_PATCH "t")

    find_path(OPENSSL_INCLUDE_DIR
            NAMES
            openssl/opensslv.h
            HINTS
            ${CMAKE_CURRENT_SOURCE_DIR}/dependencies/boringssl/include
    )

    mark_as_advanced(OPENSSL_INCLUDE_DIR OPENSSL_LIBRARIES OPENSSL_CRYPTO_LIBRARY OPENSSL_SSL_LIBRARY)
endif ()