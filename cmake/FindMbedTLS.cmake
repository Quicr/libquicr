# SPDX-FileCopyrightText: Copyright (c) 2024 Cisco Systems
# SPDX-License-Identifier: BSD-2-Clause

set(MBEDTLS_HEADERS mbedtls/ssl.h)
if (PLATFORM_ESP_IDF)
  list(APPEND MBEDTLS_HEADERS mbedtls/esp_config.h)
endif()
find_path(MBEDTLS_INCLUDE_DIRS ${MBEDTLS_HEADERS})

find_library(MBEDTLS_LIBRARY mbedtls)
find_library(MBEDX509_LIBRARY mbedx509)
find_library(MBEDCRYPTO_LIBRARY mbedcrypto)

set(MBEDTLS_LIBRARIES "${MBEDTLS_LIBRARY}" "${MBEDX509_LIBRARY}" "${MBEDCRYPTO_LIBRARY}")

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(MbedTLS DEFAULT_MSG
  MBEDTLS_INCLUDE_DIRS MBEDTLS_LIBRARY MBEDX509_LIBRARY MBEDCRYPTO_LIBRARY)

mark_as_advanced(MBEDTLS_INCLUDE_DIRS MBEDTLS_LIBRARY MBEDX509_LIBRARY MBEDCRYPTO_LIBRARY)
