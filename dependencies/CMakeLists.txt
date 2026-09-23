# SPDX-FileCopyrightText: Copyright (c) 2024 Cisco Systems
# SPDX-License-Identifier: BSD-2-Clause

# ----------------------------------------------------
# Command overrides
# ----------------------------------------------------

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

# --------------------------------------------------------
# Submodules
# --------------------------------------------------------


set(BUILD_SHARED_LIBS OFF)
set(BUILD_STATIC_LIBS ON)

# Time queue
add_subdirectory(timeq)

# mbedtls
if (${USE_MBEDTLS})
    message("Transport building with MBedTLS")

    if (NOT TARGET mbedcrypto) # If not already available on build server
        # Setup python environment for mbedtls to find.
        find_package(Python3 REQUIRED)
        execute_process(
                COMMAND
                ${Python3_EXECUTABLE} -m venv ${CMAKE_CURRENT_BINARY_DIR}/venv
                RESULT_VARIABLE
                MBEDTLS_VIRTUALENV
        )
        if (MBEDTLS_VIRTUALENV)
            message(FATAL_ERROR "Failed to create mbedtls virtual envrionment")
        endif (MBEDTLS_VIRTUALENV)

        # Override python lookup to use virtualenv when mbedtls later searches for it.
        set(Python3_EXECUTABLE ${CMAKE_CURRENT_BINARY_DIR}/venv/bin/python)

        # Install mbedtls dependencies into virtualenv.
        execute_process(
                COMMAND
                ${Python3_EXECUTABLE} -m pip install -r ${CMAKE_CURRENT_SOURCE_DIR}/mbedtls/scripts/basic.requirements.txt
                RESULT_VARIABLE
                MBEDTLS_PIP
        )
        if (MBEDTLS_PIP)
            message(FATAL_ERROR "Failed to install mbedtls dependencies")
        endif (MBEDTLS_PIP)

        # Continue with mbedtls inclusion.
        option(ENABLE_TESTING OFF)
        option(ENABLE_PROGRAMS OFF)
        set(MBEDTLS_AS_SUBPROJECT ON)
        add_subdirectory(mbedtls) #TODO: Remove this and use CPMAddPackage instead.

        set(MBEDTLS_ROOT_DIR ${CMAKE_CURRENT_SOURCE_DIR}/mbedtls)
        set(MBEDTLS_INCLUDE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/mbedtls ${CMAKE_CURRENT_SOURCE_DIR}/mbedtls/include)

    elseif(DEFINED ENV{IDF_PATH})
        set(MBEDTLS_ROOT_DIR ${IDF_PATH}/components/mbedtls/mbedtls)
        set(MBEDTLS_INCLUDE_DIR ${IDF_PATH}/components/mbedtls/mbedtls ${IDF_PATH}/components/mbedtls/mbedtls/include ${IDF_PATH}/components/mbedtls/port/include)
    endif()

    set(MBEDTLS_LIBRARY mbedtls)
    set(MBEDTLS_CRYPTO mbedcrypto)
    set(MBEDTLS_X509 mbedx509)

    set(MBEDTLS_INCLUDE_DIRS ${MBEDTLS_INCLUDE_DIR})
    set(MBEDTLS_LIBRARIES mbedtls mbedcrypto mbedx509)

    # picoTLS
    # picoTLS uses its own find mbedtls, which works with the above settings.
    set(WITH_MBEDTLS ON)
    set(WITH_FUSION OFF)
    set(picotls_BUILD_TESTS OFF)

    CPMAddPackage("gh:quicr/picotls#3927c48be674d97ca8242580fab3b4af4eb6df2a")

    add_dependencies(picotls-mbedtls mbedtls)

    if (DEFINED ENV{IDF_PATH})
        set(MBEDTLS_PREFIX ${IDF_PATH}/components/mbedtls/mbedtls)
        set(WITH_SELECT ON)
    else()
        set(MBEDTLS_PREFIX ${CMAKE_CURRENT_SOURCE_DIR}/mbedtls)
    endif()

    set(WITH_MBEDTLS ON)

    set(OPENSSL_INCLUDE_DIR "") # Picoquic will still error if this is not set
    set(WITH_OPENSSL OFF)
    set(BUILD_HTTP ON)
    set(BUILD_DEMO OFF)
    set(BUILD_PQBENCH OFF)
    set(BUILD_LOGREADER OFF)

    set(PICOQUIC_ADDITIONAL_C_FLAGS -Wno-error=format)
    set(PICOQUIC_ADDITIONAL_CXX_FLAGS -Wno-error=format)

    set(PTLS_INCLUDE_DIR ${picotls_SOURCE_DIR}/include)
    set(picoquic_BUILD_TESTS OFF)
    set(PICOQUIC_FETCH_PTLS ON)

    add_subdirectory(picoquic)
    add_dependencies(picoquic-core picotls-core mbedtls)
