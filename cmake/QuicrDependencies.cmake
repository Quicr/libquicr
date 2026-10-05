# SPDX-FileCopyrightText: Copyright (c) 2024 Cisco Systems
# SPDX-License-Identifier: BSD-2-Clause

# Override install command to add FRAMEWORK DESTINATION if missing. This is needed for framework (apple) builds
if(CMAKE_FRAMEWORK)
    macro(install)
        set (list_args "${ARGN}")
        if ("TARGETS" IN_LIST list_args AND NOT "FRAMEWORK" IN_LIST list_args)
            list(APPEND list_args "FRAMEWORK" "DESTINATION" "framework" "BUNDLE" "DESTINATION" "bundle")
            message(STATUS "Adding FRAMEWORK DESTINATION to install: ${list_args}")
            _install(${list_args})
        else ()
            message(STATUS "unchanged install: ${list_args}")
            _install(${ARGN})
        endif ()
    endmacro()
endif()


set(BUILD_SHARED_LIBS OFF)
set(BUILD_STATIC_LIBS ON)

CPMAddPackage("gh:quicr/timeq#898d45c")

if (WITH_MBEDTLS)
    set(WITH_OPENSSL OFF)
    if (QUICR_FETCH_MBEDTLS)
        CPMAddPackage(
            NAME mbedtls
            GITHUB_REPOSITORY Mbed-TLS/mbedtls
            GIT_TAG v3.6.7
            OPTIONS
                "ENABLE_PROGRAMS OFF"
                "ENABLE_TESTING OFF"
                "DISABLE_PACKAGE_CONFIG_AND_INSTALL OFF"
        )

        # NOTE: picotls and picoquic currently support legacy discovery of mbedtls, and require these variables be set.
        set(MBEDTLS_INCLUDE_DIR "${mbedtls_SOURCE_DIR}/include")
        set(MBEDTLS_INCLUDE_DIRS "${mbedtls_SOURCE_DIR}/include")
        set(MBEDTLS_LIBRARY MbedTLS::mbedtls)
        set(MBEDTLS_X509 MbedTLS::mbedx509)
        set(MBEDTLS_CRYPTO MbedTLS::mbedcrypto)
        set(MBEDTLS_LIBRARIES MbedTLS::mbedtls MbedTLS::mbedx509 MbedTLS::mbedcrypto)
    endif()
else ()
    set(WITH_OPENSSL ON)
    if (QUICR_FETCH_BORINGSSL)
        message(WARNING
            "Using QuicR's fetched BoringSSL as an OpenSSL-compatible TLS backend. "
            "This overrides OpenSSL discovery for this build. Set QUICR_FETCH_BORINGSSL=OFF "
            "and provide OPENSSL_ROOT_DIR to use an externally built TLS stack.")
        CPMAddPackage(
            NAME boringssl
            GITHUB_REPOSITORY google/boringssl
            GIT_TAG 0.20260929.0
            OPTIONS "BUILD_TESTING OFF"
        )

        # Aliases are required for tricking cmake into using BoringSSL as OpenSSL.
        add_library(BoringSSL::ssl ALIAS ssl)
        add_library(BoringSSL::crypto ALIAS crypto)
        add_library(BoringSSL::decrepit INTERFACE IMPORTED GLOBAL)
        target_link_libraries(BoringSSL::decrepit INTERFACE decrepit)

        set(OPENSSL_INCLUDE_DIR "${boringssl_SOURCE_DIR}/include" CACHE PATH "" FORCE)
        set(OPENSSL_SSL_LIBRARY BoringSSL::ssl CACHE STRING "" FORCE)
        set(OPENSSL_CRYPTO_LIBRARY BoringSSL::crypto CACHE STRING "" FORCE)
    endif()
endif ()

set(WITH_FUSION OFF)
set(BUILD_DEMO OFF)
set(BUILD_PQBENCH OFF)
set(BUILD_LOGREADER OFF)
set(picotls_BUILD_TESTS OFF)

# Picoquic still errors out if this is not set. Blanking it only for the duration of the
# picotls/picoquic configure keeps find_package(OpenSSL) working for everything else.
if (WITH_MBEDTLS)
    set(OPENSSL_INCLUDE_DIR "")
endif()

CPMAddPackage("gh:quicr/picotls#sync-092226")

set(PTLS_INCLUDE_DIR ${picotls_SOURCE_DIR}/include)

if (WIN32 AND NOT WITH_MBEDTLS)
    target_compile_definitions(picotls-core PUBLIC _WINDOWS)
    target_include_directories(picotls-core PUBLIC
        "$<BUILD_INTERFACE:${picotls_SOURCE_DIR}/picotlsvs/picotls>")
    target_sources(picotls-core PRIVATE
        "${picotls_SOURCE_DIR}/picotlsvs/picotls/wintimeofday.c")

    target_link_libraries(picotls-minicrypto bcrypt)

    foreach(tgt cli test-openssl.t)
        if (TARGET ${tgt})
            set_target_properties(${tgt} PROPERTIES EXCLUDE_FROM_ALL ON)
        endif()
    endforeach()
endif()

set(picoquic_BUILD_TESTS OFF)
set(PICOQUIC_FETCH_PTLS ON)
CPMAddPackage("gh:private-octopus/picoquic#poll-recvmmsg")
add_dependencies(picoquic-core picotls-core)

if (WITH_MBEDTLS)
    unset(OPENSSL_INCLUDE_DIR)
endif()

set(WARN_SUPPRESS_TARGET_LIST picoquic-core picoquic-log picohttp-core picotls-core picotls-openssl picotls-minicrypto)

if (WITH_MBEDTLS)
    list(APPEND WARN_SUPPRESS_TARGET_LIST picotls-mbedtls)
endif()

if (WIN32)
    target_link_libraries(picoquic-core PUBLIC ws2_32 iphlpapi)
endif()

foreach(tgt ${WARN_SUPPRESS_TARGET_LIST})
    if (TARGET ${tgt})
        target_compile_options(${tgt} PRIVATE
            $<$<C_COMPILER_ID:MSVC>:/w>
            $<$<NOT:$<C_COMPILER_ID:MSVC>>:-w>)
    endif()
endforeach()
