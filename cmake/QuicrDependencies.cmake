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
    message(STATUS "Transport building with MbedTLS")

    set(WITH_OPENSSL OFF)
endif ()

if (NOT WITH_MBEDTLS AND WITH_BORINGSSL)
    message(STATUS "Transport building with BoringSSL")

    # Android's NDK ships no linkable system OpenSSL, so pull BoringSSL and use
    # it as the OpenSSL provider for picotls/picoquic. Pinned to release
    # 0.20260813.0. EXCLUDE_FROM_ALL keeps its test/tool targets out of the
    # build; only crypto/ssl/decrepit get pulled in as link dependencies.
    CPMAddPackage(
        URI "gh:google/boringssl#7c1efd8d6ffb36a57feba44e8c73cf674801f3cb"
        EXCLUDE_FROM_ALL YES)

    # picotls' BORINGSSL_ADJUST() and picoquic consume the backend via these names.
    add_library(BoringSSL::crypto ALIAS crypto)
    add_library(BoringSSL::ssl ALIAS ssl)
    add_library(BoringSSL::decrepit ALIAS decrepit)

    # libquicr may be built SHARED, so the static crypto it embeds must be PIC.
    set_property(TARGET crypto ssl decrepit PROPERTY POSITION_INDEPENDENT_CODE ON)

    # picotls and picoquic each call find_package(OpenSSL); intercept it and
    # resolve OpenSSL to the in-tree BoringSSL. Mirrors the install() override
    # at the top of this file. picotls' BORINGSSL_ADJUST() keys off
    # openssl/base.h in OPENSSL_INCLUDE_DIR and the "BoringSSL::ssl" entry in
    # OPENSSL_LIBRARIES to wire in the decrepit library it needs.
    set(QUICR_BORINGSSL_INCLUDE_DIR ${boringssl_SOURCE_DIR}/include)
    macro(find_package)
        if ("${ARGV0}" STREQUAL "OpenSSL")
            set(OPENSSL_FOUND TRUE)
            set(OPENSSL_VERSION "1.1.1")
            set(OPENSSL_INCLUDE_DIR ${QUICR_BORINGSSL_INCLUDE_DIR})
            set(OPENSSL_CRYPTO_LIBRARY BoringSSL::crypto)
            set(OPENSSL_CRYPTO_LIBRARIES BoringSSL::crypto)
            set(OPENSSL_SSL_LIBRARY BoringSSL::ssl)
            set(OPENSSL_LIBRARIES BoringSSL::ssl BoringSSL::crypto)
        else ()
            _find_package(${ARGV})
        endif ()
    endmacro()

    # picoquic unconditionally exports its targets, and that export pulls in
    # picotls-openssl -> BoringSSL::decrepit, an in-tree static lib that isn't
    # part of any export set (generate-time error). libquicr never installs
    # picoquic, so drop that specific export instead of fighting the export
    # graph. Composes with the framework install() override above.
    macro(install)
        set(_quicr_install_args "${ARGN}")
        if ("EXPORT" IN_LIST _quicr_install_args AND "picoquic-targets" IN_LIST _quicr_install_args)
            message(STATUS "Skipping install(EXPORT picoquic-targets) for BoringSSL build")
        else ()
            _install(${ARGN})
        endif ()
    endmacro()
endif ()

set(WITH_FUSION OFF)
set(BUILD_DEMO OFF)
set(BUILD_PQBENCH OFF)
set(BUILD_LOGREADER OFF)
set(picotls_BUILD_TESTS OFF)

CPMAddPackage("gh:quicr/picotls#sync-092226")

if (NOT WITH_MBEDTLS)
    if (TARGET picotls-openssl)
        set(WITH_OPENSSL ON)
    else()
        set(WITH_OPENSSL OFF)
        message(WARNING
            "No external TLS provider was found; using picotls-minicrypto. "
            "This backend does not verify peer certificates in the current picoquic integration. "
            "Use it only where peer authentication is not required.")
    endif()
endif()

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

if (WITH_BORINGSSL)
    add_dependencies(picotls-openssl crypto ssl decrepit)
    add_dependencies(picoquic-core crypto ssl)
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