else ()
    if (USE_BORINGSSL)
        message("Transport building with BoringSSL")

        # Build the in-tree BoringSSL. Android's NDK ships no linkable system
        # OpenSSL, so this replaces the system libssl/libcrypto that picotls and
        # picoquic would otherwise expect.
        #
        # Captured here (dependencies/ scope) so the find_package() macro below
        # resolves the include path correctly. Macros re-evaluate ${...} in the
        # caller's scope, so CMAKE_CURRENT_SOURCE_DIR must not be used there.
        set(QUICR_BORINGSSL_INCLUDE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/boringssl/include)
        add_subdirectory(boringssl)

        # picotls' BORINGSSL_ADJUST() and picoquic both consume the OpenSSL
        # backend through these target names.
        add_library(BoringSSL::crypto ALIAS crypto)
        add_library(BoringSSL::ssl ALIAS ssl)
        add_library(BoringSSL::decrepit ALIAS decrepit)

        # libquicr may be built SHARED, so the static crypto it embeds must be PIC.
        set_property(TARGET crypto ssl decrepit PROPERTY POSITION_INDEPENDENT_CODE ON)

        # picotls and picoquic each call find_package(OpenSSL) and reset
        # CMAKE_MODULE_PATH to their own cmake/ dir, so a FindOpenSSL shim on our
        # module path would never be seen. Intercept the command instead and
        # resolve OpenSSL to the in-tree BoringSSL. This mirrors the install()
        # override at the top of this file. picotls' BORINGSSL_ADJUST() keys off
        # openssl/base.h in OPENSSL_INCLUDE_DIR and the "BoringSSL::ssl" entry in
        # OPENSSL_LIBRARIES to wire in the decrepit library it needs.
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
    endif ()

    set(WITH_FUSION OFF)
    set(WITH_OPENSSL ON)
    set(ENABLE_TESTING OFF)
    set(BUILD_DEMO OFF)
    set(BUILD_PQBENCH OFF)
    set(BUILD_LOGREADER OFF)

    CPMAddPackage("gh:quicr/picotls#3927c48be674d97ca8242580fab3b4af4eb6df2a")

    if (WIN32)
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

    set(PICOQUIC_FETCH_PTLS ON)
    set(picoquic_BUILD_TESTS OFF)
    add_subdirectory(picoquic)
    add_dependencies(picoquic-core picotls-core)

    if (USE_BORINGSSL)
        add_dependencies(picotls-openssl crypto ssl decrepit)
        add_dependencies(picoquic-core crypto ssl)
    endif()

    if (WIN32)
        target_link_libraries(picoquic-core PUBLIC ws2_32 iphlpapi)
    endif()

    set(WARN_SUPPRESS_TARGET_LIST picoquic-core picoquic-log picohttp-core picotls-core picotls-openssl picotls-minicrypto)
    foreach(tgt ${WARN_SUPPRESS_TARGET_LIST})
        target_compile_options(${tgt} PRIVATE
            $<$<C_COMPILER_ID:MSVC>:/w>
            $<$<NOT:$<C_COMPILER_ID:MSVC>>:-w>)
    endforeach()
endif ()
